#ifndef CPU_HPP
#define CPU_HPP

#include <memory>
#include "Process.hpp"

enum class CPUState {
    IDLE,
    RUNNING,
    CONTEXT_SWITCHING
};

class CPU {
private:
    CPUState state;
    std::shared_ptr<Process> current_process;
    std::shared_ptr<Process> next_process_pending; // Proceso a cargar tras el cambio de contexto
    int context_switch_penalty;                    // Costo en ticks por guardar/restaurar PCB
    int context_switch_remaining;

    // Métricas globales de la CPU
    int total_busy_time;
    int total_idle_time;
    int total_cs_time;
    int total_context_switches;

public:
    explicit CPU(int context_switch_penalty = 1);

    CPUState get_state() const { return state; }
    std::shared_ptr<Process> get_current_process() const { return current_process; }
    bool is_idle() const { return state == CPUState::IDLE; }
    bool is_context_switching() const { return state == CPUState::CONTEXT_SWITCHING; }

    int get_total_busy_time() const { return total_busy_time; }
    int get_total_idle_time() const { return total_idle_time; }
    int get_total_cs_time() const { return total_cs_time; }
    int get_total_context_switches() const { return total_context_switches; }
    int get_context_switch_penalty() const { return context_switch_penalty; }

    // Despacha un nuevo proceso. Si había un proceso anterior o la penalización > 0, inicia cambio de contexto.
    void dispatch(std::shared_ptr<Process> p, bool requires_context_switch);

    // Desaloja el proceso actual (por preemption). Retorna el proceso desalojado.
    std::shared_ptr<Process> preempt();

    // Libera la CPU porque el proceso terminó o se fue a E/S
    void release();

    // Avanza 1 tick de reloj en la CPU.
    // Retorna true si un proceso estaba en ejecución y completó su ráfaga actual de CPU.
    bool tick(int current_time);
};

#endif // CPU_HPP
