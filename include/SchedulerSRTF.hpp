#ifndef SCHEDULER_SRTF_HPP
#define SCHEDULER_SRTF_HPP

#include "Scheduler.hpp"
#include <vector>

class SchedulerSRTF : public Scheduler {
private:
    std::vector<std::shared_ptr<Process>> ready_queue;

public:
    SchedulerSRTF() = default;

    void add_process(std::shared_ptr<Process> p) override;
    std::shared_ptr<Process> get_next_process() override;
    bool should_preempt(const std::shared_ptr<Process>& current_running, 
                        const std::shared_ptr<Process>& candidate) const override;
    void on_tick(int current_time) override;
    void on_process_completed_or_blocked(std::shared_ptr<Process> p) override;

    std::string get_name() const override { 
        return "SRTF (Shortest Remaining Time First - Apropiativo)"; 
    }
    bool empty() const override { return ready_queue.empty(); }
    size_t ready_count() const override { return ready_queue.size(); }
    std::vector<std::shared_ptr<Process>> get_ready_processes() const override { return ready_queue; }
};

#endif // SCHEDULER_SRTF_HPP
