#include "IOQueue.hpp"

void IOQueue::add_process(std::shared_ptr<Process> p) {
    p->state = ProcessState::WAITING;
    p->time_in_current_state = 0;
    // Bloquear al proceso hijo real en su llamada al sistema read() sobre la pipe POSIX
    p->enter_io_posix_block();
    waiting_list.push_back(p);
}

std::vector<std::shared_ptr<Process>> IOQueue::tick(int current_time) {
    std::vector<std::shared_ptr<Process>> completed_io;
    std::vector<std::shared_ptr<Process>> still_waiting;

    for (auto& p : waiting_list) {
        bool burst_finished = p->execute_io_tick(current_time);
        if (burst_finished) {
            // Despertar al proceso hijo real escribiendo en la pipe POSIX con write()
            p->wake_io_posix_block();
            completed_io.push_back(p);
        } else {
            still_waiting.push_back(p);
        }
    }

    waiting_list = std::move(still_waiting);
    return completed_io;
}
