#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include <vector>
#include <memory>
#include <string>
#include "Process.hpp"
#include "Scheduler.hpp"
#include "CPU.hpp"
#include "IOQueue.hpp"
#include "Metrics.hpp"

class Simulator {
private:
    std::shared_ptr<Scheduler> scheduler;
    CPU cpu;
    IOQueue io_queue;

    std::vector<std::shared_ptr<Process>> processes;
    std::vector<GanttSegment> raw_gantt;
    std::vector<SyscallRecord> syscall_log;

    int current_time;
    bool step_mode;
    int tick_duration_ms;
    bool live_mode;
    std::string live_state_path;
    int last_running_pid; // Para detectar si el despacho requiere cambio de contexto

    void print_step_state() const;
    void print_live_ps_state(int tick) const;
    void write_live_state_json(int tick, bool finished = false) const;
    void record_syscall(int tick, const std::shared_ptr<Process>& p, const std::string& syscall_name, const std::string& details);

public:
    Simulator(std::shared_ptr<Scheduler> sched,
              int context_switch_penalty = 1,
              bool step_mode = false,
              int tick_duration_ms = 3,
              bool live_mode = false,
              const std::string& live_state_path = "/tmp/simulador_live_state.json");

    // Carga la lista inicial de procesos
    void load_processes(const std::vector<std::shared_ptr<Process>>& input_processes);

    // Ejecuta la simulación real con procesos POSIX hasta que todos hayan terminado y sido recolectados con wait4()
    SimulationResults run();

    // Carga de procesos desde archivo CSV
    static std::vector<std::shared_ptr<Process>> load_from_csv(const std::string& filepath);
};

#endif // SIMULATOR_HPP
