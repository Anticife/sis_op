#include "Simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

Simulator::Simulator(std::shared_ptr<Scheduler> sched, int context_switch_penalty, bool step_mode, int tick_duration_ms)
    : scheduler(std::move(sched)),
      cpu(context_switch_penalty, tick_duration_ms),
      current_time(0),
      step_mode(step_mode),
      last_running_pid(-1) {}

void Simulator::load_processes(const std::vector<std::shared_ptr<Process>>& input_processes) {
    processes.clear();
    for (const auto& p : input_processes) {
        processes.push_back(p->clone());
    }
}

void Simulator::record_syscall(int tick, const std::shared_ptr<Process>& p, const std::string& syscall_name, const std::string& details) {
    pid_t os_id = p ? p->os_pid : 0;
    int log_id = p ? p->pid : 0;
    char kstate = p ? p->linux_kernel_state : '-';
    syscall_log.push_back({tick, os_id, log_id, syscall_name, details, kstate});
}

std::vector<std::shared_ptr<Process>> Simulator::load_from_csv(const std::string& filepath) {
    std::vector<std::shared_ptr<Process>> result;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo de escenario: " << filepath << "\n";
        return result;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t\r\n");
            size_t end = token.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                tokens.push_back(token.substr(start, end - start + 1));
            }
        }

        if (tokens.size() < 5) continue;
        if (tokens[0] == "PID" || tokens[0] == "pid") continue;

        int pid = std::stoi(tokens[0]);
        std::string name = tokens[1];
        int arrival = std::stoi(tokens[2]);
        int priority = std::stoi(tokens[3]);

        std::vector<Burst> bursts;
        bool expect_cpu = true;

        for (size_t i = 4; i < tokens.size(); i++) {
            std::string b_str = tokens[i];
            if (b_str.empty()) continue;

            BurstType btype = BurstType::CPU;
            int duration = 0;

            if (b_str[0] == 'C' || b_str[0] == 'c') {
                btype = BurstType::CPU;
                duration = std::stoi(b_str.substr(1));
            } else if (b_str[0] == 'I' || b_str[0] == 'i') {
                btype = BurstType::IO;
                duration = std::stoi(b_str.substr(1));
            } else {
                btype = expect_cpu ? BurstType::CPU : BurstType::IO;
                duration = std::stoi(b_str);
                expect_cpu = !expect_cpu;
            }

            if (duration > 0) {
                bursts.push_back({btype, duration});
            }
        }

        if (!bursts.empty()) {
            result.push_back(std::make_shared<Process>(pid, name, arrival, priority, bursts));
        }
    }

    return result;
}

void Simulator::print_step_state() const {
    std::cout << "\n\033[1;35m>>> [TICK: " << current_time << "] <<<\033[0m ";

    // Estado CPU
    std::cout << "CPU: ";
    if (cpu.is_context_switching()) {
        auto pend = cpu.get_pending_process();
        std::cout << "\033[31m[CAMBIO DE CONTEXTO POSIX -> SIGSTOP/SIGCONT hacia OS_PID:"
                  << (pend ? pend->os_pid : -1) << "]\033[0m ";
    } else if (cpu.get_current_process()) {
        auto p = cpu.get_current_process();
        std::cout << "\033[32m[P" << p->pid << " (OS_PID:" << p->os_pid << ", /proc:" << p->linux_kernel_state
                  << ") - " << p->name << " (Rem:" << p->get_remaining_current_burst()
                  << ", Nice:" << p->nice_value << ")]\033[0m ";
    } else {
        std::cout << "\033[90m[IDLE]\033[0m ";
    }

    // Cola de Listos
    std::cout << "| LISTOS (SIGSTOP): [ ";
    for (const auto& p : scheduler->get_ready_processes()) {
        std::cout << p->name << "(P" << p->pid << ", OS:" << p->os_pid
                  << ", /proc:" << p->linux_kernel_state
                  << ", Pri:" << p->dynamic_priority << ", Rem:" << p->get_remaining_current_burst() << ") ";
    }
    std::cout << "] ";

    // Cola de E/S
    std::cout << "| BLOQUEADOS PIPE E/S: [ ";
    for (const auto& p : io_queue.get_waiting_processes()) {
        std::cout << p->name << "(P" << p->pid << ", OS:" << p->os_pid
                  << ", /proc:" << p->linux_kernel_state
                  << ", RemIO:" << p->get_remaining_current_burst() << ") ";
    }
    std::cout << "]\n";

    std::cout << "Presiona [Enter] para avanzar 1 tick POSIX...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

SimulationResults Simulator::run() {
    current_time = 0;
    raw_gantt.clear();
    syscall_log.clear();
    last_running_pid = -1;

    if (processes.empty()) {
        return Metrics::compute(scheduler->get_name(), processes, raw_gantt, syscall_log, 0, 0, 0, 0, 0, 0.0);
    }

    auto all_terminated = [this]() {
        for (const auto& p : processes) {
            if (!p->is_completed()) return false;
        }
        return true;
    };

    bool prev_tick_executed_cpu = false;

    while (!all_terminated()) {
        // 1. Llegadas de nuevos procesos: Crear proceso real en el kernel Linux con fork(), mmap(), pipe(), sched_setaffinity(), setpriority() y SIGSTOP
        std::vector<std::shared_ptr<Process>> new_arrivals;
        for (auto& p : processes) {
            if (p->state == ProcessState::NEW && p->arrival_time == current_time) {
                p->spawn_posix_process();
                record_syscall(current_time, p, "fork() + mmap() + pipe() + sched_setaffinity()",
                               "Proceso real creado en Linux con OS_PID=" + std::to_string(p->os_pid) + " y suspendido con SIGSTOP en READY");
                new_arrivals.push_back(p);
            }
        }

        // 2. Procesos que terminan su ráfaga de E/S en este tick (despertados mediante write() en pipe POSIX)
        auto completed_io = io_queue.tick(current_time);
        for (auto& p : completed_io) {
            record_syscall(current_time, p, "write(io_pipe) + kill(SIGSTOP)",
                           "Desbloqueo de read() en pipe de E/S; pasa a estado READY");
            p->advance_to_next_burst();
            if (p->is_completed()) {
                p->completion_time = current_time;
                p->state = ProcessState::TERMINATED;
                p->terminate_and_reap_posix();
                record_syscall(current_time, p, "wait4(OS_PID, &status, 0, &rusage)",
                               "Proceso finalizado tras E/S y recolectado del kernel");
            } else {
                new_arrivals.push_back(p);
            }
        }

        // Agregar los nuevos y desbloqueados a la cola de listos y verificar preemption
        for (auto& p : new_arrivals) {
            scheduler->add_process(p);

            if (cpu.get_state() == CPUState::RUNNING) {
                auto current_p = cpu.get_current_process();
                if (scheduler->should_preempt(current_p, p)) {
                    auto preempted = cpu.preempt();
                    record_syscall(current_time, preempted, "kill(OS_PID, SIGSTOP) + waitpid(WUNTRACED)",
                                   "Desalojo apropiativo de CPU por llegada de P" + std::to_string(p->pid) + " (OS_PID=" + std::to_string(p->os_pid) + ")");
                    scheduler->add_process(preempted);
                }
            }
        }

        // 3. Notificación de ciclo de reloj al planificador (para Aging -> setpriority() o quantums de RR)
        std::vector<int> prev_prios;
        for (const auto& rp : scheduler->get_ready_processes()) {
            prev_prios.push_back(rp->dynamic_priority);
        }
        if (prev_tick_executed_cpu || scheduler->get_name().find("Round Robin") == std::string::npos) {
            scheduler->on_tick(current_time);
        }
        auto ready_now = scheduler->get_ready_processes();
        for (size_t idx = 0; idx < ready_now.size() && idx < prev_prios.size(); ++idx) {
            if (ready_now[idx]->dynamic_priority != prev_prios[idx]) {
                record_syscall(current_time, ready_now[idx], "setpriority(PRIO_PROCESS, OS_PID, nice)",
                               "Aging promovio prioridad a " + std::to_string(ready_now[idx]->dynamic_priority) +
                               " (nice=" + std::to_string(ready_now[idx]->nice_value) + ")");
            }
        }

        // 4. Si la CPU está ocupada, evaluar si el planificador exige preemption (ej. expiró quantum en RR o Aging promovió a alguien)
        if (cpu.get_state() == CPUState::RUNNING) {
            auto current_p = cpu.get_current_process();
            bool preempt_needed = scheduler->should_preempt(current_p, nullptr);
            if (!preempt_needed) {
                // Verificar si algún proceso en READY superó la prioridad del proceso en ejecución (por Aging)
                for (const auto& cand : scheduler->get_ready_processes()) {
                    if (scheduler->should_preempt(current_p, cand)) {
                        preempt_needed = true;
                        break;
                    }
                }
            }
            if (preempt_needed) {
                auto preempted = cpu.preempt();
                record_syscall(current_time, preempted, "kill(OS_PID, SIGSTOP) + waitpid(WUNTRACED)",
                               "Preempcion de CPU por expiracion de quantum / prioridad envejecida");
                scheduler->add_process(preempted);
            }
        }

        // 5. Si la CPU está IDLE y hay procesos listos, despachar el siguiente
        if (cpu.is_idle() && !scheduler->empty()) {
            auto next_p = scheduler->get_next_process();
            bool needs_cs = (last_running_pid != -1 && last_running_pid != next_p->pid);
            cpu.dispatch(next_p, needs_cs);
            if (needs_cs) {
                record_syscall(current_time, next_p, "clock_nanosleep() [Context Switch]",
                               "Conmutacion de contexto en kernel hacia OS_PID=" + std::to_string(next_p->os_pid));
            } else {
                record_syscall(current_time, next_p, "kill(OS_PID, SIGCONT)",
                               "Despacho directo de proceso en kernel Linux");
            }
            last_running_pid = next_p->pid;
        }

        // 6. Modo interactivo paso a paso (si fue activado por el usuario)
        if (step_mode) {
            print_step_state();
        }

        // 7. Registrar segmento de Gantt en este tick con su OS_PID real
        if (cpu.is_context_switching()) {
            auto pend = cpu.get_pending_process();
            raw_gantt.push_back({current_time, current_time + 1, -2, pend ? pend->os_pid : 0, "CS"});
        } else if (cpu.get_current_process()) {
            auto cur = cpu.get_current_process();
            raw_gantt.push_back({current_time, current_time + 1, cur->pid, cur->os_pid, cur->name});
        } else {
            raw_gantt.push_back({current_time, current_time + 1, -1, 0, "IDLE"});
        }

        // 8. Incrementar tiempo de espera (waiting time) de todos los procesos en estado READY
        for (auto& p : scheduler->get_ready_processes()) {
            p->waiting_time++;
        }

        // 9. Ejecutar 1 tick físico real de CPU mediante clock_nanosleep() y lectura de /proc/<os_pid>/stat
        bool was_cs = cpu.is_context_switching();
        prev_tick_executed_cpu = (cpu.get_state() == CPUState::RUNNING && cpu.get_current_process() != nullptr);
        bool burst_done = cpu.tick(current_time);
        if (was_cs && cpu.get_state() == CPUState::RUNNING && cpu.get_current_process()) {
            record_syscall(current_time + 1, cpu.get_current_process(), "kill(OS_PID, SIGCONT)",
                           "Fin de cambio de contexto; proceso reanudado en CPU");
        }

        if (burst_done) {
            auto p = cpu.get_current_process();
            scheduler->on_process_completed_or_blocked(p);

            p->advance_to_next_burst();
            if (p->is_completed()) {
                p->completion_time = current_time + 1;
                p->state = ProcessState::TERMINATED;
                p->terminate_and_reap_posix();
                record_syscall(current_time + 1, p, "wait4(OS_PID, &status, 0, &rusage)",
                               "Proceso terminado (_exit(0)) y recolectado; CPU_ms=" +
                               std::to_string(p->real_user_cpu_ms + p->real_sys_cpu_ms));
                cpu.release();
            } else {
                if (p->is_current_burst_io()) {
                    io_queue.add_process(p);
                    record_syscall(current_time + 1, p, "read(io_pipe_fd) [Blocking I/O]",
                                   "Proceso bloqueado en pipe POSIX esperando E/S (/proc state='" +
                                   std::string(1, p->linux_kernel_state) + "')");
                    cpu.release();
                } else {
                    p->suspend_posix_process();
                    scheduler->add_process(p);
                    cpu.release();
                }
            }
        }

        current_time++;
    }

    return Metrics::compute(scheduler->get_name(),
                           processes,
                           raw_gantt,
                           syscall_log,
                           current_time,
                           cpu.get_total_busy_time(),
                           cpu.get_total_idle_time(),
                           cpu.get_total_cs_time(),
                           cpu.get_total_context_switches(),
                           cpu.get_real_cs_overhead_ms());
}
