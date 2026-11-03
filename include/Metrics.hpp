#ifndef METRICS_HPP
#define METRICS_HPP

#include <vector>
#include <string>
#include <memory>
#include <sys/types.h>
#include "Process.hpp"

struct GanttSegment {
    int start_time;
    int end_time;
    int pid;        // > 0 = Proceso, -1 = IDLE, -2 = CAMBIO_CONTEXTO
    pid_t os_pid;   // PID real en el kernel Linux
    std::string label;
};

struct SyscallRecord {
    int tick;
    pid_t os_pid;
    int logical_pid;
    std::string syscall_name;
    std::string details;
    char kernel_state;
};

struct SimulationResults {
    std::string algorithm_name;
    int total_time;
    int cpu_busy_time;
    int cpu_idle_time;
    int cpu_cs_time;
    int total_context_switches;
    double cpu_utilization_pct;
    double cpu_overhead_pct;
    double throughput;

    double avg_waiting_time;
    double avg_turnaround_time;
    double avg_response_time;
    double jains_fairness_index;

    // Telemetría POSIX Real agregada del Kernel Linux
    double total_real_cpu_ms;
    double total_real_cs_ms;
    long total_kernel_cs;
    int total_syscalls;

    std::vector<std::shared_ptr<Process>> processes;
    std::vector<GanttSegment> gantt_chart;
    std::vector<SyscallRecord> syscall_log;
};

class Metrics {
public:
    static SimulationResults compute(const std::string& algo_name,
                                     const std::vector<std::shared_ptr<Process>>& processes,
                                     const std::vector<GanttSegment>& raw_gantt,
                                     const std::vector<SyscallRecord>& syscall_log,
                                     int total_time,
                                     int cpu_busy_time,
                                     int cpu_idle_time,
                                     int cpu_cs_time,
                                     int total_context_switches,
                                     double real_cs_ms);

    static std::string render_gantt(const std::vector<GanttSegment>& gantt);
    static std::string render_table(const SimulationResults& res);
    static std::string render_comparison_table(const std::vector<SimulationResults>& results_list);
    static void export_csv(const std::string& filepath, const std::vector<SimulationResults>& results_list);
    static void export_json_trace(const std::string& filepath, const std::vector<SimulationResults>& results_list);
};

#endif // METRICS_HPP
