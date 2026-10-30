#ifndef SCHEDULER_FCFS_HPP
#define SCHEDULER_FCFS_HPP

#include "Scheduler.hpp"
#include <deque>

class SchedulerFCFS : public Scheduler {
private:
    std::deque<std::shared_ptr<Process>> ready_queue;

public:
    SchedulerFCFS() = default;

    void add_process(std::shared_ptr<Process> p) override;
    std::shared_ptr<Process> get_next_process() override;
    bool should_preempt(const std::shared_ptr<Process>& current_running, 
                        const std::shared_ptr<Process>& candidate) const override;
    void on_tick(int current_time) override;
    void on_process_completed_or_blocked(std::shared_ptr<Process> p) override;

    std::string get_name() const override { return "FCFS (First-Come, First-Served)"; }
    bool empty() const override { return ready_queue.empty(); }
    size_t ready_count() const override { return ready_queue.size(); }
    std::vector<std::shared_ptr<Process>> get_ready_processes() const override;
};

#endif // SCHEDULER_FCFS_HPP
