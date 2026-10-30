#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include <memory>
#include <vector>
#include <string>
#include "Process.hpp"

class Scheduler {
public:
    virtual ~Scheduler() = default;

    // Agrega un proceso a la cola de listos (READY)
    virtual void add_process(std::shared_ptr<Process> p) = 0;

    // Obtiene el siguiente proceso a despachar (lo extrae de la cola de listos)
    virtual std::shared_ptr<Process> get_next_process() = 0;

    // Evalúa si un nuevo proceso candidato debe desalojar al proceso que está actualmente en CPU
    virtual bool should_preempt(const std::shared_ptr<Process>& current_running, 
                                const std::shared_ptr<Process>& candidate) const = 0;

    // Evento por cada ciclo de reloj (tick) para lógica como envejecimiento (aging) o quantums
    virtual void on_tick(int current_time) = 0;

    // Notificación cuando el proceso actual deja la CPU (por fin de ráfaga o paso a E/S)
    virtual void on_process_completed_or_blocked(std::shared_ptr<Process> p) = 0;

    virtual std::string get_name() const = 0;
    virtual bool empty() const = 0;
    virtual size_t ready_count() const = 0;
    virtual std::vector<std::shared_ptr<Process>> get_ready_processes() const = 0;
};

#endif // SCHEDULER_HPP
