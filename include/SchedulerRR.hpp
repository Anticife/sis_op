#ifndef SCHEDULER_RR_HPP
#define SCHEDULER_RR_HPP

#include "Scheduler.hpp"
#include <deque>

class SchedulerRR : public Scheduler {
private:
    std::deque<std::shared_ptr<Process>> ready_queue;
    int quantum;
    int quantum_ticks_used;

public:
    explicit SchedulerRR(int quantum = 4);

    void add_process(std::shared_ptr<Process> p) override;
    std::shared_ptr<Process> get_next_process() override;
    bool should_preempt(const std::shared_ptr<Process>& current_running, 
                        const std::shared_ptr<Process>& candidate) const override;
    void on_tick(int current_time) override;
    void on_process_completed_or_blocked(std::shared_ptr<Process> p) override;

    int get_quantum() const { return quantum; }
    int get_quantum_ticks_used() const { return quantum_ticks_used; }
    bool is_quantum_expired() const;
    void reset_quantum();

    std::string get_name() const override { 
        return "Round Robin (RR, q=" + std::to_string(quantum) + ")"; 
    }
    bool empty() const override { return ready_queue.empty(); }
    size_t ready_count() const override { return ready_queue.size(); }
    std::vector<std::shared_ptr<Process>> get_ready_processes() const override;
};

#endif // SCHEDULER_RR_HPP
