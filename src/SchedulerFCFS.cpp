#include "SchedulerFCFS.hpp"

void SchedulerFCFS::add_process(std::shared_ptr<Process> p) {
    p->state = ProcessState::READY;
    ready_queue.push_back(p);
}

std::shared_ptr<Process> SchedulerFCFS::get_next_process() {
    if (ready_queue.empty()) return nullptr;
    auto p = ready_queue.front();
    ready_queue.pop_front();
    return p;
}

bool SchedulerFCFS::should_preempt(const std::shared_ptr<Process>& /*current_running*/, 
                                   const std::shared_ptr<Process>& /*candidate*/) const {
    return false; // FCFS es estrictamente no apropiativo
}

void SchedulerFCFS::on_tick(int /*current_time*/) {
    // FCFS no modifica prioridades ni usa quantums
}

void SchedulerFCFS::on_process_completed_or_blocked(std::shared_ptr<Process> /*p*/) {
    // Sin acción especial
}

std::vector<std::shared_ptr<Process>> SchedulerFCFS::get_ready_processes() const {
    return std::vector<std::shared_ptr<Process>>(ready_queue.begin(), ready_queue.end());
}
