#include "Simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <time.h>

Simulator::Simulator(std::shared_ptr<Scheduler> sched,
                     int context_switch_penalty,
                     bool step_mode,
                     int tick_duration_ms,
                     bool live_mode,
                     const std::string& live_state_path)
    : scheduler(std::move(sched)),
      cpu(context_switch_penalty, tick_duration_ms),
      current_time(0),
      step_mode(step_mode),
      tick_duration_ms(tick_duration_ms),
      live_mode(live_mode),
      live_state_path(live_state_path),
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

    // Mostrar tabla ps real también en modo paso a paso
    print_live_ps_state(current_time);

    std::cout << "Presiona [Enter] para avanzar 1 tick POSIX...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

void Simulator::print_live_ps_state(int tick) const {
    std::cout << "\n\033[1;36m==================== [TICK POSIX t=" << tick
              << "s | PADRE PID=" << getpid() << " | " << scheduler->get_name() << "] ====================\033[0m\n";

    std::cout << "  \033[1;32mCPU (R+)\033[0m: ";
    if (cpu.is_context_switching()) {
        auto pend = cpu.get_pending_process();
        std::cout << "CONMUTANDO CONTEXTO -> P" << (pend ? pend->pid : -1)
                  << " (PID=" << (pend ? pend->os_pid : -1) << ")";
    } else if (cpu.get_current_process()) {
        auto cur = cpu.get_current_process();
        std::cout << "P" << cur->pid << ":" << cur->name << " (PID=" << cur->os_pid
                  << ", STAT=" << cur->linux_kernel_state << "+, Rem=" << cur->get_remaining_current_burst() << "s)";
    } else {
        std::cout << "IDLE";
    }

    std::cout << " | \033[1;33mREADY/STOP (T+)\033[0m: ";
    for (const auto& rp : scheduler->get_ready_processes()) {
        std::cout << "P" << rp->pid << "(PID=" << rp->os_pid << ",T+) ";
    }

    std::cout << "| \033[1;34mESPERA E/S (S+)\033[0m: ";
    for (const auto& ip : io_queue.get_waiting_processes()) {
        std::cout << "P" << ip->pid << "(PID=" << ip->os_pid << ",S+,wchan=" << ip->linux_wchan << ") ";
    }

    std::cout << "| \033[1;31mZOMBIE (Z+)\033[0m: ";
    for (const auto& zp : processes) {
        if (zp->posix_zombie && !zp->posix_reaped) {
            std::cout << "P" << zp->pid << "(PID=" << zp->os_pid << ",Z+<defunct>) ";
        }
    }
    std::cout << "\n\033[90m--- Salida en vivo de: ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest ---\033[0m\n";

    FILE* fp = popen("ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest 2>/dev/null", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp) != nullptr) {
            std::cout << "  " << line;
        }
        pclose(fp);
    }
    std::cout << std::flush;
}

void Simulator::write_live_state_json(int tick, bool finished) const {
    if (live_state_path.empty()) return;

    std::string tmp_path = live_state_path + ".tmp";
    std::ofstream out(tmp_path);
    if (!out.is_open()) return;

    std::string cpu_state_str = "IDLE";
    int running_pid = -1;
    pid_t running_os_pid = 0;
    std::string running_name = "IDLE";

    if (cpu.is_context_switching()) {
        cpu_state_str = "CONTEXT_SWITCH";
        auto pend = cpu.get_pending_process();
        if (pend) {
            running_pid = pend->pid;
            running_os_pid = pend->os_pid;
            running_name = pend->name;
        }
    } else if (cpu.get_current_process()) {
        cpu_state_str = "RUNNING";
        auto cur = cpu.get_current_process();
        running_pid = cur->pid;
        running_os_pid = cur->os_pid;
        running_name = cur->name;
    }

    out << "{\n";
    out << "  \"active\": " << (finished ? "false" : "true") << ",\n";
    out << "  \"parent_pid\": " << getpid() << ",\n";
    out << "  \"ppid\": " << getppid() << ",\n";
    out << "  \"tick\": " << tick << ",\n";
    out << "  \"tick_ms\": " << tick_duration_ms << ",\n";
    out << "  \"algorithm\": \"" << scheduler->get_name() << "\",\n";
    out << "  \"cpu_state\": \"" << cpu_state_str << "\",\n";
    out << "  \"running_pid\": " << running_pid << ",\n";
    out << "  \"running_os_pid\": " << running_os_pid << ",\n";
    out << "  \"running_name\": \"" << running_name << "\",\n";
    out << "  \"busy_ticks\": " << cpu.get_total_busy_time() << ",\n";
    out << "  \"cs_ticks\": " << cpu.get_total_cs_time() << ",\n";
    out << "  \"cs_count\": " << cpu.get_total_context_switches() << ",\n";

    out << "  \"processes\": [\n";
    for (size_t i = 0; i < processes.size(); ++i) {
        const auto& p = processes[i];
        std::string logical_state = "NEW";
        if (p->state == ProcessState::RUNNING) logical_state = "RUNNING";
        else if (p->state == ProcessState::READY) logical_state = "READY";
        else if (p->state == ProcessState::WAITING) logical_state = "WAITING_IO";
        else if (p->state == ProcessState::TERMINATED) logical_state = "TERMINATED";

        int total_work = p->total_cpu_time + p->total_io_time;
        int done_work = p->executed_cpu_time + p->executed_io_time;
        int progress_pct = (total_work > 0) ? ((done_work * 100) / total_work) : 100;
        if (p->state == ProcessState::TERMINATED) progress_pct = 100;

        out << "    {\"pid\": " << p->pid
            << ", \"os_pid\": " << p->os_pid
            << ", \"name\": \"" << p->name << "\""
            << ", \"arrival\": " << p->arrival_time
            << ", \"base_priority\": " << p->base_priority
            << ", \"dynamic_priority\": " << p->dynamic_priority
            << ", \"nice\": " << p->nice_value
            << ", \"logical_state\": \"" << logical_state << "\""
            << ", \"proc_state\": \"" << std::string(1, p->linux_kernel_state) << "\""
            << ", \"wchan\": \"" << p->linux_wchan << "\""
            << ", \"rem_burst\": " << p->get_remaining_current_burst()
            << ", \"total_cpu\": " << p->total_cpu_time
            << ", \"total_io\": " << p->total_io_time
            << ", \"exec_cpu\": " << p->executed_cpu_time
            << ", \"exec_io\": " << p->executed_io_time
            << ", \"progress_pct\": " << progress_pct
            << ", \"wait_time\": " << p->waiting_time
            << ", \"turnaround\": " << p->get_turnaround_time()
            << ", \"response\": " << p->get_response_time()
            << ", \"zombie\": " << (p->posix_zombie ? "true" : "false")
            << ", \"cpu_ops\": " << p->real_cpu_operations
            << ", \"cpu_ms\": " << (p->real_user_cpu_ms + p->real_sys_cpu_ms)
            << ", \"syscalls\": " << p->syscalls_executed
            << "}" << (i + 1 < processes.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"gantt\": [\n";
    for (size_t i = 0; i < raw_gantt.size(); ++i) {
        const auto& g = raw_gantt[i];
        out << "    {\"t\": " << g.start_time
            << ", \"pid\": " << g.pid
            << ", \"os_pid\": " << g.os_pid
            << ", \"label\": \"" << g.label << "\"}"
            << (i + 1 < raw_gantt.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"syscalls\": [\n";
    size_t start_sc = syscall_log.size() > 100 ? syscall_log.size() - 100 : 0;
    for (size_t i = start_sc; i < syscall_log.size(); ++i) {
        const auto& sc = syscall_log[i];
        out << "    {\"tick\": " << sc.tick
            << ", \"pid\": " << sc.logical_pid
            << ", \"os_pid\": " << sc.os_pid
            << ", \"syscall\": \"" << sc.syscall_name << "\""
            << ", \"detail\": \"" << sc.details << "\""
            << ", \"proc_state\": \"" << std::string(1, sc.kernel_state) << "\"}"
            << (i + 1 < syscall_log.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    out.close();

    std::rename(tmp_path.c_str(), live_state_path.c_str());
}

SimulationResults Simulator::run() {
    current_time = 0;
    raw_gantt.clear();
    syscall_log.clear();
    last_running_pid = -1;

    if (processes.empty()) {
        return Metrics::compute(scheduler->get_name(), processes, raw_gantt, syscall_log, 0, 0, 0, 0, 0, 0.0);
    }

    // 0. Instanciar todos los procesos hijos reales con fork() al inicio para que todo el árbol de procesos
    // sea inmediatamente visible en el kernel Linux (ps -C simulador --forest) con sus PIDs reales.
    for (auto& p : processes) {
        p->spawn_posix_process();
    }

    if (live_mode || step_mode || tick_duration_ms >= 200) {
        std::cout << "\n\033[1;33m========================================================================================================\033[0m\n";
        std::cout << "\033[1;32mPADRE PLANIFICADOR PID=" << getpid() << " PPID=" << getppid()
                  << " | Algoritmo: " << scheduler->get_name()
                  << " | Tick=" << tick_duration_ms << " ms\033[0m\n";
        for (const auto& p : processes) {
            std::cout << "  HIJO_P" << p->pid << " [" << p->name << "] PID=" << p->os_pid
                      << " PPID=" << getpid()
                      << " | Llegada=" << p->arrival_time << "s"
                      << " | Prioridad=" << p->base_priority << " (nice=" << p->nice_value << ")"
                      << " | Estado Inicial=/proc/" << p->os_pid << "/stat: " << p->linux_kernel_state << " (SIGSTOP)\n";
        }
        std::cout << "\033[1;36mProcesos reales creados en Linux:\033[0m ";
        for (size_t i = 0; i < processes.size(); ++i) {
            std::cout << "P" << processes[i]->pid << "=" << processes[i]->os_pid
                      << (i + 1 < processes.size() ? " | " : "\n");
        }
        std::cout << "Observe los procesos en tiempo real desde otra terminal con:\n"
                  << "  \033[1;37mwatch -n 0.5 \"ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest\"\033[0m\n"
                  << "  O ejecuta: \033[1;32mmake monitor\033[0m\n";
        std::cout << "\033[1;33m========================================================================================================\033[0m\n" << std::flush;
    }

    auto all_terminated = [this]() {
        for (const auto& p : processes) {
            if (!p->is_completed()) return false;
        }
        return true;
    };

    bool prev_tick_executed_cpu = false;

    while (!all_terminated()) {
        // 1. Llegadas de procesos en el tick actual (pasan de NEW a READY en el planificador)
        std::vector<std::shared_ptr<Process>> new_arrivals;
        for (auto& p : processes) {
            if (p->state == ProcessState::NEW && p->arrival_time == current_time) {
                record_syscall(current_time, p, "fork() + mmap() + pipe() + sched_setaffinity()",
                               "Proceso activo en planificador con OS_PID=" + std::to_string(p->os_pid) + " (SIGSTOP en READY)");
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
                p->enter_zombie_posix_state();
                record_syscall(current_time, p, "_exit(0) [ZOMBIE <defunct>]",
                               "Proceso hijo termina y pasa a estado Z+ (<defunct>) en ps esperando wait4()");
                if (live_mode || tick_duration_ms >= 200) {
                    std::cout << "\033[1;31m[t=" << current_time << "s] HIJO_ZOMBIE P" << p->pid
                              << " (" << p->name << ") PID=" << p->os_pid << " PPID=" << getpid()
                              << " termina ahora -> estado Z+ <defunct> en ps\033[0m\n";
                }
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

        // 4. Si la CPU está ocupada, evaluar si el planificador exige preemption
        if (cpu.get_state() == CPUState::RUNNING) {
            auto current_p = cpu.get_current_process();
            bool preempt_needed = scheduler->should_preempt(current_p, nullptr);
            if (!preempt_needed) {
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
                               "Despacho directo de proceso en kernel Linux (Estado R+)");
            }
            last_running_pid = next_p->pid;
        }

        // 6. Registrar segmento de Gantt en este tick con su OS_PID real
        if (cpu.is_context_switching()) {
            auto pend = cpu.get_pending_process();
            raw_gantt.push_back({current_time, current_time + 1, -2, pend ? pend->os_pid : 0, "CS"});
        } else if (cpu.get_current_process()) {
            auto cur = cpu.get_current_process();
            raw_gantt.push_back({current_time, current_time + 1, cur->pid, cur->os_pid, cur->name});
        } else {
            raw_gantt.push_back({current_time, current_time + 1, -1, 0, "IDLE"});
        }

        // Si estamos en modo en vivo, dar 25ms para que el kernel planifique al hijo recién despertado con SIGCONT
        // y aparezca inmediatamente en estado 'R+' consumiendo CPU en la salida de 'ps'
        if (live_mode || tick_duration_ms >= 100) {
            struct timespec settle = {0, 25000000L}; // 25 ms
            clock_nanosleep(CLOCK_MONOTONIC, 0, &settle, nullptr);
        }

        // Actualizar el estado real leído de /proc/<os_pid>/stat y /proc/<os_pid>/wchan para todos los procesos
        for (auto& p : processes) {
            p->query_linux_proc_state();
        }

        if (live_mode || tick_duration_ms >= 100) {
            write_live_state_json(current_time, false);
        }

        if (live_mode) {
            print_live_ps_state(current_time);
        }

        // 7. Modo interactivo paso a paso (si fue activado por el usuario)
        if (step_mode) {
            print_step_state();
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
                           "Fin de cambio de contexto; proceso reanudado en CPU (Estado R+)");
        }

        if (burst_done) {
            auto p = cpu.get_current_process();
            scheduler->on_process_completed_or_blocked(p);

            p->advance_to_next_burst();
            if (p->is_completed()) {
                p->completion_time = current_time + 1;
                p->state = ProcessState::TERMINATED;
                // Pasa a estado ZOMBIE real (_exit(0)) sin llamar aún a wait4(), para que sea visible como Z+ <defunct> en ps
                p->enter_zombie_posix_state();
                record_syscall(current_time + 1, p, "_exit(0) [ZOMBIE <defunct>]",
                               "Proceso terminado (_exit(0)) en estado Z+ <defunct> visible en ps");
                if (live_mode || tick_duration_ms >= 200) {
                    std::cout << "\033[1;31m[t=" << (current_time + 1) << "s] HIJO_ZOMBIE P" << p->pid
                              << " (" << p->name << ") PID=" << p->os_pid << " PPID=" << getpid()
                              << " termina ahora -> estado Z+ <defunct> en ps\033[0m\n";
                }
                cpu.release();
            } else {
                if (p->is_current_burst_io()) {
                    io_queue.add_process(p);
                    record_syscall(current_time + 1, p, "read(io_pipe_fd) [Blocking I/O]",
                                   "Proceso bloqueado en pipe POSIX esperando E/S (/proc state='" +
                                   std::string(1, p->linux_kernel_state) + "', wchan=" + p->linux_wchan + ")");
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

    // Mostrar estado final en ps con los procesos en Z+ antes de recolectarlos con wait4()
    if (live_mode) {
        print_live_ps_state(current_time);
        write_live_state_json(current_time, false);
        std::cout << "\n\033[1;33m[KERNEL] Recolectando procesos ZOMBIE (Z+ <defunct>) mediante wait4(OS_PID, &status, 0, &rusage)...\033[0m\n";
    }

    // 10. Recolectar todos los procesos hijos con wait4() y extraer struct rusage real del kernel Linux
    for (auto& p : processes) {
        p->terminate_and_reap_posix();
        record_syscall(current_time, p, "wait4(OS_PID, &status, 0, &rusage)",
                       "Zombie recolectado del kernel; CPU_ms=" +
                       std::to_string(p->real_user_cpu_ms + p->real_sys_cpu_ms));
    }

    if (live_mode || tick_duration_ms >= 100) {
        write_live_state_json(current_time, true);
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
