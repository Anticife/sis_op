#!/usr/bin/env python3
"""
scripts/live_server.py
Servidor Web de Subida y Procesamiento de Archivos en Linux conectado al Planificador POSIX (./simulador).
- Guarda archivos reales subidos desde el navegador en /mnt/d/sis_op/uploads/
- Calcula su hash SHA-256 real y genera la cola de trabajo para ./simulador
- Lanza ./simulador --live creando un proceso hijo real (fork) por cada archivo subido
- Expone en /api/live el progreso por archivo y la salida real de 'ps -C simulador --forest'
"""

import os
import re
import time
import json
import base64
import signal
import hashlib
import subprocess
import tempfile
from urllib.parse import urlparse, parse_qs
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")
UPLOADS_DIR = os.path.join(PROJECT_DIR, "uploads")
SIM_BIN = os.path.join(PROJECT_DIR, "simulador")
LIVE_STATE_FILE = "/tmp/simulador_live_state.json"

os.makedirs(UPLOADS_DIR, exist_ok=True)

ACTIVE_PROC = None
UPLOADED_FILES_META = {}


def sanitize_filename(name):
    clean = re.sub(r"[^a-zA-Z0-9._-]", "_", os.path.basename(name))
    clean = clean.strip("_")
    return clean if clean else "archivo.dat"


def get_real_ps_outputs():
    """Ejecuta los comandos reales 'ps' en el kernel Linux y devuelve su salida de consola."""
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

    try:
        ps_top_proc = subprocess.run(
            "ps aux --sort=-%cpu | head -12",
            shell=True,
            capture_output=True,
            text=True,
            timeout=2
        )
        ps_top = ps_top_proc.stdout.strip()
    except Exception as e:
        ps_top = f"Error ejecutando ps aux: {e}"

    return ps_forest, ps_top, running_in_os


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


def infer_bursts_from_size(size_bytes, custom_bursts=None):
    """Calcula las ráfagas de CPU y E/S (lectura de disco/pipe) según el tamaño del archivo."""
    if custom_bursts:
        return custom_bursts
    kb = max(1, size_bytes // 1024)
    if kb < 50:
        return "C2,I3,C2"
    elif kb < 500:
        return "C3,I5,C3"
    elif kb < 2048:
        return "C5,I4,C4"
    else:
        return "C8,I5,C6"


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class PosixFileServerHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DOCS_DIR, **kwargs)

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (BrokenPipeError, ConnectionResetError):
            # Ignorar desconexiones prematuras del navegador sin ensuciar la consola
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
                if os.path.exists(LIVE_STATE_FILE):
                    try:
                        os.remove(LIVE_STATE_FILE)
                    except OSError:
                        pass

                UPLOADED_FILES_META = {}
                csv_lines = [
                    "# Cola de Archivos Reales Subidos al Servidor Linux",
                    "PID,Nombre,Llegada,Prioridad,Rafagas"
                ]

                for idx, fitem in enumerate(files_list, start=1):
                    fname = sanitize_filename(fitem.get("name", f"archivo_{idx}.dat"))
                    prio = max(1, min(5, int(fitem.get("priority", 2))))
                    arrival = max(0, int(fitem.get("arrival", idx - 1)))
                    b64_data = fitem.get("content_b64", "")

                    disk_path = os.path.join(UPLOADS_DIR, fname)
                    if b64_data:
                        raw_bytes = base64.b64decode(b64_data)
                    else:
                        # Si es un archivo de prueba o lote rápido, generar contenido binario/texto real en disco
                        sim_size = int(fitem.get("size", 65536))
                        header = f"ARCHIVO_REAL_SERVIDOR_POSIX={fname}\nTIMESTAMP={time.time()}\n".encode("utf-8")
                        repeats = max(1, min(sim_size, 512 * 1024) // len(header))
                        raw_bytes = header * repeats

                    with open(disk_path, "wb") as wf:
                        wf.write(raw_bytes)

                    real_size = os.path.getsize(disk_path)
                    declared_size = int(fitem.get("size", real_size))
                    sha256_hex = hashlib.sha256(raw_bytes).hexdigest()
                    bursts_str = infer_bursts_from_size(declared_size, fitem.get("bursts"))

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
            ps_forest, ps_top, running_in_os = get_real_ps_outputs()
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
                "ps_top": ps_top,
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
    server = ReusableThreadingHTTPServer(("0.0.0.0", port), PosixFileServerHandler)
    print("=" * 80)
    print(f"  SERVIDOR DE SUBIDA Y PROCESAMIENTO DE ARCHIVOS ACTIVO EN: http://localhost:{port}")
    print(f"  Directorio de archivos reales: {UPLOADS_DIR}")
    print(f"  Motor de Planificación C++17 : {SIM_BIN}")
    print("  Para monitorear los procesos reales en otra terminal al subir archivos:")
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
