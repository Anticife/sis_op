#!/usr/bin/env python3
"""
scripts/live_server.py
Servidor HTTP interactivo conectado al binario POSIX ./simulador en Linux.
Permite que docs/presentacion_sustentacion.html invoque ejecuciones reales del kernel
mediante llamadas al sistema POSIX (fork, kill SIGSTOP/SIGCONT, pipe, read/write, wait4, /proc)
y reciba la telemetría real en formato JSON.
"""

import os
import json
import subprocess
import tempfile
from urllib.parse import urlparse, parse_qs
from http.server import SimpleHTTPRequestHandler, HTTPServer

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")
SIM_BIN = os.path.join(PROJECT_DIR, "simulador")


class PosixSimulatorHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DOCS_DIR, **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
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

                payload = json.dumps(trace_data).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
            except Exception as e:
                err_payload = json.dumps({"error": str(e)}).encode("utf-8")
                self.send_response(500)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(err_payload)
            finally:
                if os.path.exists(tmp_json):
                    os.remove(tmp_json)
            return

        if parsed.path == "/" or parsed.path == "":
            self.path = "/presentacion_sustentacion.html"
        return super().do_GET()


def main():
    port = 8080
    server = HTTPServer(("0.0.0.0", port), PosixSimulatorHandler)
    print("=" * 75)
    print(f"  SERVIDOR POSIX EN VIVO ACTIVO EN: http://localhost:{port}")
    print(f"  Conectado al binario C++17: {SIM_BIN}")
    print("  Presiona Ctrl+C para detener.")
    print("=" * 75)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServidor detenido.")


if __name__ == "__main__":
    main()
