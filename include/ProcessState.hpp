#ifndef PROCESS_STATE_HPP
#define PROCESS_STATE_HPP

#include <string>

enum class ProcessState {
    NEW,
    READY,
    RUNNING,
    WAITING,    // Bloqueado en cola de E/S
    TERMINATED
};

inline std::string to_string(ProcessState state) {
    switch (state) {
        case ProcessState::NEW:        return "NEW";
        case ProcessState::READY:      return "READY";
        case ProcessState::RUNNING:    return "RUNNING";
        case ProcessState::WAITING:    return "WAITING";
        case ProcessState::TERMINATED: return "TERMINATED";
        default:                       return "UNKNOWN";
    }
}

inline std::string to_ansi_color(ProcessState state) {
    switch (state) {
        case ProcessState::NEW:        return "\033[90m"; // Gris
        case ProcessState::READY:      return "\033[33m"; // Amarillo
        case ProcessState::RUNNING:    return "\033[32m"; // Verde
        case ProcessState::WAITING:    return "\033[35m"; // Magenta
        case ProcessState::TERMINATED: return "\033[34m"; // Azul
        default:                       return "\033[0m";  // Reset
    }
}

inline std::string reset_color() {
    return "\033[0m";
}

#endif // PROCESS_STATE_HPP
