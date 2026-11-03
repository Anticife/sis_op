#!/usr/bin/env python3
"""
scripts/live_server.py
Servidor Web de Subida y Procesamiento de Archivos en Linux conectado al Planificador POSIX (./simulador).
- Inicia completamente limpio (uploads/ vacío).
- Asigna automáticamente prioridad y ráfagas por peso de archivo y algoritmo.
- Crea procesos hijos reales (fork) visibles exclusivamente en:
    ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest
"""

import os
import re
import time
import json
import base64
import signal
import hashlib
import subprocess
import shutil
from urllib.parse import urlparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")
UPLOADS_DIR = os.path.join(PROJECT_DIR, "uploads")
SIM_BIN = os.path.join(PROJECT_DIR, "simulador")
LIVE_STATE_FILE = "/tmp/simulador_live_state.json"

ACTIVE_PROC = None
UPLOADED_FILES_META = {}


def clean_uploads_dir():
    """Limpia el directorio uploads y el archivo de estado para iniciar siempre vacío."""
    global UPLOADED_FILES_META
    UPLOADED_FILES_META = {}
    if os.path.exists(UPLOADS_DIR):
        for item in os.listdir(UPLOADS_DIR):
            item_path = os.path.join(UPLOADS_DIR, item)
            try:
                if os.path.isfile(item_path) or os.path.islink(item_path):
                    os.unlink(item_path)
                elif os.path.isdir(item_path):
                    shutil.rmtree(item_path)
            except Exception:
                pass
    else:
        os.makedirs(UPLOADS_DIR, exist_ok=True)

    if os.path.exists(LIVE_STATE_FILE):
        try:
            os.remove(LIVE_STATE_FILE)
        except OSError:
            pass


def sanitize_filename(name):
    clean = re.sub(r"[^a-zA-Z0-9._-]", "_", os.path.basename(name))
    clean = clean.strip("_")
    return clean if clean else "archivo.dat"


def infer_priority_and_bursts(size_bytes):
    """
    Calcula automáticamente prioridad (1-5), valor nice y ráfagas de CPU y E/S según el peso real.
    Archivos livianos (< 100 KB) -> Prioridad 1 (nice 0, más interactivos).
    Archivos medianos (100 KB - 1 MB) -> Prioridad 2 (nice 4).
    Archivos pesados (1 MB - 5 MB) -> Prioridad 3 (nice 8).
    Archivos muy pesados (5 MB - 20 MB) -> Prioridad 4 (nice 12).
    Archivos masivos (> 20 MB) -> Prioridad 5 (nice 16, batch).
    """
    kb = max(1, size_bytes // 1024)
    if kb < 100:
        return 1, 0, "C2,I2,C2"
    elif kb < 1024:
        return 2, 4, "C4,I3,C3"
    elif kb < 5120:
        return 3, 8, "C6,I4,C4"
    elif kb < 20480:
        return 4, 12, "C10,I5,C6"
    else:
        return 5, 16, "C15,I6,C10"


def get_real_ps_outputs():
    """Ejecuta exclusivamente 'ps -C simulador --forest' en el kernel Linux."""
    try:
        ps_forest_proc = subprocess.run(
            ["ps", "-C", "simulador", "-o", "pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args", "--forest"],
            capture_output=True,
            text=True,
            timeout=2
        )
        ps_forest = ps_forest_proc.stdout.strip()
        running_in_os = (ps_forest_proc.returncode == 0 and len(ps_forest.splitlines()) > 1)
    except Exception as e:
        ps_forest = f"Error ejecutando ps -C simulador: {e}"
        running_in_os = False

    return ps_forest, running_in_os


def stop_active_simulador():
    global ACTIVE_PROC
    if ACTIVE_PROC is not None:
        try:
            if ACTIVE_PROC.poll() is None:
                os.killpg(os.getpgid(ACTIVE_PROC.pid), signal.SIGTERM)
                ACTIVE_PROC.wait(timeout=2)
        except Exception:
            try:
                ACTIVE_PROC.kill()
            except Exception:
                pass
        ACTIVE_PROC = None


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class PosixFileServerHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DOCS_DIR, **kwargs)

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, format, *args):
        if "/api/live" in (args[0] if args else ""):
            return
        super().log_message(format, *args)

    def send_json(self, status_code, data):
        try:
            payload = json.dumps(data).encode("utf-8")
            self.send_response(status_code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        global ACTIVE_PROC, UPLOADED_FILES_META
        parsed = urlparse(self.path)

        if parsed.path == "/api/clear":
            stop_active_simulador()
            clean_uploads_dir()
            self.send_json(200, {"cleared": True})
            return

        if parsed.path == "/api/upload":
            try:
                content_len = int(self.headers.get("Content-Length", "0"))
                raw_body = self.rfile.read(content_len)
                body = json.loads(raw_body.decode("utf-8"))

                files_list = body.get("files", [])
                algo = body.get("algo", "rr")
                quantum = int(body.get("quantum", 4))
                cs_cost = int(body.get("cs", 1))
                aging_th = int(body.get("aging", 5))
                tick_ms = int(body.get("tick_ms", 1000))

                if not files_list:
                    self.send_json(400, {"error": "No se recibieron archivos para procesar."})
                    return

                stop_active_simulador()
                clean_uploads_dir()

                UPLOADED_FILES_META = {}
                csv_lines = [
                    "# Cola de Archivos Reales Subidos al Servidor Linux",
                    "PID,Nombre,Llegada,Prioridad,Rafagas"
                ]

                for idx, fitem in enumerate(files_list, start=1):
                    fname = sanitize_filename(fitem.get("name", f"archivo_{idx}.dat"))
                    declared_size = max(1, int(fitem.get("size", 1024)))
                    b64_data = fitem.get("content_b64", "")

                    auto_prio, auto_nice, auto_bursts = infer_priority_and_bursts(declared_size)

                    # Si el usuario especificó prioridad explícita la respeta, de lo contrario usa la inferida por peso
                    prio = int(fitem.get("priority", auto_prio)) if fitem.get("priority") else auto_prio
                    prio = max(1, min(5, prio))
                    arrival = max(0, int(fitem.get("arrival", idx - 1)))
                    bursts_str = fitem.get("bursts") or auto_bursts

                    disk_path = os.path.join(UPLOADS_DIR, fname)
                    raw_bytes = None
                    if b64_data:
                        try:
                            padded = b64_data + "=" * (-len(b64_data) % 4)
                            raw_bytes = base64.b64decode(padded)
                        except Exception:
                            raw_bytes = None

                    if not raw_bytes:
                        header = f"DATA_FILE={fname}\nSIZE_ESTIMATED={declared_size}\nTIMESTAMP={time.time()}\n".encode("utf-8")
                        repeats = max(1, min(declared_size, 256 * 1024) // len(header))
                        raw_bytes = header * repeats

                    with open(disk_path, "wb") as wf:
                        wf.write(raw_bytes)

                    real_size = os.path.getsize(disk_path)
                    sha256_hex = hashlib.sha256(raw_bytes).hexdigest()

                    UPLOADED_FILES_META[idx] = {
                        "pid": idx,
                        "filename": fname,
                        "disk_path": f"uploads/{fname}",
                        "size_bytes": declared_size,
                        "disk_bytes": real_size,
                        "sha256": sha256_hex[:16] + "...",
                        "sha256_full": sha256_hex,
                        "priority": prio,
                        "arrival": arrival,
                        "bursts": bursts_str
                    }

                    csv_lines.append(f"{idx},{fname},{arrival},{prio},{bursts_str}")

                scenario_csv_path = os.path.join(UPLOADS_DIR, "cola_archivos_actual.csv")
                with open(scenario_csv_path, "w", encoding="utf-8") as cf:
                    cf.write("\n".join(csv_lines) + "\n")

                cmd = [
                    SIM_BIN,
                    "--live",
                    "--scenario", scenario_csv_path,
                    "--algo", algo,
                    "--quantum", str(quantum),
                    "--cs-cost", str(cs_cost),
                    "--aging-th", str(aging_th),
                    "--tick-ms", str(tick_ms),
                    "--live-state", LIVE_STATE_FILE
                ]

                ACTIVE_PROC = subprocess.Popen(
                    cmd,
                    cwd=PROJECT_DIR,
                    preexec_fn=os.setsid
                )

                self.send_json(200, {
                    "started": True,
                    "scheduler_pid": ACTIVE_PROC.pid,
                    "files_count": len(files_list),
                    "uploaded_files": UPLOADED_FILES_META,
                    "cmd": " ".join(cmd)
                })
            except Exception as e:
                self.send_json(500, {"error": str(e)})
            return

        self.send_json(404, {"error": "Ruta POST no encontrada"})

    def do_GET(self):
        global ACTIVE_PROC
        parsed = urlparse(self.path)

        if parsed.path == "/api/stop":
            stop_active_simulador()
            self.send_json(200, {"stopped": True})
            return

        if parsed.path == "/api/live":
            if ACTIVE_PROC is not None:
                ACTIVE_PROC.poll()
            ps_forest, running_in_os = get_real_ps_outputs()
            sim_state = None
            if os.path.exists(LIVE_STATE_FILE):
                try:
                    with open(LIVE_STATE_FILE, "r", encoding="utf-8") as f:
                        sim_state = json.load(f)
                except Exception:
                    sim_state = None

            self.send_json(200, {
                "running_in_os": running_in_os,
                "ps_forest": ps_forest,
                "sim_state": sim_state,
                "uploaded_files": UPLOADED_FILES_META
            })
            return

        if parsed.path == "/" or parsed.path == "":
            self.path = "/presentacion_sustentacion.html"
        return super().do_GET()


def main():
    import sys
    port = 8080
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass

    clean_uploads_dir()

    server = ReusableThreadingHTTPServer(("0.0.0.0", port), PosixFileServerHandler)
    print("=" * 80)
    print(f"  SERVIDOR DE PROCESAMIENTO DE ARCHIVOS INICIADO EN: http://localhost:{port}")
    print(f"  Directorio limpio: {UPLOADS_DIR} (0 archivos al inicio)")
    print(f"  Binario C++17 : {SIM_BIN}")
    print("  Monitorea en tu terminal con:")
    print("    make monitor")
    print("    watch -n 0.5 \"ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest || true\"")
    print("  Presiona Ctrl+C para detener.")
    print("=" * 80)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        stop_active_simulador()
        print("\nServidor detenido.")


if __name__ == "__main__":
    main()
