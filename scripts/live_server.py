#!/usr/bin/env python3
"""
scripts/live_server.py
Servidor HTTP en tiempo real conectado al binario POSIX ./simulador y al comando 'ps' de Linux.
Permite:
1. Iniciar el proceso real ./simulador en segundo plano con ticks de 1 segundo (--live --tick-ms 1000)
   para que el árbol de procesos hijos (R+, S+, T+, Z+ <defunct>) exista durante 30-45 segundos
   y sea observable tanto desde cualquier terminal Linux (watch ps) como desde la interfaz web.
2. Consultar en vivo (/api/live) la salida real de:
   - ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest
   - ps aux --sort=-%cpu | head -12
   junto con el estado instantáneo del planificador (/tmp/simulador_live_state.json).
"""

import os
import json
import signal
import subprocess
import tempfile
from urllib.parse import urlparse, parse_qs
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")
SIM_BIN = os.path.join(PROJECT_DIR, "simulador")
LIVE_STATE_FILE = "/tmp/simulador_live_state.json"

ACTIVE_PROC = None


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


class PosixSimulatorHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DOCS_DIR, **kwargs)

    def log_message(self, format, *args):
        # Evitar inundar la terminal con logs de polling de /api/live cada 500ms
        if "/api/live" in (args[0] if args else ""):
            return
        super().log_message(format, *args)

    def send_json(self, status_code, data):
        payload = json.dumps(data).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        global ACTIVE_PROC
        parsed = urlparse(self.path)

        if parsed.path == "/api/start":
            params = parse_qs(parsed.query)
            scen_id = params.get("scenario", ["2"])[0]
            algo = params.get("algo", ["rr"])[0]
            quantum = params.get("quantum", ["4"])[0]
            cs_cost = params.get("cs", ["1"])[0]
            aging_th = params.get("aging", ["5"])[0]
            tick_ms = params.get("tick_ms", ["1000"])[0]

            scen_map = {
                "1": "scenarios/escenario1_convoy.csv",
                "2": "scenarios/escenario2_io_bursts.csv",
                "3": "scenarios/escenario3_starvation.csv",
            }
            scen_path = os.path.join(PROJECT_DIR, scen_map.get(scen_id, "scenarios/escenario2_io_bursts.csv"))

            stop_active_simulador()
            if os.path.exists(LIVE_STATE_FILE):
                try:
                    os.remove(LIVE_STATE_FILE)
                except OSError:
                    pass

            cmd = [
                SIM_BIN,
                "--live",
                "--scenario", scen_path,
                "--algo", algo,
                "--quantum", str(quantum),
                "--cs-cost", str(cs_cost),
                "--aging-th", str(aging_th),
                "--tick-ms", str(tick_ms),
                "--live-state", LIVE_STATE_FILE
            ]

            try:
                ACTIVE_PROC = subprocess.Popen(
                    cmd,
                    cwd=PROJECT_DIR,
                    preexec_fn=os.setsid
                )
                self.send_json(200, {
                    "started": True,
                    "pid": ACTIVE_PROC.pid,
                    "cmd": " ".join(cmd)
                })
            except Exception as e:
                self.send_json(500, {"error": str(e)})
            return

        if parsed.path == "/api/stop":
            stop_active_simulador()
            self.send_json(200, {"stopped": True})
            return

        if parsed.path == "/api/live":
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
                "sim_state": sim_state
            })
            return

        if parsed.path == "/api/run":
            params = parse_qs(parsed.query)
            scen_id = params.get("scenario", ["1"])[0]
            algo = params.get("algo", ["rr"])[0]
            quantum = params.get("quantum", ["4"])[0]
            cs_cost = params.get("cs", ["1"])[0]
            aging_th = params.get("aging", ["5"])[0]
            tick_ms = params.get("tick_ms", ["3"])[0]

            scen_map = {
                "1": "scenarios/escenario1_convoy.csv",
                "2": "scenarios/escenario2_io_bursts.csv",
                "3": "scenarios/escenario3_starvation.csv",
            }
            scen_path = os.path.join(PROJECT_DIR, scen_map.get(scen_id, "scenarios/escenario1_convoy.csv"))

            with tempfile.NamedTemporaryFile(mode="w+", suffix=".json", delete=False) as tmp:
                tmp_json = tmp.name

            try:
                cmd = [
                    SIM_BIN,
                    "--scenario", scen_path,
                    "--algo", algo,
                    "--quantum", str(quantum),
                    "--cs-cost", str(cs_cost),
                    "--aging-th", str(aging_th),
                    "--tick-ms", str(tick_ms),
                    "--export-json", tmp_json
                ]
                proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
                with open(tmp_json, "r", encoding="utf-8") as f:
                    trace_data = json.load(f)
                trace_data["console_stdout"] = proc.stdout
                self.send_json(200, trace_data)
            except Exception as e:
                self.send_json(500, {"error": str(e)})
            finally:
                if os.path.exists(tmp_json):
                    os.remove(tmp_json)
            return

        if parsed.path == "/" or parsed.path == "":
            self.path = "/presentacion_sustentacion.html"
        return super().do_GET()


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


def main():
    import sys
    port = 8080
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
    server = ReusableThreadingHTTPServer(("0.0.0.0", port), PosixSimulatorHandler)
    print("=" * 80)
    print(f"  SERVIDOR POSIX EN VIVO ACTIVO EN: http://localhost:{port}")
    print(f"  Binario C++17 conectado: {SIM_BIN}")
    print("  Cuando inicies la simulación en la web (o con './simulador --live' en consola),")
    print("  también podrás ver los procesos reales en otra terminal con:")
    print("    make monitor")
    print("    watch -n 0.5 \"ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest || true\"")
    print("    watch -n 1 \"ps aux --sort=-%cpu | head -15\"")
    print("  Presiona Ctrl+C para detener.")
    print("=" * 80)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        stop_active_simulador()
        print("\nServidor detenido.")


if __name__ == "__main__":
    main()
