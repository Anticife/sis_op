#include "SchedulerRR.hpp"

SchedulerRR::SchedulerRR(int quantum)
    : quantum(quantum), quantum_ticks_used(0) {}

void SchedulerRR::add_process(std::shared_ptr<Process> p) {
    p->state = ProcessState::READY;
    ready_queue.push_back(p);
}

std::shared_ptr<Process> SchedulerRR::get_next_process() {
    if (ready_queue.empty()) return nullptr;
    auto p = ready_queue.front();
    ready_queue.pop_front();
    quantum_ticks_used = 0;
    return p;
}

bool SchedulerRR::is_quantum_expired() const {
    return quantum_ticks_used >= quantum;
}

void SchedulerRR::reset_quantum() {
    quantum_ticks_used = 0;
}

bool SchedulerRR::should_preempt(const std::shared_ptr<Process>& /*current_running*/, 
                                 const std::shared_ptr<Process>& /*candidate*/) const {
    // Si el quantum expiró y hay alguien esperando en la cola de listos, desaloja
    return is_quantum_expired() && !ready_queue.empty();
}

void SchedulerRR::on_tick(int /*current_time*/) {
    quantum_ticks_used++;
}

void SchedulerRR::on_process_completed_or_blocked(std::shared_ptr<Process> /*p*/) {
    quantum_ticks_used = 0;
}

std::vector<std::shared_ptr<Process>> SchedulerRR::get_ready_processes() const {
    return std::vector<std::shared_ptr<Process>>(ready_queue.begin(), ready_queue.end());
}
