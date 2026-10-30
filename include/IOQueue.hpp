#ifndef IO_QUEUE_HPP
#define IO_QUEUE_HPP

#include <vector>
#include <memory>
#include "Process.hpp"

class IOQueue {
private:
    std::vector<std::shared_ptr<Process>> waiting_list;

public:
    IOQueue() = default;

    void add_process(std::shared_ptr<Process> p);

    // Avanza 1 tick en los procesos en espera de E/S.
    // Retorna la lista de procesos que completaron su ráfaga de E/S en este tick.
    std::vector<std::shared_ptr<Process>> tick(int current_time);

    bool empty() const { return waiting_list.empty(); }
    size_t size() const { return waiting_list.size(); }
    std::vector<std::shared_ptr<Process>> get_waiting_processes() const { return waiting_list; }
};

#endif // IO_QUEUE_HPP
