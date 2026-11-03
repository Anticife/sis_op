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
                                   const std::vector<SyscallRecord>& syscall_log,
                                   int total_time,
                                   int cpu_busy_time,
                                   int cpu_idle_time,
                                   int cpu_cs_time,
                                   int total_context_switches,
                                   double real_cs_ms) {
    SimulationResults res;
    res.algorithm_name = algo_name;
    res.total_time = total_time;
    res.cpu_busy_time = cpu_busy_time;
    res.cpu_idle_time = cpu_idle_time;
    res.cpu_cs_time = cpu_cs_time;
    res.total_context_switches = total_context_switches;
    res.processes = processes;
    res.syscall_log = syscall_log;
    res.total_real_cs_ms = real_cs_ms;
    res.total_real_cpu_ms = 0.0;
    res.total_kernel_cs = 0;
    res.total_syscalls = 0;

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

        res.total_real_cpu_ms += (p->real_user_cpu_ms + p->real_sys_cpu_ms);
        res.total_kernel_cs += (p->kernel_voluntary_cs + p->kernel_nonvoluntary_cs);
        res.total_syscalls += p->syscalls_executed;

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
    out << "\n\033[1;36m=== DIAGRAMA DE GANTT POSIX (Línea de Tiempo de Procesos Reales en CPU) ===\033[0m\n\n";

    out << "  ";
    for (const auto& seg : gantt) {
        std::string color = "\033[32m";
        if (seg.pid == -1) color = "\033[90m";
        else if (seg.pid == -2) color = "\033[31m";

        std::string text = seg.label;
        if (seg.pid > 0 && seg.os_pid > 0) {
            text += "(os:" + std::to_string(seg.os_pid) + ")";
        }
        out << color << "[" << text << "]" << "\033[0m";
    }
    out << "\n";

    out << "  ";
    for (const auto& seg : gantt) {
        std::string text = seg.label;
        if (seg.pid > 0 && seg.os_pid > 0) {
            text += "(os:" + std::to_string(seg.os_pid) + ")";
        }
        out << seg.start_time;
        int width = static_cast<int>(text.length()) + 2;
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

    out << "\033[1;33m=== RESULTADOS POSIX: " << res.algorithm_name << " ===\033[0m\n";
    out << std::string(134, '-') << "\n";
    out << std::left
        << std::setw(5)  << "PID"
        << std::setw(9)  << "OS_PID"
        << std::setw(18) << "Nombre"
        << std::setw(6)  << "Lleg."
        << std::setw(9)  << "Pri(B/D)"
        << std::setw(6)  << "Nice"
        << std::setw(6)  << "CPU"
        << std::setw(6)  << "E/S"
        << std::setw(6)  << "Ini"
        << std::setw(6)  << "Fin"
        << std::setw(8)  << "Ret(T)"
        << std::setw(8)  << "Esp(W)"
        << std::setw(8)  << "Resp(R)"
        << std::setw(6)  << "C.C."
        << std::setw(10) << "CPU_ms"
        << std::setw(8)  << "KernCS"
        << std::setw(9)  << "Syscalls"
        << "\n";
    out << std::string(134, '-') << "\n";

    for (const auto& p : res.processes) {
        std::string pri_str = std::to_string(p->base_priority) + "/" + std::to_string(p->dynamic_priority);
        double proc_cpu_ms = p->real_user_cpu_ms + p->real_sys_cpu_ms;
        long kern_cs = p->kernel_voluntary_cs + p->kernel_nonvoluntary_cs;
        out << std::left << std::fixed << std::setprecision(2)
            << std::setw(5)  << p->pid
            << std::setw(9)  << p->os_pid
            << std::setw(18) << p->name.substr(0, 17)
            << std::setw(6)  << p->arrival_time
            << std::setw(9)  << pri_str
            << std::setw(6)  << p->nice_value
            << std::setw(6)  << p->total_cpu_time
            << std::setw(6)  << p->total_io_time
            << std::setw(6)  << p->first_run_time
            << std::setw(6)  << p->completion_time
            << std::setw(8)  << p->get_turnaround_time()
            << std::setw(8)  << p->waiting_time
            << std::setw(8)  << p->get_response_time()
            << std::setw(6)  << p->context_switches
            << std::setw(10) << proc_cpu_ms
            << std::setw(8)  << kern_cs
            << std::setw(9)  << p->syscalls_executed
            << "\n";
    }
    out << std::string(134, '-') << "\n";

    out << std::fixed << std::setprecision(2);
    out << "  \033[1mMétricas Promedio y Telemetría del Kernel Linux:\033[0m\n"
        << "  • Tiempo de Retorno Promedio (T)  : " << res.avg_turnaround_time << " ticks\n"
        << "  • Tiempo de Espera Promedio (W)   : " << res.avg_waiting_time << " ticks\n"
        << "  • Tiempo de Respuesta Promedio (R): " << res.avg_response_time << " ticks\n"
        << "  • Utilización Efectiva de CPU     : " << res.cpu_utilization_pct << "% (" << res.cpu_busy_time << "/" << res.total_time << " ticks)\n"
        << "  • Sobrecarga Cambio de Contexto   : " << res.cpu_overhead_pct << "% (" << res.total_context_switches << " switches, " << res.total_real_cs_ms << " ms reales)\n"
        << "  • Tiempo Real de CPU (wait4/clock): " << res.total_real_cpu_ms << " ms | Context Switches en Kernel (rusage): " << res.total_kernel_cs << "\n"
        << "  • Llamadas al Sistema POSIX Reales: " << res.total_syscalls << " syscalls (fork, kill, wait4, pipe, read, write, mmap, setpriority, /proc)\n"
        << "  • Rendimiento (Throughput)        : " << std::setprecision(4) << res.throughput << " procesos/tick\n"
        << "  • Índice de Equidad de Jain       : " << std::setprecision(4) << res.jains_fairness_index << " (1.0 = equidad perfecta)\n\n";

    return out.str();
}

std::string Metrics::render_comparison_table(const std::vector<SimulationResults>& results_list) {
    std::ostringstream out;

    out << "\n\033[1;32m========================================================================================================================================\033[0m\n";
    out << "\033[1;32m                         TABLA COMPARATIVA DE ALGORITMOS (EJECUCIÓN REAL CON SYSCALLS POSIX EN LINUX)                                   \033[0m\n";
    out << "\033[1;32m========================================================================================================================================\033[0m\n";
    out << std::left
        << std::setw(38) << "Algoritmo"
        << std::setw(10) << "Ret.(T)"
        << std::setw(10) << "Esp.(W)"
        << std::setw(10) << "Resp.(R)"
        << std::setw(11) << "CPU Util%"
        << std::setw(11) << "CS Overh%"
        << std::setw(8)  << "C.C."
        << std::setw(11) << "CPU_ms"
        << std::setw(10) << "Kern_CS"
        << std::setw(10) << "Syscalls"
        << std::setw(10) << "JainIdx"
        << "\n";
    out << std::string(139, '-') << "\n";

    for (const auto& res : results_list) {
        out << std::left << std::fixed << std::setprecision(2)
            << std::setw(38) << res.algorithm_name.substr(0, 37)
            << std::setw(10) << res.avg_turnaround_time
            << std::setw(10) << res.avg_waiting_time
            << std::setw(10) << res.avg_response_time
            << std::setw(11) << res.cpu_utilization_pct
            << std::setw(11) << res.cpu_overhead_pct
            << std::setw(8)  << res.total_context_switches
            << std::setw(11) << res.total_real_cpu_ms
            << std::setw(10) << res.total_kernel_cs
            << std::setw(10) << res.total_syscalls
            << std::setw(10) << res.jains_fairness_index
            << "\n";
    }
    out << "========================================================================================================================================\n\n";

    return out.str();
}

void Metrics::export_csv(const std::string& filepath, const std::vector<SimulationResults>& results_list) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error al abrir el archivo para exportar CSV: " << filepath << "\n";
        return;
    }

    // Cabecera de procesos (mantiene las primeras 14 columnas compatibles + columnas de telemetría POSIX real)
    file << "Algoritmo,PID,Nombre,Llegada,PrioridadBase,PrioridadDinamica,TotalCPU,TotalIO,Inicio,Fin,Retorno,Espera,Respuesta,CambiosContexto,OS_PID,Nice,RealCPU_ms,KernelCS,Syscalls\n";

    for (const auto& res : results_list) {
        for (const auto& p : res.processes) {
            double proc_cpu_ms = p->real_user_cpu_ms + p->real_sys_cpu_ms;
            long kern_cs = p->kernel_voluntary_cs + p->kernel_nonvoluntary_cs;
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
                 << p->context_switches << ","
                 << p->os_pid << ","
                 << p->nice_value << ","
                 << std::fixed << std::setprecision(3) << proc_cpu_ms << ","
                 << kern_cs << ","
                 << p->syscalls_executed << "\n";
        }
    }

    // Sección de Resumen Global en el CSV
    file << "\n--- RESUMEN GLOBAL DE METRICAS ---\n";
    file << "Algoritmo,TiempoTotal,CPU_Utilizacion_Pct,CPU_Overhead_Pct,Throughput,Promedio_Retorno,Promedio_Espera,Promedio_Respuesta,Total_Cambios_Contexto,Indice_Jain,Total_Real_CPU_ms,Total_Kernel_CS,Total_POSIX_Syscalls\n";

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
             << res.jains_fairness_index << ","
             << std::fixed << std::setprecision(3) << res.total_real_cpu_ms << ","
             << res.total_kernel_cs << ","
             << res.total_syscalls << "\n";
    }

    // Sección de Diagrama de Gantt con OS_PID real
    file << "\n--- DIAGRAMA DE GANTT ---\n";
    file << "Algoritmo,Inicio,Fin,PID,Etiqueta,OS_PID\n";

    for (const auto& res : results_list) {
        for (const auto& seg : res.gantt_chart) {
            file << "\"" << res.algorithm_name << "\","
                 << seg.start_time << ","
                 << seg.end_time << ","
                 << seg.pid << ","
                 << "\"" << seg.label << "\","
                 << seg.os_pid << "\n";
        }
    }

    // Sección de Auditoría de Llamadas al Sistema POSIX Reales
    file << "\n--- REGISTRO DE LLAMADAS AL SISTEMA POSIX ---\n";
    file << "Algoritmo,Tick,PID_Logico,OS_PID_Linux,Syscall,Estado_ProcFS,Detalle\n";
    for (const auto& res : results_list) {
        for (const auto& ev : res.syscall_log) {
            file << "\"" << res.algorithm_name << "\","
                 << ev.tick << ","
                 << ev.logical_pid << ","
                 << ev.os_pid << ","
                 << "\"" << ev.syscall_name << "\","
                 << "\"" << ev.kernel_state << "\","
                 << "\"" << ev.details << "\"\n";
        }
    }

    file.close();
}

void Metrics::export_json_trace(const std::string& filepath, const std::vector<SimulationResults>& results_list) {
    std::ofstream file(filepath);
    if (!file.is_open()) return;

    file << "{\n  \"algorithms\": [\n";
    for (size_t i = 0; i < results_list.size(); ++i) {
        const auto& res = results_list[i];
        file << "    {\n";
        file << "      \"name\": \"" << res.algorithm_name << "\",\n";
        file << "      \"total_time\": " << res.total_time << ",\n";
        file << "      \"cpu_utilization_pct\": " << res.cpu_utilization_pct << ",\n";
        file << "      \"cpu_overhead_pct\": " << res.cpu_overhead_pct << ",\n";
        file << "      \"avg_turnaround\": " << res.avg_turnaround_time << ",\n";
        file << "      \"avg_waiting\": " << res.avg_waiting_time << ",\n";
        file << "      \"avg_response\": " << res.avg_response_time << ",\n";
        file << "      \"context_switches\": " << res.total_context_switches << ",\n";
        file << "      \"jains_index\": " << res.jains_fairness_index << ",\n";
        file << "      \"total_real_cpu_ms\": " << res.total_real_cpu_ms << ",\n";
        file << "      \"total_kernel_cs\": " << res.total_kernel_cs << ",\n";
        file << "      \"total_syscalls\": " << res.total_syscalls << ",\n";

        file << "      \"processes\": [\n";
        for (size_t j = 0; j < res.processes.size(); ++j) {
            const auto& p = res.processes[j];
            file << "        {\"pid\": " << p->pid
                 << ", \"os_pid\": " << p->os_pid
                 << ", \"name\": \"" << p->name << "\""
                 << ", \"arrival\": " << p->arrival_time
                 << ", \"base_priority\": " << p->base_priority
                 << ", \"dynamic_priority\": " << p->dynamic_priority
                 << ", \"nice\": " << p->nice_value
                 << ", \"total_cpu\": " << p->total_cpu_time
                 << ", \"total_io\": " << p->total_io_time
                 << ", \"first_run\": " << p->first_run_time
                 << ", \"completion\": " << p->completion_time
                 << ", \"turnaround\": " << p->get_turnaround_time()
                 << ", \"waiting\": " << p->waiting_time
                 << ", \"response\": " << p->get_response_time()
                 << ", \"real_cpu_ms\": " << (p->real_user_cpu_ms + p->real_sys_cpu_ms)
                 << ", \"kernel_cs\": " << (p->kernel_voluntary_cs + p->kernel_nonvoluntary_cs)
                 << ", \"syscalls\": " << p->syscalls_executed
                 << "}" << (j + 1 < res.processes.size() ? "," : "") << "\n";
        }
        file << "      ],\n";

        file << "      \"gantt\": [\n";
        for (size_t j = 0; j < res.gantt_chart.size(); ++j) {
            const auto& g = res.gantt_chart[j];
            file << "        {\"start\": " << g.start_time
                 << ", \"end\": " << g.end_time
                 << ", \"pid\": " << g.pid
                 << ", \"os_pid\": " << g.os_pid
                 << ", \"label\": \"" << g.label << "\"}"
                 << (j + 1 < res.gantt_chart.size() ? "," : "") << "\n";
        }
        file << "      ],\n";

        file << "      \"syscalls\": [\n";
        for (size_t j = 0; j < res.syscall_log.size(); ++j) {
            const auto& s = res.syscall_log[j];
            file << "        {\"tick\": " << s.tick
                 << ", \"pid\": " << s.logical_pid
                 << ", \"os_pid\": " << s.os_pid
                 << ", \"syscall\": \"" << s.syscall_name << "\""
                 << ", \"state\": \"" << s.kernel_state << "\""
                 << ", \"details\": \"" << s.details << "\"}"
                 << (j + 1 < res.syscall_log.size() ? "," : "") << "\n";
        }
        file << "      ]\n";
        file << "    }" << (i + 1 < results_list.size() ? "," : "") << "\n";
    }
    file << "  ]\n}\n";
    file.close();
}
