#include "Process.hpp"
#include <stdexcept>

Process::Process(int pid, const std::string& name, int arrival_time, int priority, const std::vector<Burst>& bursts)
    : pid(pid),
      name(name),
      arrival_time(arrival_time),
      base_priority(priority),
      dynamic_priority(priority),
      bursts(bursts),
      current_burst_index(0),
      state(ProcessState::NEW),
      first_run_time(-1),
      completion_time(-1),
      total_cpu_time(0),
      total_io_time(0),
      executed_cpu_time(0),
      executed_io_time(0),
      waiting_time(0),
      context_switches(0),
      time_in_current_state(0) {

    for (const auto& b : bursts) {
        if (b.type == BurstType::CPU) {
            total_cpu_time += b.duration;
        } else {
            total_io_time += b.duration;
        }
    }

    if (!bursts.empty()) {
        remaining_burst = bursts[0].duration;
    } else {
        remaining_burst = 0;
        state = ProcessState::TERMINATED;
    }
}

std::shared_ptr<Process> Process::clone() const {
    auto copy = std::make_shared<Process>(pid, name, arrival_time, base_priority, bursts);
    return copy;
}

bool Process::is_completed() const {
    return state == ProcessState::TERMINATED || current_burst_index >= bursts.size();
}

bool Process::is_current_burst_cpu() const {
    if (is_completed()) return false;
    return bursts[current_burst_index].type == BurstType::CPU;
}

bool Process::is_current_burst_io() const {
    if (is_completed()) return false;
    return bursts[current_burst_index].type == BurstType::IO;
}

bool Process::execute_cpu_tick(int current_time) {
    if (first_run_time == -1) {
        first_run_time = current_time;
    }

    executed_cpu_time++;
    remaining_burst--;

    if (remaining_burst <= 0) {
        return true; // Ráfaga CPU completada
    }
    return false;
}

bool Process::execute_io_tick(int /*current_time*/) {
    executed_io_time++;
    remaining_burst--;

    if (remaining_burst <= 0) {
        return true; // Ráfaga E/S completada
    }
    return false;
}

bool Process::advance_to_next_burst() {
    current_burst_index++;
    if (current_burst_index < bursts.size()) {
        remaining_burst = bursts[current_burst_index].duration;
        return true;
    }
    state = ProcessState::TERMINATED;
    remaining_burst = 0;
    return false;
}

int Process::get_remaining_current_burst() const {
    return remaining_burst;
}

int Process::get_turnaround_time() const {
    if (completion_time == -1) return 0;
    return completion_time - arrival_time;
}

int Process::get_response_time() const {
    if (first_run_time == -1) return 0;
    return first_run_time - arrival_time;
}
