#include "Metrics.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <numeric>
#include <cmath>

SimulationResults Metrics::compute(const std::string& algo_name,
                                   const std::vector<std::shared_ptr<Process>>& processes,
                                   const std::vector<GanttSegment>& raw_gantt,
                                   int total_time,
                                   int cpu_busy_time,
                                   int cpu_idle_time,
                                   int cpu_cs_time,
                                   int total_context_switches) {
    SimulationResults res;
    res.algorithm_name = algo_name;
    res.total_time = total_time;
    res.cpu_busy_time = cpu_busy_time;
    res.cpu_idle_time = cpu_idle_time;
    res.cpu_cs_time = cpu_cs_time;
    res.total_context_switches = total_context_switches;
    res.processes = processes;

    if (total_time > 0) {
        res.cpu_utilization_pct = (static_cast<double>(cpu_busy_time) / total_time) * 100.0;
        res.cpu_overhead_pct = (static_cast<double>(cpu_cs_time) / total_time) * 100.0;
        res.throughput = static_cast<double>(processes.size()) / total_time;
    } else {
        res.cpu_utilization_pct = 0.0;
        res.cpu_overhead_pct = 0.0;
        res.throughput = 0.0;
    }

    double sum_wait = 0.0;
    double sum_turnaround = 0.0;
    double sum_response = 0.0;

    double jain_sum = 0.0;
    double jain_sum_sq = 0.0;

    for (const auto& p : processes) {
        int ta = p->get_turnaround_time();
        int resp = p->get_response_time();

        sum_turnaround += ta;
        sum_wait += p->waiting_time;
        sum_response += resp;

        // Ratio de servicio útil respecto al tiempo total en el sistema
        double ratio = (ta > 0) ? (static_cast<double>(p->total_cpu_time) / ta) : 1.0;
        jain_sum += ratio;
        jain_sum_sq += (ratio * ratio);
    }

    size_t n = processes.size();
    if (n > 0) {
        res.avg_waiting_time = sum_wait / n;
        res.avg_turnaround_time = sum_turnaround / n;
        res.avg_response_time = sum_response / n;

        if (jain_sum_sq > 0.0) {
            res.jains_fairness_index = (jain_sum * jain_sum) / (n * jain_sum_sq);
        } else {
            res.jains_fairness_index = 1.0;
        }
    } else {
        res.avg_waiting_time = 0.0;
        res.avg_turnaround_time = 0.0;
        res.avg_response_time = 0.0;
        res.jains_fairness_index = 1.0;
    }

    // Comprimir el diagrama de Gantt (fusionar segmentos contiguos del mismo proceso)
    for (const auto& seg : raw_gantt) {
        if (!res.gantt_chart.empty() && res.gantt_chart.back().pid == seg.pid) {
            res.gantt_chart.back().end_time = seg.end_time;
        } else {
            res.gantt_chart.push_back(seg);
        }
    }

    return res;
}

std::string Metrics::render_gantt(const std::vector<GanttSegment>& gantt) {
    if (gantt.empty()) return "(Diagrama de Gantt vacío)\n";

    std::ostringstream out;
    out << "\n\033[1;36m=== DIAGRAMA DE GANTT (Línea de Tiempo de la CPU) ===\033[0m\n\n";

    // Fila superior de bloques
    out << "  ";
    for (const auto& seg : gantt) {
        std::string color = "\033[32m"; // Verde por defecto para procesos
        if (seg.pid == -1) color = "\033[90m"; // Gris para IDLE
        else if (seg.pid == -2) color = "\033[31m"; // Rojo para Cambio de Contexto

        out << color << "[" << seg.label << "]" << "\033[0m";
    }
    out << "\n";

    // Fila inferior de tiempos
    out << "  ";
    for (const auto& seg : gantt) {
        out << seg.start_time;
        int width = static_cast<int>(seg.label.length()) + 2;
        int pad = width - static_cast<int>(std::to_string(seg.start_time).length());
        if (pad > 0) out << std::string(pad, ' ');
    }
    if (!gantt.empty()) {
        out << gantt.back().end_time;
    }
    out << "\n\n";

    return out.str();
}

std::string Metrics::render_table(const SimulationResults& res) {
    std::ostringstream out;
    out << "\n\033[1;33m------------------------------------------------------------------------------------------------------------------------\033[0m\n";
    out << "\033[1;37m RESULTADOS DETALLADOS POR PROCESO - " << res.algorithm_name << "\033[0m\n";
    out << "\033[1;33m------------------------------------------------------------------------------------------------------------------------\033[0m\n";
    out << std::left
        << std::setw(6)  << "PID"
        << std::setw(18) << "Nombre"
        << std::setw(8)  << "Llegada"
        << std::setw(8)  << "P.Base"
        << std::setw(8)  << "P.Din"
        << std::setw(8)  << "CPU T."
        << std::setw(8)  << "E/S T."
        << std::setw(8)  << "Inicio"
        << std::setw(8)  << "Fin"
        << std::setw(12) << "Retorno(T)"
        << std::setw(12) << "Espera(W)"
        << std::setw(12) << "Resp.(R)"
        << std::setw(6)  << "C.C."
        << "\n";
    out << "------------------------------------------------------------------------------------------------------------------------\n";

    for (const auto& p : res.processes) {
        out << std::left
            << std::setw(6)  << p->pid
            << std::setw(18) << p->name
            << std::setw(8)  << p->arrival_time
            << std::setw(8)  << p->base_priority
            << std::setw(8)  << p->dynamic_priority
            << std::setw(8)  << p->total_cpu_time
            << std::setw(8)  << p->total_io_time
            << std::setw(8)  << p->first_run_time
            << std::setw(8)  << p->completion_time
            << std::setw(12) << p->get_turnaround_time()
            << std::setw(12) << p->waiting_time
            << std::setw(12) << p->get_response_time()
            << std::setw(6)  << p->context_switches
            << "\n";
    }
    out << "------------------------------------------------------------------------------------------------------------------------\n";
    out << std::fixed << std::setprecision(2);
    out << "\033[1;32mMétricas Globales:\033[0m\n";
    out << "  * Tiempo total de simulación: " << res.total_time << " ticks\n";
    out << "  * Tiempo CPU Ocupada (Útil):   " << res.cpu_busy_time << " ticks (" << res.cpu_utilization_pct << "%)\n";
    out << "  * Tiempo Cambio de Contexto:  " << res.cpu_cs_time << " ticks (" << res.cpu_overhead_pct << "% sobrecarga)\n";
    out << "  * Tiempo CPU Inactiva (Idle): " << res.cpu_idle_time << " ticks\n";
    out << "  * Total Cambios de Contexto:  " << res.total_context_switches << "\n";
    out << "  * Rendimiento (Throughput):   " << res.throughput << " procesos/tick\n";
    out << "  * Promedio Tiempo Retorno (T): " << res.avg_turnaround_time << " ticks\n";
    out << "  * Promedio Tiempo Espera (W):  " << res.avg_waiting_time << " ticks\n";
    out << "  * Promedio Tiempo Respuesta(R):" << res.avg_response_time << " ticks\n";
    out << "  * Índice de Equidad de Jain:  " << res.jains_fairness_index << " (1.0 = equidad perfecta)\n\n";

    return out.str();
}

std::string Metrics::render_comparison_table(const std::vector<SimulationResults>& results_list) {
    if (results_list.empty()) return "";

    std::ostringstream out;
    out << "\n\033[1;36m========================================================================================================================\033[0m\n";
    out << "\033[1;37m TABLA COMPARATIVA CONSOLIDADA DE ALGORITMOS DE PLANIFICACIÓN\033[0m\n";
    out << "\033[1;36m========================================================================================================================\033[0m\n";
    out << std::left
        << std::setw(38) << "Algoritmo"
        << std::setw(12) << "T.Promedio"
        << std::setw(12) << "W.Promedio"
        << std::setw(12) << "R.Promedio"
        << std::setw(12) << "CPU Util%"
        << std::setw(12) << "Overhead%"
        << std::setw(10) << "C.C."
        << std::setw(12) << "Throughput"
        << std::setw(10) << "Jain Index"
        << "\n";
    out << "------------------------------------------------------------------------------------------------------------------------\n";

    for (const auto& res : results_list) {
        out << std::left << std::fixed << std::setprecision(2)
            << std::setw(38) << res.algorithm_name
            << std::setw(12) << res.avg_turnaround_time
            << std::setw(12) << res.avg_waiting_time
            << std::setw(12) << res.avg_response_time
            << std::setw(12) << res.cpu_utilization_pct
            << std::setw(12) << res.cpu_overhead_pct
            << std::setw(10) << res.total_context_switches
            << std::setw(12) << res.throughput
            << std::setw(10) << res.jains_fairness_index
            << "\n";
    }
    out << "========================================================================================================================\n\n";

    return out.str();
}

void Metrics::export_csv(const std::string& filepath, const std::vector<SimulationResults>& results_list) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error al abrir el archivo para exportar CSV: " << filepath << "\n";
        return;
    }

    // Cabecera del archivo CSV
    file << "Algoritmo,PID,Nombre,Llegada,PrioridadBase,PrioridadDinamica,TotalCPU,TotalIO,Inicio,Fin,Retorno,Espera,Respuesta,CambiosContexto\n";

    for (const auto& res : results_list) {
        for (const auto& p : res.processes) {
            file << "\"" << res.algorithm_name << "\","
                 << p->pid << ","
                 << "\"" << p->name << "\","
                 << p->arrival_time << ","
                 << p->base_priority << ","
                 << p->dynamic_priority << ","
                 << p->total_cpu_time << ","
                 << p->total_io_time << ","
                 << p->first_run_time << ","
                 << p->completion_time << ","
                 << p->get_turnaround_time() << ","
                 << p->waiting_time << ","
                 << p->get_response_time() << ","
                 << p->context_switches << "\n";
        }
    }

    // Sección de Resumen Global en el CSV
    file << "\n--- RESUMEN GLOBAL DE METRICAS ---\n";
    file << "Algoritmo,TiempoTotal,CPU_Utilizacion_Pct,CPU_Overhead_Pct,Throughput,Promedio_Retorno,Promedio_Espera,Promedio_Respuesta,Total_Cambios_Contexto,Indice_Jain\n";

    for (const auto& res : results_list) {
        file << "\"" << res.algorithm_name << "\","
             << res.total_time << ","
             << res.cpu_utilization_pct << ","
             << res.cpu_overhead_pct << ","
             << res.throughput << ","
             << res.avg_turnaround_time << ","
             << res.avg_waiting_time << ","
             << res.avg_response_time << ","
             << res.total_context_switches << ","
             << res.jains_fairness_index << "\n";
    }

    file.close();
}
