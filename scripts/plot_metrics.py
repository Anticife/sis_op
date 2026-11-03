#!/usr/bin/env python3
"""
scripts/plot_metrics.py
Generador gráfico de métricas y diagramas de Gantt para el proyecto de
Gestión de Procesos y Planificación de CPU (Sistemas Operativos).

Lectura 100% DINÁMICA desde los archivos CSV generados por el simulador:
  - docs/resultados_escenario1.csv
  - docs/resultados_escenario2.csv
  - docs/resultados_escenario3.csv
  - scenarios/escenario1_convoy.csv

Genera 4 gráficas de alta resolución (300 DPI) en docs/graficas/:
  1. escenario1_comparativa.png
  2. escenario1_gantt.png (Leído dinámicamente de la sección GANTT del CSV)
  3. escenario3_starvation_vs_aging.png (Leído dinámicamente del CSV)
  4. round_robin_sensibilidad_quantum.png (Simulado dinámicamente desde el CSV del escenario)
"""

import os
import sys
import csv
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.ticker import MultipleLocator
import numpy as np

# Configurar directorios base
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")
SCENARIOS_DIR = os.path.join(PROJECT_DIR, "scenarios")
OUTPUT_DIR = os.path.join(DOCS_DIR, "graficas")
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Configuración global de estilo
plt.rcParams.update({
    'font.sans-serif': ['DejaVu Sans', 'Liberation Sans', 'Arial', 'Helvetica'],
    'font.family': 'sans-serif',
    'axes.edgecolor': '#333333',
    'axes.linewidth': 0.8,
    'grid.color': '#e0e0e0',
    'grid.linestyle': '--',
    'grid.alpha': 0.7,
    'figure.titlesize': 14,
    'figure.titleweight': 'bold'
})

# Paleta armónica dinámica para procesos
COLOR_PALETTE = [
    "#2b5b84",  # Azul petróleo
    "#27ae60",  # Verde esmeralda
    "#2980b9",  # Celeste azul
    "#8e44ad",  # Morado amatista
    "#e67e22",  # Naranja
    "#16a085",  # Turquesa
    "#d35400",  # Óxido
    "#7f8c8d"   # Gris medio
]


def load_scenario_processes(csv_path):
    """Carga y parsea dinámicamente un archivo de escenario CSV en scenarios/."""
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"Archivo de escenario no encontrado: {csv_path}")

    processes = []
    with open(csv_path, mode="r", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or row[0].startswith('#') or row[0].lower() == 'pid':
                continue
            # PID, Nombre, Llegada, Prioridad, Rafagas...
            pid = int(row[0].strip())
            name = row[1].strip()
            arrival = int(row[2].strip())
            priority = int(row[3].strip())
            bursts = []
            for b in row[4:]:
                b = b.strip()
                if not b:
                    continue
                if b[0].upper() == 'C':
                    bursts.append(('CPU', int(b[1:])))
                elif b[0].upper() == 'I':
                    bursts.append(('IO', int(b[1:])))
                else:
                    bursts.append(('CPU', int(b)))

            total_cpu = sum(dur for btype, dur in bursts if btype == 'CPU')
            total_io = sum(dur for btype, dur in bursts if btype == 'IO')
            processes.append({
                'pid': pid,
                'name': name,
                'arrival': arrival,
                'priority': priority,
                'bursts': bursts,
                'total_cpu': total_cpu,
                'total_io': total_io
            })
    return processes


def parse_csv_summary(csv_path):
    """Lee dinámicamente la sección '--- RESUMEN GLOBAL DE METRICAS ---' de un CSV exportado."""
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"Archivo de métricas no encontrado: {csv_path}. Ejecuta 'make test' primero.")

    summary_data = []
    in_summary = False
    with open(csv_path, mode="r", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            if "--- RESUMEN GLOBAL DE METRICAS ---" in row[0]:
                in_summary = True
                continue
            if in_summary:
                if "---" in row[0]:
                    break
                if row[0] == "Algoritmo":
                    continue
                if len(row) >= 10:
                    try:
                        record = {
                            "Algoritmo": row[0],
                            "TiempoTotal": float(row[1]),
                            "CPU_Utilizacion_Pct": float(row[2]),
                            "CPU_Overhead_Pct": float(row[3]),
                            "Throughput": float(row[4]),
                            "Promedio_Retorno": float(row[5]),
                            "Promedio_Espera": float(row[6]),
                            "Promedio_Respuesta": float(row[7]),
                            "Total_Cambios_Contexto": int(float(row[8])),
                            "Indice_Jain": float(row[9])
                        }
                        summary_data.append(record)
                    except ValueError:
                        pass
    return summary_data


def parse_csv_processes(csv_path):
    """Lee dinámicamente la sección de procesos de un CSV exportado."""
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"Archivo de métricas no encontrado: {csv_path}. Ejecuta 'make test' primero.")

    processes = []
    with open(csv_path, mode="r", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = None
        for row in reader:
            if not row:
                continue
            if "---" in row[0]:
                break
            if header is None:
                header = row
                continue
            if len(row) >= 14:
                try:
                    processes.append({
                        "Algoritmo": row[0],
                        "PID": int(row[1]),
                        "Nombre": row[2],
                        "Llegada": int(row[3]),
                        "PrioridadBase": int(row[4]),
                        "PrioridadDinamica": int(row[5]),
                        "TotalCPU": int(row[6]),
                        "TotalIO": int(row[7]),
                        "Inicio": int(row[8]),
                        "Fin": int(row[9]),
                        "Retorno": int(row[10]),
                        "Espera": int(row[11]),
                        "Respuesta": int(row[12]),
                        "CambiosContexto": int(row[13])
                    })
                except ValueError:
                    pass
    return processes


def parse_csv_gantt(csv_path):
    """
    Lee dinámicamente la sección '--- DIAGRAMA DE GANTT ---' del CSV exportado por el simulador.
    Retorna un diccionario ordenado { algoritmo: [(inicio, fin, pid, etiqueta), ...] }
    y un mapeo de etiquetas { pid: nombre }.
    """
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"Archivo de métricas no encontrado: {csv_path}. Ejecuta 'make test' primero.")

    gantt_by_algo = {}
    pid_labels = {}
    in_gantt = False

    with open(csv_path, mode="r", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            if "--- DIAGRAMA DE GANTT ---" in row[0]:
                in_gantt = True
                continue
            if in_gantt:
                if row[0] == "Algoritmo":
                    continue
                if len(row) >= 5:
                    algo = row[0].strip()
                    try:
                        start = int(row[1])
                        end = int(row[2])
                        pid = int(row[3])
                        label = row[4].strip()
                        os_pid = int(row[5]) if len(row) >= 6 else 0
                        if algo not in gantt_by_algo:
                            gantt_by_algo[algo] = []
                        gantt_by_algo[algo].append((start, end, pid, label, os_pid))
                        if pid > 0 and pid not in pid_labels:
                            pid_labels[pid] = f"{label} (OS_PID:{os_pid})" if os_pid > 0 else label
                    except ValueError:
                        pass

    return gantt_by_algo, pid_labels


# -----------------------------------------------------------------------------
# 1. Gráfica Comparativa de Tiempos: Escenario 1 (Efecto Convoy)
# -----------------------------------------------------------------------------
def plot_escenario1_comparativa():
    csv_path = os.path.join(DOCS_DIR, "resultados_escenario1.csv")
    data = parse_csv_summary(csv_path)
    if not data:
        raise ValueError(f"No se pudieron leer datos válidos de {csv_path}")

    labels = []
    t_retorno = []
    t_espera = []

    for d in data:
        algo_name = d["Algoritmo"]
        if "FCFS" in algo_name:
            labels.append("FCFS\n(Convoy)")
        elif "Round Robin" in algo_name:
            labels.append("Round Robin\n(q=4)")
        elif "SRTF" in algo_name:
            labels.append("SRTF\n(Apropiativo)")
        elif "Aging" in algo_name:
            labels.append("Prioridad\ncon Aging")
        elif "Estricta" in algo_name:
            labels.append("Prioridad\nEstricta")
        else:
            labels.append(algo_name[:15])
        t_retorno.append(d["Promedio_Retorno"])
        t_espera.append(d["Promedio_Espera"])

    x = np.arange(len(labels))
    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 6), dpi=300)

    color_t = "#1f77b4"  # Azul profesional
    color_w = "#ff7f0e"  # Naranja ámbar

    rects1 = ax.bar(x - width/2, t_retorno, width, label='Tiempo de Retorno Promedio (T)', color=color_t, edgecolor='#0d47a1', alpha=0.9, zorder=3)
    rects2 = ax.bar(x + width/2, t_espera, width, label='Tiempo de Espera Promedio (W)', color=color_w, edgecolor='#d35400', alpha=0.9, zorder=3)

    # Anotaciones numéricas dinámicas
    for bar in rects1:
        yval = bar.get_height()
        ax.annotate(f"{yval:.1f}",
                    xy=(bar.get_x() + bar.get_width() / 2, yval),
                    xytext=(0, 4), textcoords="offset points",
                    ha='center', va='bottom', fontsize=9, fontweight='bold', color='#1f77b4')

    for bar in rects2:
        yval = bar.get_height()
        ax.annotate(f"{yval:.1f}",
                    xy=(bar.get_x() + bar.get_width() / 2, yval),
                    xytext=(0, 4), textcoords="offset points",
                    ha='center', va='bottom', fontsize=9, fontweight='bold', color='#d35400')

    # Anotaciones analíticas inferidas de los datos
    fcfs_idx = 0
    srtf_idx = 2 if len(t_espera) > 2 else 1
    w_fcfs = t_espera[fcfs_idx]
    w_srtf = t_espera[srtf_idx]
    pct_reduction = ((w_fcfs - w_srtf) / w_fcfs) * 100.0 if w_fcfs > 0 else 0

    ax.annotate(f'Efecto Convoy:\nEspera disparada a {w_fcfs:.1f} ticks\n(Proceso Batch acapara CPU)',
                xy=(fcfs_idx + width/2, w_fcfs), xytext=(0.55, w_fcfs + 2),
                arrowprops=dict(arrowstyle="->", color='#c0392b', lw=1.5),
                fontsize=9, bbox=dict(boxstyle="round,pad=0.3", fc="#fce4ec", ec="#c0392b", lw=1))

    ax.annotate(f'Óptimo en Espera:\nSRTF reduce W en {pct_reduction:.1f}%\n({w_srtf:.1f} ticks)',
                xy=(srtf_idx + width/2, w_srtf), xytext=(srtf_idx - 0.2, w_srtf + 14),
                arrowprops=dict(arrowstyle="->", color='#27ae60', lw=1.5),
                fontsize=9, bbox=dict(boxstyle="round,pad=0.3", fc="#e8f8f5", ec="#27ae60", lw=1))

    ax.set_ylabel('Ticks de Reloj (CPU)', fontsize=11, fontweight='bold')
    ax.set_title('Escenario 1: Comparativa de Rendimiento y Efecto Convoy\nTiempo de Retorno (T) vs. Tiempo de Espera (W)', fontsize=12, pad=15)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=10)
    ax.legend(loc='upper right', frameon=True, facecolor='white', framealpha=0.9)
    ax.set_ylim(0, max(max(t_retorno), max(t_espera)) * 1.2)
    ax.grid(axis='y', zorder=0)

    plt.tight_layout()
    out_file = os.path.join(OUTPUT_DIR, "escenario1_comparativa.png")
    plt.savefig(out_file, dpi=300)
    plt.close()
    print(f"[OK] Gráfica generada dinámicamente: {out_file}")


# -----------------------------------------------------------------------------
# 2. Diagrama de Gantt Visual de Alta Resolución: Escenario 1 (100% DINÁMICO)
# -----------------------------------------------------------------------------
def plot_escenario1_gantt():
    csv_path = os.path.join(DOCS_DIR, "resultados_escenario1.csv")
    gantt_by_algo, pid_labels = parse_csv_gantt(csv_path)

    if not gantt_by_algo:
        raise ValueError(f"No se encontró la sección '--- DIAGRAMA DE GANTT ---' en {csv_path}. Ejecuta 'make test'.")

    # Mapeo dinámico de colores por PID detectado en el CSV
    colors = {-2: "#e74c3c", -1: "#95a5a6"}  # -2 = Context Switch, -1 = IDLE
    pids_found = sorted([pid for pid in pid_labels.keys() if pid > 0])
    for idx, pid in enumerate(pids_found):
        colors[pid] = COLOR_PALETTE[idx % len(COLOR_PALETTE)]

    # Algoritmos a graficar ordenados
    algo_order = []
    for full_name in gantt_by_algo.keys():
        short_name = full_name
        if "FCFS" in full_name:
            short_name = "FCFS (No Apropiativo)"
        elif "Round Robin" in full_name:
            short_name = "Round Robin (q=4)"
        elif "SRTF" in full_name:
            short_name = "SRTF (Apropiativo)"
        elif "Aging" in full_name:
            short_name = "Prioridad + Aging"
        elif "Estricta" in full_name:
            short_name = "Prioridad Estricta"
        algo_order.append((short_name, gantt_by_algo[full_name]))

    # Invertir para que FCFS quede abajo y Aging arriba
    algo_order.reverse()

    fig, ax = plt.subplots(figsize=(14, 6), dpi=300)
    bar_height = 0.55
    y_positions = np.arange(len(algo_order))
    max_time = 0

    for y_idx, (algo_name, segments) in enumerate(algo_order):
        for seg in segments:
            start, end, pid, label = seg[0], seg[1], seg[2], seg[3]
            dur = end - start
            if end > max_time:
                max_time = end

            col = colors.get(pid, "#bdc3c7")
            hatch = "//" if pid == -2 else None
            edgecol = "#922b21" if pid == -2 else "white"

            ax.barh(y_idx, dur, left=start, height=bar_height,
                    color=col, edgecolor=edgecol, linewidth=0.8,
                    hatch=hatch, zorder=3)

            # Etiqueta interna si el bloque tiene ancho suficiente
            if dur >= 2:
                display_label = f"P{pid}" if pid > 0 else "CS"
                ax.text(start + dur / 2, y_idx, display_label,
                        ha='center', va='center', color='white',
                        fontsize=7.5, fontweight='bold', zorder=4)

    ax.set_yticks(y_positions)
    ax.set_yticklabels([name for name, _ in algo_order], fontsize=10, fontweight='bold')
    ax.set_xlabel('Línea de Tiempo del Reloj de la CPU (ticks)', fontsize=11, fontweight='bold')
    ax.set_xlim(-1, max_time + 3)
    ax.xaxis.set_major_locator(MultipleLocator(5))
    ax.xaxis.set_minor_locator(MultipleLocator(1))
    ax.grid(axis='x', which='major', linestyle='--', alpha=0.7, zorder=0)
    ax.grid(axis='x', which='minor', linestyle=':', alpha=0.3, zorder=0)

    ax.set_title('Diagrama de Gantt Comparativo - Escenario 1 (Efecto Convoy)\nInferencia Dinámica desde Resultados del Simulador C++',
                 fontsize=13, pad=14)

    # Leyenda generada 100% dinámicamente según los procesos detectados
    legend_patches = []
    for pid in pids_found:
        proc_name = pid_labels.get(pid, f"Proceso {pid}")
        legend_patches.append(mpatches.Patch(facecolor=colors[pid], label=f"P{pid}: {proc_name}"))
    legend_patches.append(mpatches.Patch(facecolor=colors[-2], hatch='//', label='Cambio de Contexto (CS = 1 tick)'))

    ax.legend(handles=legend_patches, loc='upper center', bbox_to_anchor=(0.5, -0.15),
              ncol=min(len(legend_patches), 3), frameon=True, fontsize=9)

    plt.tight_layout()
    out_file = os.path.join(OUTPUT_DIR, "escenario1_gantt.png")
    plt.savefig(out_file, dpi=300)
    plt.close()
    print(f"[OK] Diagrama de Gantt dinámico generado: {out_file}")


# -----------------------------------------------------------------------------
# 3. Gráfica de Inanición vs. Envejecimiento (Aging): Escenario 3 (100% DINÁMICO)
# -----------------------------------------------------------------------------
def plot_escenario3_starvation_vs_aging():
    csv_path = os.path.join(DOCS_DIR, "resultados_escenario3.csv")
    procs = parse_csv_processes(csv_path)

    strict_procs = [p for p in procs if "Sin Aging" in p["Algoritmo"] or "Estricta" in p["Algoritmo"]]
    aging_procs = [p for p in procs if ("Aging" in p["Algoritmo"] or "Envejecimiento" in p["Algoritmo"]) and "Sin" not in p["Algoritmo"]]

    if not strict_procs or not aging_procs:
        raise ValueError(f"No se encontraron datos de Prioridad Estricta y Aging en {csv_path}. Ejecuta 'make test'.")

    # Mapeo ordenado de nombres
    p_names = [f"P{p['PID']}: {p['Nombre'].replace('Urgente_Alarma_', 'Urg').replace('Tarea_Fondo_Batch', 'Fondo')}" for p in strict_procs]
    w_strict = [p["Espera"] for p in strict_procs]
    t_strict = [p["Retorno"] for p in strict_procs]
    w_aging = [p["Espera"] for p in aging_procs]
    t_aging = [p["Retorno"] for p in aging_procs]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5.5), dpi=300)
    x = np.arange(len(p_names))
    width = 0.38

    # Panel 1: Tiempo de Espera (W)
    ax1.bar(x - width/2, w_strict, width, label='Prioridad Estricta (Sin Aging)', color='#c0392b', alpha=0.85, edgecolor='#922b21')
    ax1.bar(x + width/2, w_aging, width, label='Prioridad con Aging (th=5)', color='#27ae60', alpha=0.85, edgecolor='#1e8449')

    # Anotaciones dinámicas sobre P1
    p1_w_strict = w_strict[0]
    p1_w_aging = w_aging[0]
    ax1.annotate(f'Inanición severa:\nEspera = {p1_w_strict} ticks\n(Condenado al final)',
                 xy=(0 - width/2, p1_w_strict), xytext=(0.5, p1_w_strict - 3),
                 arrowprops=dict(arrowstyle="->", color='#c0392b', lw=1.2),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.2", fc="#fadbd8", ec="#c0392b"))

    ax1.annotate(f'Rescatado por Aging:\nEspera = {p1_w_aging} ticks\n(Promoción dinámica)',
                 xy=(0 + width/2, p1_w_aging), xytext=(1.2, p1_w_aging - 7),
                 arrowprops=dict(arrowstyle="->", color='#27ae60', lw=1.2),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.2", fc="#d4efdf", ec="#27ae60"))

    ax1.set_ylabel('Tiempo de Espera W (ticks)', fontsize=10, fontweight='bold')
    ax1.set_title('Tiempo de Espera por Proceso (W)', fontsize=11, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(p_names, rotation=30, ha='right', fontsize=9)
    ax1.legend(loc='upper right', frameon=True, fontsize=8.5)
    ax1.grid(axis='y')

    # Panel 2: Tiempo de Retorno (T)
    ax2.bar(x - width/2, t_strict, width, label='Prioridad Estricta (Sin Aging)', color='#e74c3c', alpha=0.85, edgecolor='#c0392b')
    ax2.bar(x + width/2, t_aging, width, label='Prioridad con Aging (th=5)', color='#2ecc71', alpha=0.85, edgecolor='#27ae60')

    p1_t_strict = t_strict[0]
    p1_t_aging = t_aging[0]
    diff_t = p1_t_strict - p1_t_aging

    ax2.annotate(f'Culmina en Tick {p1_t_strict}\n(Último proceso)',
                 xy=(0 - width/2, p1_t_strict), xytext=(0.8, p1_t_strict - 4),
                 arrowprops=dict(arrowstyle="->", color='#c0392b', lw=1.2),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.2", fc="#fadbd8", ec="#c0392b"))

    ax2.annotate(f'Culmina en Tick {p1_t_aging}\n({diff_t} ticks antes gracias al Aging)',
                 xy=(0 + width/2, p1_t_aging), xytext=(1.3, p1_t_aging - 9),
                 arrowprops=dict(arrowstyle="->", color='#27ae60', lw=1.2),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.2", fc="#d4efdf", ec="#27ae60"))

    ax2.set_ylabel('Tiempo de Retorno T (ticks)', fontsize=10, fontweight='bold')
    ax2.set_title('Tiempo de Retorno por Proceso (T)', fontsize=11, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels(p_names, rotation=30, ha='right', fontsize=9)
    ax2.legend(loc='upper right', frameon=True, fontsize=8.5)
    ax2.grid(axis='y')

    fig.suptitle('Escenario 3: Fenómeno de Inanición vs. Solución por Envejecimiento (Aging)\nInferencia Dinámica desde Resultados del Simulador C++',
                 fontsize=13, y=1.02)
    plt.tight_layout()
    out_file = os.path.join(OUTPUT_DIR, "escenario3_starvation_vs_aging.png")
    plt.savefig(out_file, dpi=300, bbox_inches='tight')
    plt.close()
    print(f"[OK] Gráfica de Inanición vs Aging generada dinámicamente: {out_file}")


# -----------------------------------------------------------------------------
# 4. Curva de Sensibilidad del Quantum: Ejecución Real del Binario POSIX C++
# -----------------------------------------------------------------------------
def run_real_posix_rr_for_quantum(q, cs_penalty=1):
    """
    Ejecuta directamente el binario compilado ./simulador con llamadas al sistema POSIX reales
    para cada valor de quantum q in [1..15] y extrae las métricas reales del CSV generado.
    """
    import subprocess
    import tempfile

    sim_bin = os.path.join(PROJECT_DIR, "simulador")
    scenario_path = os.path.join(SCENARIOS_DIR, "escenario1_convoy.csv")

    with tempfile.NamedTemporaryFile(mode="w+", suffix=".csv", delete=False) as tmp:
        tmp_csv = tmp.name

    try:
        subprocess.run(
            [sim_bin, "--scenario", scenario_path, "--algo", "rr",
             "--quantum", str(q), "--cs-cost", str(cs_penalty),
             "--tick-ms", "1", "--export", tmp_csv],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True
        )
        summary = parse_csv_summary(tmp_csv)
        if summary and len(summary) > 0:
            row = summary[0]
            return (
                q,
                row["TiempoTotal"],
                row["Total_Cambios_Contexto"],
                row["Promedio_Espera"],
                row["CPU_Overhead_Pct"]
            )
    finally:
        if os.path.exists(tmp_csv):
            os.remove(tmp_csv)
        tmp_json = tmp_csv[:-4] + ".json"
        if os.path.exists(tmp_json):
            os.remove(tmp_json)

    raise RuntimeError(f"No se pudo ejecutar ./simulador para q={q}")


def plot_round_robin_sensibilidad_quantum():
    quantums = list(range(1, 16))
    w_prom = []
    cs_overheads = []
    cs_counts = []

    for q in quantums:
        _, _, cs, w, ov = run_real_posix_rr_for_quantum(q)
        w_prom.append(w)
        cs_overheads.append(ov)
        cs_counts.append(cs)

    fig, ax1 = plt.subplots(figsize=(10, 6), dpi=300)

    color_w = '#1f77b4'  # Azul
    color_cs = '#d62728' # Rojo

    ax1.set_xlabel('Valor del Quantum de CPU (q en ticks)', fontsize=11, fontweight='bold')
    ax1.set_ylabel('Tiempo de Espera Promedio W (ticks)', color=color_w, fontsize=11, fontweight='bold')
    line1 = ax1.plot(quantums, w_prom, marker='o', linewidth=2.2, color=color_w, label='Tiempo de Espera Promedio (W)')
    ax1.tick_params(axis='y', labelcolor=color_w)
    ax1.set_ylim(min(w_prom) * 0.7, max(w_prom) * 1.25)
    ax1.set_xticks(quantums)
    ax1.grid(True, linestyle='--', alpha=0.6)

    # Segundo eje Y para Sobrecarga de Cambio de Contexto
    ax2 = ax1.twinx()
    ax2.set_ylabel('Sobrecarga por Cambio de Contexto (%)', color=color_cs, fontsize=11, fontweight='bold')
    line2 = ax2.plot(quantums, cs_overheads, marker='s', linewidth=2.2, linestyle='--', color=color_cs, label='Sobrecarga CS Overhead (%)')
    ax2.tick_params(axis='y', labelcolor=color_cs)
    ax2.set_ylim(min(cs_overheads) * 0.7, max(cs_overheads) * 1.3)

    # Sombrear la zona óptima inferida
    min_w_idx = int(np.argmin(w_prom))
    opt_q = quantums[min_w_idx]
    ax1.axvspan(max(1, opt_q - 1), opt_q + 1, color='#2ecc71', alpha=0.18, label=f'Zona Óptima (Sweet Spot: q={opt_q-1}..{opt_q+1})')

    # Anotaciones didácticas inferidas
    ax1.annotate(f'Sobrecarga Excesiva (Thrashing):\n{cs_counts[0]} cambios de contexto\n{cs_overheads[0]:.1f}% CPU en cambios de contexto',
                 xy=(1, w_prom[0]), xytext=(1.4, max(w_prom) * 0.9),
                 arrowprops=dict(arrowstyle="->", color='#c0392b', lw=1.3),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.25", fc="#fadbd8", ec="#c0392b"))

    ax1.annotate(f'Mínimo W = {w_prom[min_w_idx]:.1f} ticks (q={opt_q})\nBalance óptimo',
                 xy=(opt_q, w_prom[min_w_idx]), xytext=(opt_q + 0.3, min(w_prom) * 0.8),
                 arrowprops=dict(arrowstyle="->", color='#27ae60', lw=1.3),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.25", fc="#d4efdf", ec="#27ae60"))

    ax1.annotate('Degeneración hacia FCFS:\nEspera sube linealmente\n(Efecto Convoy reaparece)',
                 xy=(15, w_prom[-1]), xytext=(10.2, max(w_prom) * 0.95),
                 arrowprops=dict(arrowstyle="->", color='#2980b9', lw=1.3),
                 fontsize=8.5, bbox=dict(boxstyle="round,pad=0.25", fc="#ebf5fb", ec="#2980b9"))

    lines = line1 + line2
    labels = [l.get_label() for l in lines]
    ax1.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 1.13), ncol=2, frameon=True)

    plt.title('Sensibilidad del Quantum en Round Robin vs. Sobrecarga de Kernel\n(Inferencia Dinámica desde scenarios/escenario1_convoy.csv)',
              fontsize=12, pad=25)
    plt.tight_layout()
    out_file = os.path.join(OUTPUT_DIR, "round_robin_sensibilidad_quantum.png")
    plt.savefig(out_file, dpi=300)
    plt.close()
    print(f"[OK] Curva de sensibilidad generada dinámicamente: {out_file}")


def main():
    print("=" * 75)
    print("  GENERANDO MÓDULO GRÁFICO DINÁMICO DE MÉTRICAS (SISTEMAS OPERATIVOS)")
    print(f"  Directorio destino: {OUTPUT_DIR}")
    print("=" * 75)

    plot_escenario1_comparativa()
    plot_escenario1_gantt()
    plot_escenario3_starvation_vs_aging()
    plot_round_robin_sensibilidad_quantum()

    print("\n[+] Todas las gráficas fueron generadas exitosamente desde datos reales a 300 DPI.")

if __name__ == "__main__":
    main()
