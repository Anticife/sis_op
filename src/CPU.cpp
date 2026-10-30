#include "CPU.hpp"

CPU::CPU(int context_switch_penalty)
    : state(CPUState::IDLE),
      current_process(nullptr),
      next_process_pending(nullptr),
      context_switch_penalty(context_switch_penalty),
      context_switch_remaining(0),
      total_busy_time(0),
      total_idle_time(0),
      total_cs_time(0),
      total_context_switches(0) {}

void CPU::dispatch(std::shared_ptr<Process> p, bool requires_context_switch) {
    if (requires_context_switch && context_switch_penalty > 0) {
        state = CPUState::CONTEXT_SWITCHING;
        context_switch_remaining = context_switch_penalty;
        next_process_pending = p;
        total_context_switches++;
        if (p) p->context_switches++;
    } else {
        current_process = p;
        next_process_pending = nullptr;
        if (current_process) {
            current_process->state = ProcessState::RUNNING;
            state = CPUState::RUNNING;
        } else {
            state = CPUState::IDLE;
        }
    }
}

std::shared_ptr<Process> CPU::preempt() {
    auto previous = current_process;
    current_process = nullptr;
    state = CPUState::IDLE;
    return previous;
}

void CPU::release() {
    current_process = nullptr;
    next_process_pending = nullptr;
    state = CPUState::IDLE;
}

bool CPU::tick(int current_time) {
    if (state == CPUState::CONTEXT_SWITCHING) {
        total_cs_time++;
        context_switch_remaining--;
        if (context_switch_remaining <= 0) {
            current_process = next_process_pending;
            next_process_pending = nullptr;
            if (current_process) {
                current_process->state = ProcessState::RUNNING;
                state = CPUState::RUNNING;
            } else {
                state = CPUState::IDLE;
            }
        }
        return false;
    }

    if (state == CPUState::RUNNING && current_process) {
        total_busy_time++;
        return current_process->execute_cpu_tick(current_time);
    }

    if (state == CPUState::IDLE) {
        total_idle_time++;
    }

    return false;
}
