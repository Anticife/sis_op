#include "SchedulerPriority.hpp"
#include <algorithm>

SchedulerPriority::SchedulerPriority(bool enable_aging, int aging_threshold)
    : enable_aging(enable_aging), aging_threshold(aging_threshold) {}

void SchedulerPriority::add_process(std::shared_ptr<Process> p) {
    p->state = ProcessState::READY;
    p->time_in_current_state = 0;
    ready_queue.push_back(p);
}

std::shared_ptr<Process> SchedulerPriority::get_next_process() {
    if (ready_queue.empty()) return nullptr;

    // Selecciona el proceso con menor dynamic_priority (1 es mayor prioridad que 2, etc.)
    // Desempate por orden de llegada
    auto it = std::min_element(ready_queue.begin(), ready_queue.end(),
        [](const std::shared_ptr<Process>& a, const std::shared_ptr<Process>& b) {
            if (a->dynamic_priority != b->dynamic_priority) {
                return a->dynamic_priority < b->dynamic_priority;
            }
            return a->arrival_time < b->arrival_time;
        });

    auto best = *it;
    ready_queue.erase(it);
    return best;
}

bool SchedulerPriority::should_preempt(const std::shared_ptr<Process>& current_running, 
                                       const std::shared_ptr<Process>& candidate) const {
    if (!current_running || !candidate) return false;
    // Si la prioridad dinámica del candidato es estrictamente superior (número menor)
    return candidate->dynamic_priority < current_running->dynamic_priority;
}

void SchedulerPriority::on_tick(int /*current_time*/) {
    if (!enable_aging) return;

    for (auto& p : ready_queue) {
        p->time_in_current_state++;
        if (p->time_in_current_state >= aging_threshold) {
            if (p->dynamic_priority > 1) {
                p->dynamic_priority--; // Promoción de prioridad (número menor = más urgente)
            }
            p->time_in_current_state = 0;
        }
    }
}

void SchedulerPriority::on_process_completed_or_blocked(std::shared_ptr<Process> /*p*/) {
    // Sin acción especial
}
