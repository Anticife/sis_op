#include "Simulator.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

Simulator::Simulator(std::shared_ptr<Scheduler> sched, int context_switch_penalty, bool step_mode)
    : scheduler(std::move(sched)),
      cpu(context_switch_penalty),
      current_time(0),
      step_mode(step_mode),
      last_running_pid(-1) {}

void Simulator::load_processes(const std::vector<std::shared_ptr<Process>>& input_processes) {
    processes.clear();
    for (const auto& p : input_processes) {
        processes.push_back(p->clone());
    }
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
        // Ignorar líneas vacías y comentarios
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(ss, token, ',')) {
            // Trim de espacios
            size_t start = token.find_first_not_of(" \t\r\n");
            size_t end = token.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                tokens.push_back(token.substr(start, end - start + 1));
            }
        }

        if (tokens.size() < 5) continue; // Mínimo: PID, Name, Arrival, Priority, Burst1
        if (tokens[0] == "PID" || tokens[0] == "pid") continue; // Saltar cabecera

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
                // Alternar por defecto CPU -> E/S -> CPU...
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
        std::cout << "\033[31m[CAMBIO DE CONTEXTO]\033[0m ";
    } else if (cpu.get_current_process()) {
        auto p = cpu.get_current_process();
        std::cout << "\033[32m[PID: " << p->pid << " - " << p->name 
                  << " (Rem: " << p->get_remaining_current_burst() << ", DinPri: " << p->dynamic_priority << ")]\033[0m ";
    } else {
        std::cout << "\033[90m[IDLE]\033[0m ";
    }

    // Cola de Listos
    std::cout << "| LISTOS: [ ";
    for (const auto& p : scheduler->get_ready_processes()) {
        std::cout << p->name << "(P" << p->pid << ", Pri:" << p->dynamic_priority << ", Rem:" << p->get_remaining_current_burst() << ") ";
    }
    std::cout << "] ";

    // Cola de E/S
    std::cout << "| BLOQUEADOS E/S: [ ";
    for (const auto& p : io_queue.get_waiting_processes()) {
        std::cout << p->name << "(P" << p->pid << ", RemIO:" << p->get_remaining_current_burst() << ") ";
    }
    std::cout << "]\n";

    std::cout << "Presiona [Enter] para continuar...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

SimulationResults Simulator::run() {
    current_time = 0;
    raw_gantt.clear();
    last_running_pid = -1;

    // Verificar si no hay procesos
    if (processes.empty()) {
        return Metrics::compute(scheduler->get_name(), processes, raw_gantt, 0, 0, 0, 0, 0);
    }

    auto all_terminated = [this]() {
        for (const auto& p : processes) {
            if (!p->is_completed()) return false;
        }
        return true;
    };

    while (!all_terminated()) {
        // 1. Llegadas de nuevos procesos al sistema en este tick
        std::vector<std::shared_ptr<Process>> new_arrivals;
        for (auto& p : processes) {
            if (p->state == ProcessState::NEW && p->arrival_time == current_time) {
                new_arrivals.push_back(p);
            }
        }

        // 2. Procesos que terminan su ráfaga de E/S en este tick
        auto completed_io = io_queue.tick(current_time);
        for (auto& p : completed_io) {
            p->advance_to_next_burst();
            if (p->is_completed()) {
                p->completion_time = current_time;
                p->state = ProcessState::TERMINATED;
            } else {
                new_arrivals.push_back(p);
            }
        }

        // Agregar los nuevos y desbloqueados a la cola de listos y verificar preemption
        for (auto& p : new_arrivals) {
            scheduler->add_process(p);

            // Si la CPU está ejecutando un proceso, evaluar si el nuevo proceso debe desalojarlo
            if (cpu.get_state() == CPUState::RUNNING) {
                auto current_p = cpu.get_current_process();
                if (scheduler->should_preempt(current_p, p)) {
                    auto preempted = cpu.preempt();
                    scheduler->add_process(preempted);
                    // El candidato y el desalojado están ahora en el planificador
                }
            }
        }

        // 3. Notificación de ciclo de reloj al planificador (para aging o quantums de RR)
        scheduler->on_tick(current_time);

        // 4. Si la CPU está ocupada, evaluar si el planificador exige preemption (ej. expiró quantum en RR)
        if (cpu.get_state() == CPUState::RUNNING) {
            auto current_p = cpu.get_current_process();
            if (scheduler->should_preempt(current_p, nullptr)) {
                auto preempted = cpu.preempt();
                scheduler->add_process(preempted);
            }
        }

        // 5. Si la CPU está IDLE y hay procesos listos, despachar el siguiente
        if (cpu.is_idle() && !scheduler->empty()) {
            auto next_p = scheduler->get_next_process();
            bool needs_cs = (last_running_pid != -1 && last_running_pid != next_p->pid);
            cpu.dispatch(next_p, needs_cs);
            last_running_pid = next_p->pid;
        }

        // 6. Modo interactivo paso a paso (si fue activado por el usuario)
        if (step_mode) {
            print_step_state();
        }

        // 7. Registrar segmento de Gantt en este tick
        if (cpu.is_context_switching()) {
            raw_gantt.push_back({current_time, current_time + 1, -2, "CS"});
        } else if (cpu.get_current_process()) {
            raw_gantt.push_back({current_time, current_time + 1, cpu.get_current_process()->pid, cpu.get_current_process()->name});
        } else {
            raw_gantt.push_back({current_time, current_time + 1, -1, "IDLE"});
        }

        // 8. Incrementar tiempo de espera (waiting time) de todos los procesos en estado READY
        for (auto& p : scheduler->get_ready_processes()) {
            p->waiting_time++;
        }

        // 9. Ejecutar 1 tick de CPU
        bool burst_done = cpu.tick(current_time);

        if (burst_done) {
            auto p = cpu.get_current_process();
            scheduler->on_process_completed_or_blocked(p);

            p->advance_to_next_burst();
            if (p->is_completed()) {
                p->completion_time = current_time + 1;
                p->state = ProcessState::TERMINATED;
                cpu.release();
            } else {
                if (p->is_current_burst_io()) {
                    io_queue.add_process(p);
                    cpu.release();
                } else {
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
                           current_time,
                           cpu.get_total_busy_time(),
                           cpu.get_total_idle_time(),
                           cpu.get_total_cs_time(),
                           cpu.get_total_context_switches());
}
