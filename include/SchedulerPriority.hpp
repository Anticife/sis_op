#ifndef SCHEDULER_PRIORITY_HPP
#define SCHEDULER_PRIORITY_HPP

#include "Scheduler.hpp"
#include <vector>

class SchedulerPriority : public Scheduler {
private:
    std::vector<std::shared_ptr<Process>> ready_queue;
    bool enable_aging;
    int aging_threshold; // Ticks en READY necesarios para aumentar 1 nivel de prioridad

public:
    explicit SchedulerPriority(bool enable_aging = true, int aging_threshold = 5);

    void add_process(std::shared_ptr<Process> p) override;
    std::shared_ptr<Process> get_next_process() override;
    bool should_preempt(const std::shared_ptr<Process>& current_running, 
                        const std::shared_ptr<Process>& candidate) const override;
    void on_tick(int current_time) override;
    void on_process_completed_or_blocked(std::shared_ptr<Process> p) override;

    bool is_aging_enabled() const { return enable_aging; }
    int get_aging_threshold() const { return aging_threshold; }

    std::string get_name() const override { 
        return enable_aging ? "Prioridad Apropiativa con Envejecimiento (Aging, th=" + std::to_string(aging_threshold) + ")"
                            : "Prioridad Apropiativa Estricta (Sin Aging)"; 
    }
    bool empty() const override { return ready_queue.empty(); }
    size_t ready_count() const override { return ready_queue.size(); }
    std::vector<std::shared_ptr<Process>> get_ready_processes() const override { return ready_queue; }
};

#endif // SCHEDULER_PRIORITY_HPP
