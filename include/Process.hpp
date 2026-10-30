#ifndef PROCESS_HPP
#define PROCESS_HPP

#include <string>
#include <vector>
#include <memory>
#include "ProcessState.hpp"

enum class BurstType {
    CPU,
    IO
};

struct Burst {
    BurstType type;
    int duration;
};

class Process {
public:
    int pid;
    std::string name;
    int arrival_time;
    int base_priority;      // 1 = máxima prioridad, números mayores = menor prioridad
    int dynamic_priority;   // Modificada por envejecimiento (aging)

    std::vector<Burst> bursts;
    size_t current_burst_index;
    int remaining_burst;    // Tiempo restante de la ráfaga actual

    ProcessState state;

    // Métricas de ejecución
    int first_run_time;     // Momento en que la CPU lo ejecuta por primera vez (-1 si no ha ejecutado)
    int completion_time;    // Momento en que finaliza la última ráfaga (-1 si no ha terminado)
    int total_cpu_time;     // Suma total de todas sus ráfagas de CPU
    int total_io_time;      // Suma total de todas sus ráfagas de E/S
    int executed_cpu_time;  // CPU consumida acumulada
    int executed_io_time;   // E/S consumida acumulada
    int waiting_time;       // Tiempo total en estado READY
    int context_switches;   // Veces que fue desalojado de CPU
    int time_in_current_state; // Ticks consecutivos en el estado actual (para aging)

    Process(int pid, const std::string& name, int arrival_time, int priority, const std::vector<Burst>& bursts);

    // Copia profunda para reiniciar la simulación con otro algoritmo
    std::shared_ptr<Process> clone() const;

    bool is_completed() const;
    bool is_current_burst_cpu() const;
    bool is_current_burst_io() const;

    // Ejecuta 1 tick de CPU. Retorna true si la ráfaga actual de CPU finalizó.
    bool execute_cpu_tick(int current_time);

    // Ejecuta 1 tick de E/S. Retorna true si la ráfaga actual de E/S finalizó.
    bool execute_io_tick(int current_time);

    // Pasa a la siguiente ráfaga si existe. Retorna true si aún le quedan ráfagas.
    bool advance_to_next_burst();

    // Tiempo restante de la ráfaga actual de CPU (para SRTF)
    int get_remaining_current_burst() const;

    // Métricas calculadas al finalizar
    int get_turnaround_time() const;
    int get_response_time() const;
};

#endif // PROCESS_HPP
