#include "SchedulerSRTF.hpp"
#include <algorithm>

void SchedulerSRTF::add_process(std::shared_ptr<Process> p) {
    p->state = ProcessState::READY;
    ready_queue.push_back(p);
}

std::shared_ptr<Process> SchedulerSRTF::get_next_process() {
    if (ready_queue.empty()) return nullptr;

    // Encuentra el proceso con menor tiempo restante en la ráfaga actual de CPU
    // Desempate por tiempo de llegada (FIFO)
    auto it = std::min_element(ready_queue.begin(), ready_queue.end(),
        [](const std::shared_ptr<Process>& a, const std::shared_ptr<Process>& b) {
            if (a->get_remaining_current_burst() != b->get_remaining_current_burst()) {
                return a->get_remaining_current_burst() < b->get_remaining_current_burst();
            }
            return a->arrival_time < b->arrival_time;
        });

    auto best = *it;
    ready_queue.erase(it);
    return best;
}

bool SchedulerSRTF::should_preempt(const std::shared_ptr<Process>& current_running, 
                                   const std::shared_ptr<Process>& candidate) const {
    if (!current_running || !candidate) return false;
    // Si el proceso candidato tiene una ráfaga restante estrictamente menor, desaloja
    return candidate->get_remaining_current_burst() < current_running->get_remaining_current_burst();
}

void SchedulerSRTF::on_tick(int /*current_time*/) {
    // No requiere lógica de reloj periódica en la cola
}

void SchedulerSRTF::on_process_completed_or_blocked(std::shared_ptr<Process> /*p*/) {
    // Sin acción especial
}
