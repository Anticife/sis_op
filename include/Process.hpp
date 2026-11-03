#ifndef PROCESS_HPP
#define PROCESS_HPP

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include <sys/types.h>
#include "ProcessState.hpp"

enum class BurstType {
    CPU,
    IO
};

struct Burst {
    BurstType type;
    int duration;
};

/**
 * Bloque de memoria compartida POSIX (mapeado vía mmap MAP_SHARED | MAP_ANONYMOUS)
 * entre el proceso planificador padre y cada proceso hijo real creado con fork().
 */
struct SharedProcessControl {
    std::atomic<int> mode;              // 0 = CPU_BURST, 1 = IO_SLEEP, 2 = TERMINATE
    std::atomic<uint64_t> cpu_ops;      // Operaciones reales de CPU ejecutadas por el hijo
    std::atomic<int64_t> child_cpu_ns;  // Tiempo real de CPU medido por CLOCK_PROCESS_CPUTIME_ID en el hijo
    std::atomic<int> io_blocked;        // 1 cuando el hijo está bloqueado en read() sobre la pipe POSIX
};

class Process {
public:
    int pid;                // ID lógico del escenario
    pid_t os_pid;           // PID real en el kernel Linux retornado por fork()
    bool posix_zombie;      // Indica si el proceso hijo ya ejecutó _exit(0) y está en estado Z (Zombie <defunct>)
    bool posix_reaped;      // Indica si wait4() ya recolectó el proceso hijo en el kernel
    std::string name;
    int arrival_time;
    int base_priority;      // 1 = máxima prioridad, números mayores = menor prioridad
    int dynamic_priority;   // Modificada por envejecimiento (aging) y aplicada con setpriority()
    int nice_value;         // Valor nice POSIX real aplicado en el kernel (-20 a 19)

    std::vector<Burst> bursts;
    size_t current_burst_index;
    int remaining_burst;    // Tiempo restante de la ráfaga actual

    ProcessState state;
    char linux_kernel_state; // Estado real leído de /proc/<os_pid>/stat ('R', 'S', 'T', 'Z')
    std::string linux_wchan; // Canal de espera en el kernel leído de /proc/<os_pid>/wchan

    // Descriptores y memoria compartida POSIX reales
    int io_pipe_fd[2];
    SharedProcessControl* shm_ctrl;

    // Métricas de ejecución y telemetría real del kernel Linux (wait4 / getrusage / /proc)
    int first_run_time;     // Momento en que la CPU lo ejecuta por primera vez (-1 si no ha ejecutado)
    int completion_time;    // Momento en que finaliza la última ráfaga (-1 si no ha terminado)
    int total_cpu_time;     // Suma total de todas sus ráfagas de CPU
    int total_io_time;      // Suma total de todas sus ráfagas de E/S
    int executed_cpu_time;  // CPU consumida acumulada (en ticks)
    int executed_io_time;   // E/S consumida acumulada (en ticks)
    int waiting_time;       // Tiempo total en estado READY
    int context_switches;   // Veces que fue desalojado de CPU por el planificador
    int time_in_current_state; // Ticks consecutivos en el estado actual (para aging)

    // Telemetría real del Kernel Linux obtenida mediante wait4() / rusage y clock_gettime()
    double real_user_cpu_ms;        // Tiempo real en modo usuario (ru_utime + CLOCK_PROCESS_CPUTIME_ID)
    double real_sys_cpu_ms;         // Tiempo real en modo kernel (ru_stime)
    long kernel_voluntary_cs;       // Cambios de contexto voluntarios en el kernel (ru_nvcsw)
    long kernel_nonvoluntary_cs;    // Cambios de contexto involuntarios en el kernel (ru_nivcsw)
    long kernel_minor_faults;       // Fallos de página menores en el kernel (ru_minflt)
    uint64_t real_cpu_operations;   // Iteraciones reales de ALU/FPU ejecutadas por el hijo
    int syscalls_executed;          // Contador de llamadas al sistema POSIX realizadas sobre este proceso

    Process(int pid, const std::string& name, int arrival_time, int priority, const std::vector<Burst>& bursts);
    ~Process();

    // Copia profunda para reiniciar la simulación con otro algoritmo
    std::shared_ptr<Process> clone() const;

    bool is_completed() const;
    bool is_current_burst_cpu() const;
    bool is_current_burst_io() const;

    // --- Operaciones con Llamadas al Sistema POSIX Reales ---
    // Crea el proceso real en Linux usando mmap(), pipe(), fork(), sched_setaffinity(), setpriority() y SIGSTOP
    bool spawn_posix_process();

    // Despacha el proceso en el kernel enviando kill(os_pid, SIGCONT)
    bool resume_posix_process();

    // Desaloja el proceso en el kernel enviando kill(os_pid, SIGSTOP) + waitpid(WUNTRACED)
    bool suspend_posix_process();

    // Bloquea el proceso real en una operación read() sobre su pipe POSIX de E/S
    bool enter_io_posix_block();

    // Despierta al proceso real de su bloqueo de E/S mediante write() en la pipe POSIX
    bool wake_io_posix_block();

    // Actualiza la prioridad real en el kernel Linux usando setpriority(PRIO_PROCESS, os_pid, nice)
    bool update_posix_priority(int new_dyn_priority);

    // Lee directamente /proc/<os_pid>/stat y /proc/<os_pid>/wchan mediante open()/read()/close()
    char query_linux_proc_state();

    // Hace que el proceso hijo ejecute _exit(0) y quede en estado ZOMBIE real ('Z' <defunct>) en ps
    // sin recolectarlo aún con wait4(), permitiendo observar el estado Z+ en consola/ps
    bool enter_zombie_posix_state();

    // Finaliza el proceso hijo (si no es zombie aún) y recolecta métricas reales del kernel mediante wait4() y struct rusage
    bool terminate_and_reap_posix();

    // Inicializa el buffer argv[0] del proceso para renombrar los hijos en ps aux / ps --forest
    static void init_argv_buffer(int argc, char** argv);

    // Limpia recursos POSIX (descriptores de pipe y región mmap)
    void cleanup_posix_resources();

    // Ejecuta 1 tick de CPU sobre el proceso real. Retorna true si la ráfaga actual de CPU finalizó.
    bool execute_cpu_tick(int current_time, int tick_duration_ms = 3);

    // Ejecuta 1 tick de E/S sobre el proceso real bloqueado en pipe. Retorna true si la ráfaga de E/S finalizó.
    bool execute_io_tick(int current_time);

    // Pasa a la siguiente ráfaga si existe. Retorna true si aún le quedan ráfagas.
    bool advance_to_next_burst();

    // Tiempo restante de la ráfaga actual de CPU (para SRTF)
    int get_remaining_current_burst() const;

    // Métricas calculadas al finalizar
    int get_turnaround_time() const;
    int get_response_time() const;

    static int priority_to_nice(int priority);
};

#endif // PROCESS_HPP
