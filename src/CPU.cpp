#include "CPU.hpp"
#include <time.h>

CPU::CPU(int context_switch_penalty, int tick_duration_ms)
    : state(CPUState::IDLE),
      current_process(nullptr),
      next_process_pending(nullptr),
      context_switch_penalty(context_switch_penalty),
      context_switch_remaining(0),
      tick_duration_ms(tick_duration_ms),
      total_busy_time(0),
      total_idle_time(0),
      total_cs_time(0),
      total_context_switches(0),
      real_cs_overhead_ms(0.0) {}

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
            // Despertar al proceso real en el kernel Linux con kill(os_pid, SIGCONT)
            current_process->resume_posix_process();
            state = CPUState::RUNNING;
        } else {
            state = CPUState::IDLE;
        }
    }
}

std::shared_ptr<Process> CPU::preempt() {
    auto previous = current_process;
    if (previous) {
        // Suspender al proceso real en el kernel Linux con kill(os_pid, SIGSTOP) + waitpid(WUNTRACED)
        previous->suspend_posix_process();
    }
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
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        if (tick_duration_ms > 0) {
            struct timespec req;
            req.tv_sec = tick_duration_ms / 1000;
            req.tv_nsec = (tick_duration_ms % 1000) * 1000000L;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
        }

        total_cs_time++;
        context_switch_remaining--;
        if (context_switch_remaining <= 0) {
            current_process = next_process_pending;
            next_process_pending = nullptr;
            if (current_process) {
                current_process->state = ProcessState::RUNNING;
                // Reanudar al proceso entrante en el kernel mediante kill(os_pid, SIGCONT)
                current_process->resume_posix_process();
                state = CPUState::RUNNING;
            } else {
                state = CPUState::IDLE;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        double elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0 +
                            (t_end.tv_nsec - t_start.tv_nsec) / 1000000.0;
        real_cs_overhead_ms += elapsed_ms;
        return false;
    }

    if (state == CPUState::RUNNING && current_process) {
        total_busy_time++;
        return current_process->execute_cpu_tick(current_time, tick_duration_ms);
    }

    if (state == CPUState::IDLE) {
        if (tick_duration_ms > 0) {
            struct timespec req = {0, (tick_duration_ms % 1000) * 1000000L};
            clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
        }
        total_idle_time++;
    }

    return false;
}
