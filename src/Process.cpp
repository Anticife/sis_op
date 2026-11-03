#include "Process.hpp"
#include <stdexcept>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <cstring>
#include <cstdio>

static char* g_argv0_ptr = nullptr;
static size_t g_argv_total_len = 0;

void Process::init_argv_buffer(int argc, char** argv) {
    if (argc > 0 && argv && argv[0]) {
        g_argv0_ptr = argv[0];
        char* end_ptr = argv[argc - 1] + std::strlen(argv[argc - 1]);
        if (end_ptr > g_argv0_ptr) {
            g_argv_total_len = static_cast<size_t>(end_ptr - g_argv0_ptr);
        } else {
            g_argv_total_len = std::strlen(argv[0]);
        }
    }
}

int Process::priority_to_nice(int priority) {
    // Mapea prioridad lógica (1 = máxima, 5+ = baja) al rango POSIX nice (0 a 19 para usuarios sin root)
    int nice_val = (priority - 1) * 4;
    if (nice_val < 0) nice_val = 0;
    if (nice_val > 19) nice_val = 19;
    return nice_val;
}

Process::Process(int pid, const std::string& name, int arrival_time, int priority, const std::vector<Burst>& bursts)
    : pid(pid),
      os_pid(-1),
      posix_zombie(false),
      posix_reaped(false),
      name(name),
      arrival_time(arrival_time),
      base_priority(priority),
      dynamic_priority(priority),
      nice_value(priority_to_nice(priority)),
      bursts(bursts),
      current_burst_index(0),
      state(ProcessState::NEW),
      linux_kernel_state('N'),
      linux_wchan("-"),
      shm_ctrl(nullptr),
      first_run_time(-1),
      completion_time(-1),
      total_cpu_time(0),
      total_io_time(0),
      executed_cpu_time(0),
      executed_io_time(0),
      waiting_time(0),
      context_switches(0),
      time_in_current_state(0),
      real_user_cpu_ms(0.0),
      real_sys_cpu_ms(0.0),
      kernel_voluntary_cs(0),
      kernel_nonvoluntary_cs(0),
      kernel_minor_faults(0),
      real_cpu_operations(0),
      syscalls_executed(0) {

    io_pipe_fd[0] = -1;
    io_pipe_fd[1] = -1;

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

Process::~Process() {
    cleanup_posix_resources();
}

void Process::cleanup_posix_resources() {
    if (os_pid > 0 && !posix_reaped) {
        // Asegurar que ningún proceso hijo quede vivo o zombie si se destruye el objeto prematuramente
        kill(os_pid, SIGKILL);
        int status = 0;
        waitpid(os_pid, &status, 0);
        posix_reaped = true;
    }
    if (io_pipe_fd[0] != -1) {
        close(io_pipe_fd[0]);
        io_pipe_fd[0] = -1;
    }
    if (io_pipe_fd[1] != -1) {
        close(io_pipe_fd[1]);
        io_pipe_fd[1] = -1;
    }
    if (shm_ctrl != nullptr && shm_ctrl != MAP_FAILED) {
        munmap(shm_ctrl, sizeof(SharedProcessControl));
        shm_ctrl = nullptr;
    }
}

std::shared_ptr<Process> Process::clone() const {
    auto copy = std::make_shared<Process>(pid, name, arrival_time, base_priority, bursts);
    return copy;
}

bool Process::spawn_posix_process() {
    if (os_pid > 0) return true;

    // 1. Llamada al sistema mmap() para memoria compartida entre Padre (Planificador) e Hijo
    void* mem = mmap(nullptr, sizeof(SharedProcessControl),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    syscalls_executed++;
    if (mem == MAP_FAILED) {
        return false;
    }
    shm_ctrl = static_cast<SharedProcessControl*>(mem);
    shm_ctrl->mode.store(0, std::memory_order_relaxed);
    shm_ctrl->cpu_ops.store(0, std::memory_order_relaxed);
    shm_ctrl->child_cpu_ns.store(0, std::memory_order_relaxed);
    shm_ctrl->io_blocked.store(0, std::memory_order_relaxed);

    // 2. Llamada al sistema pipe() para el bloqueo real de Entrada/Salida (read/write)
    if (pipe(io_pipe_fd) == -1) {
        munmap(shm_ctrl, sizeof(SharedProcessControl));
        shm_ctrl = nullptr;
        return false;
    }
    syscalls_executed++;

    // Tubería de sincronización inicial para que el hijo notifique cuando fijó su afinidad de CPU
    int sync_pipe[2];
    if (pipe(sync_pipe) == -1) {
        cleanup_posix_resources();
        return false;
    }
    syscalls_executed++;

    // 3. Llamada al sistema fork() para crear el proceso hijo real en el kernel Linux
    pid_t child = fork();
    syscalls_executed++;

    if (child < 0) {
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        cleanup_posix_resources();
        return false;
    }

    if (child == 0) {
        // ================= CONTEXTO DEL PROCESO HIJO REAL EN LINUX =================
        // Si el proceso padre muere por cualquier señal (ej. SIGINT, SIGPIPE), terminar automáticamente al hijo
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        close(sync_pipe[0]);
        close(io_pipe_fd[1]); // El hijo solo lee de la tubería de E/S para bloquearse

        // Fijar nombre de comando en el kernel a "simulador" para que 'ps -C simulador --forest' agrupe padre e hijos
        prctl(PR_SET_NAME, "simulador", 0, 0, 0);

        // Sobrescribir argv[0] para que en 'ps aux' y 'ps -o args --forest' se identifique el proceso hijo
        if (g_argv0_ptr && g_argv_total_len > 0) {
            std::memset(g_argv0_ptr, 0, g_argv_total_len);
            std::snprintf(g_argv0_ptr, g_argv_total_len, "simulador:P%d_%s", pid, name.c_str());
        }

        // Fijar afinidad a la CPU 0 mediante sched_setaffinity() para competencia real por un núcleo
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

        // Avisar al planificador padre que el proceso hijo está inicializado
        char ready_byte = 'R';
        if (write(sync_pipe[1], &ready_byte, 1) < 0) {
            _exit(1);
        }
        close(sync_pipe[1]);

        // Si existe el archivo real subido en uploads/<name>, abrirlo con open() para procesar sus bytes reales
        std::string upload_path = "uploads/" + name;
        int file_fd = open(upload_path.c_str(), O_RDONLY);
        unsigned char file_buf[512];

        // Bucle de trabajo real del proceso hijo gobernado por señales POSIX (SIGSTOP/SIGCONT) y pipe
        volatile uint64_t hash_accum = 0x9E3779B97F4A7C15ULL ^ static_cast<uint64_t>(getpid());
        while (true) {
            int current_mode = shm_ctrl->mode.load(std::memory_order_acquire);
            if (current_mode == 2) {
                // Orden de terminación: salir limpiamente con syscall _exit(0)
                break;
            } else if (current_mode == 1) {
                // Modo Bloqueo de E/S: realizar syscall read() bloqueante sobre la pipe del kernel
                shm_ctrl->io_blocked.store(1, std::memory_order_release);
                char io_token = 0;
                ssize_t n = read(io_pipe_fd[0], &io_token, 1);
                shm_ctrl->io_blocked.store(0, std::memory_order_release);
                if (n <= 0 && shm_ctrl->mode.load(std::memory_order_acquire) == 2) {
                    break;
                }
            } else {
                // Modo Ráfaga de CPU: procesar bytes reales del archivo subido (si existe) y ejecutar operaciones de hash
                if (file_fd >= 0) {
                    ssize_t rbytes = read(file_fd, file_buf, sizeof(file_buf));
                    if (rbytes <= 0) {
                        lseek(file_fd, 0, SEEK_SET);
                    } else {
                        for (ssize_t b = 0; b < rbytes; ++b) {
                            hash_accum = (hash_accum ^ file_buf[b]) * 0x100000001b3ULL;
                        }
                    }
                }
                for (int i = 0; i < 2500; ++i) {
                    hash_accum ^= (hash_accum << 13);
                    hash_accum ^= (hash_accum >> 7);
                    hash_accum += 0xBF58476D1CE4E5B9ULL;
                }
                shm_ctrl->cpu_ops.fetch_add(2500, std::memory_order_relaxed);

                struct timespec ts_cpu;
                if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_cpu) == 0) {
                    int64_t ns = static_cast<int64_t>(ts_cpu.tv_sec) * 1000000000LL + ts_cpu.tv_nsec;
                    shm_ctrl->child_cpu_ns.store(ns, std::memory_order_relaxed);
                }
            }
        }

        if (file_fd >= 0) close(file_fd);
        close(io_pipe_fd[0]);
        _exit(0);
    }

    // ================= CONTEXTO DEL PLANIFICADOR PADRE =================
    os_pid = child;
    close(sync_pipe[1]);

    // Esperar confirmación de inicialización del hijo mediante read()
    char ack = 0;
    if (read(sync_pipe[0], &ack, 1) < 0) {
        // Ignorar error menor
    }
    close(sync_pipe[0]);
    syscalls_executed++;

    // 4. Llamada al sistema setpriority() para asignar el valor nice POSIX en el kernel
    nice_value = priority_to_nice(dynamic_priority);
    setpriority(PRIO_PROCESS, static_cast<id_t>(os_pid), nice_value);
    syscalls_executed++;

    // 5. Suspender inmediatamente al proceso recién creado con kill(os_pid, SIGSTOP) + waitpid(WUNTRACED)
    // para que permanezca en la cola de LISTOS ('T' Stopped en el kernel) hasta que la CPU lo despache
    suspend_posix_process();

    return true;
}

bool Process::resume_posix_process() {
    if (os_pid <= 0) return false;
    shm_ctrl->mode.store(0, std::memory_order_release);

    // Enviar señal real SIGCONT al proceso en el kernel Linux
    if (kill(os_pid, SIGCONT) == 0) {
        syscalls_executed++;
        query_linux_proc_state();
        return true;
    }
    return false;
}

bool Process::suspend_posix_process() {
    if (os_pid <= 0) return false;

    // Enviar señal real SIGSTOP para desalojar al proceso de la CPU física
    if (kill(os_pid, SIGSTOP) == 0) {
        syscalls_executed++;
        int status = 0;
        // Esperar confirmación del kernel de que el proceso entró en estado STOPPED
        waitpid(os_pid, &status, WUNTRACED);
        syscalls_executed++;
        query_linux_proc_state();
        return true;
    }
    return false;
}

bool Process::enter_io_posix_block() {
    if (os_pid <= 0 || !shm_ctrl) return false;

    // Indicar al proceso hijo que ejecute read() bloqueante sobre su pipe de E/S
    shm_ctrl->mode.store(1, std::memory_order_release);
    kill(os_pid, SIGCONT);
    syscalls_executed++;

    // Esperar brevemente a que el proceso hijo entre en la syscall read() bloqueante en el kernel
    struct timespec req = {0, 500000L}; // 0.5 ms
    for (int tries = 0; tries < 10; ++tries) {
        if (shm_ctrl->io_blocked.load(std::memory_order_acquire) == 1) {
            break;
        }
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
    }
    query_linux_proc_state(); // En /proc/<os_pid>/stat ahora aparece 'S' (Interruptible Sleep)
    return true;
}

bool Process::wake_io_posix_block() {
    if (os_pid <= 0 || !shm_ctrl || io_pipe_fd[1] == -1) return false;

    shm_ctrl->mode.store(0, std::memory_order_release);
    char wake_token = 'I';
    // Escribir 1 byte en la pipe POSIX para despertar al hijo de su read() bloqueante
    if (write(io_pipe_fd[1], &wake_token, 1) == 1) {
        syscalls_executed++;
    }

    // Una vez despierto de E/S, pasa a estado READY (detenido hasta que el planificador lo despache)
    suspend_posix_process();
    return true;
}

bool Process::update_posix_priority(int new_dyn_priority) {
    dynamic_priority = new_dyn_priority;
    nice_value = priority_to_nice(dynamic_priority);
    if (os_pid > 0) {
        if (setpriority(PRIO_PROCESS, static_cast<id_t>(os_pid), nice_value) == 0) {
            syscalls_executed++;
            return true;
        }
    }
    return false;
}

char Process::query_linux_proc_state() {
    if (os_pid <= 0 || posix_reaped) return linux_kernel_state;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/stat", static_cast<int>(os_pid));

    // Llamadas al sistema POSIX open(), read(), close() sobre el pseudo-sistema de archivos /proc
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        syscalls_executed += 3;

        if (n > 0) {
            buf[n] = '\0';
            // El estado del proceso en /proc/<pid>/stat está justo después del último paréntesis ')'
            const char* rparen = std::strrchr(buf, ')');
            if (rparen && rparen[1] == ' ' && rparen[2] != '\0') {
                linux_kernel_state = rparen[2];
            }
        }
    }

    // Leer también /proc/<os_pid>/wchan para conocer la función del kernel donde duerme/espera
    char wpath[64];
    std::snprintf(wpath, sizeof(wpath), "/proc/%d/wchan", static_cast<int>(os_pid));
    int wfd = open(wpath, O_RDONLY);
    if (wfd >= 0) {
        char wbuf[64];
        ssize_t wn = read(wfd, wbuf, sizeof(wbuf) - 1);
        close(wfd);
        syscalls_executed += 3;
        if (wn > 0) {
            wbuf[wn] = '\0';
            // Limpiar saltos de línea
            for (ssize_t i = 0; i < wn; ++i) {
                if (wbuf[i] == '\n' || wbuf[i] == '\r') {
                    wbuf[i] = '\0';
                    break;
                }
            }
            if (std::strcmp(wbuf, "0") == 0 || wbuf[0] == '\0') {
                linux_wchan = "-";
            } else {
                linux_wchan = wbuf;
            }
        }
    }

    if (linux_kernel_state == 'R' || linux_kernel_state == 'Z') {
        linux_wchan = "-";
    } else if (linux_kernel_state == 'T' && linux_wchan == "-") {
        linux_wchan = "do_signal_stop";
    } else if (linux_kernel_state == 'S' && linux_wchan == "-") {
        linux_wchan = "pipe_read";
    }

    return linux_kernel_state;
}

bool Process::enter_zombie_posix_state() {
    if (os_pid <= 0 || posix_reaped || posix_zombie) return false;

    if (shm_ctrl) {
        shm_ctrl->mode.store(2, std::memory_order_release);
        real_cpu_operations = shm_ctrl->cpu_ops.load(std::memory_order_relaxed);
        real_user_cpu_ms = static_cast<double>(shm_ctrl->child_cpu_ns.load(std::memory_order_relaxed)) / 1000000.0;
    }

    // Desbloqueamos cualquier read() de E/S y enviamos SIGCONT para que el hijo ejecute _exit(0)
    // SIN llamar aún a wait4(), dejando el proceso en estado ZOMBIE real ('Z' <defunct>) en ps
    if (io_pipe_fd[1] != -1) {
        char end_byte = 'X';
        if (write(io_pipe_fd[1], &end_byte, 1) < 0) {}
        close(io_pipe_fd[1]);
        io_pipe_fd[1] = -1;
    }
    kill(os_pid, SIGCONT);
    syscalls_executed++;

    struct timespec req = {0, 1000000L}; // 1 ms
    for (int tries = 0; tries < 25; ++tries) {
        if (query_linux_proc_state() == 'Z') {
            break;
        }
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
    }

    posix_zombie = true;
    linux_kernel_state = 'Z';
    linux_wchan = "-";
    return true;
}

bool Process::terminate_and_reap_posix() {
    if (os_pid <= 0 || posix_reaped) return false;

    if (!posix_zombie) {
        enter_zombie_posix_state();
    }

    // Llamada al sistema wait4() para recolectar el proceso terminado y extraer struct rusage del kernel
    int status = 0;
    struct rusage usage;
    std::memset(&usage, 0, sizeof(usage));

    pid_t ret = wait4(os_pid, &status, 0, &usage);
    syscalls_executed++;

    if (ret == os_pid) {
        double utime_ms = static_cast<double>(usage.ru_utime.tv_sec) * 1000.0 +
                          static_cast<double>(usage.ru_utime.tv_usec) / 1000.0;
        double stime_ms = static_cast<double>(usage.ru_stime.tv_sec) * 1000.0 +
                          static_cast<double>(usage.ru_stime.tv_usec) / 1000.0;

        if (shm_ctrl) {
            double clk_ms = static_cast<double>(shm_ctrl->child_cpu_ns.load()) / 1000000.0;
            real_user_cpu_ms = (clk_ms > utime_ms) ? clk_ms : utime_ms;
            real_cpu_operations = shm_ctrl->cpu_ops.load();
        } else {
            real_user_cpu_ms = utime_ms;
        }
        real_sys_cpu_ms = stime_ms;
        kernel_voluntary_cs = usage.ru_nvcsw;
        kernel_nonvoluntary_cs = usage.ru_nivcsw;
        kernel_minor_faults = usage.ru_minflt;
    }

    posix_reaped = true;
    linux_kernel_state = 'Z';

    if (io_pipe_fd[0] != -1) {
        close(io_pipe_fd[0]);
        io_pipe_fd[0] = -1;
    }
    if (shm_ctrl != nullptr && shm_ctrl != MAP_FAILED) {
        munmap(shm_ctrl, sizeof(SharedProcessControl));
        shm_ctrl = nullptr;
        syscalls_executed++;
    }

    return true;
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

bool Process::execute_cpu_tick(int current_time, int tick_duration_ms) {
    if (first_run_time == -1) {
        first_run_time = current_time;
    }

    // Dar un quantum físico real de procesador al proceso hijo mediante clock_nanosleep()
    if (os_pid > 0 && tick_duration_ms > 0) {
        struct timespec req;
        req.tv_sec = tick_duration_ms / 1000;
        req.tv_nsec = (tick_duration_ms % 1000) * 1000000L;
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
        syscalls_executed++;
        query_linux_proc_state();
        if (shm_ctrl) {
            real_cpu_operations = shm_ctrl->cpu_ops.load(std::memory_order_relaxed);
            real_user_cpu_ms = static_cast<double>(shm_ctrl->child_cpu_ns.load(std::memory_order_relaxed)) / 1000000.0;
        }
    }

    executed_cpu_time++;
    remaining_burst--;

    if (remaining_burst <= 0) {
        return true; // Ráfaga CPU completada
    }
    return false;
}

bool Process::execute_io_tick(int /*current_time*/) {
    if (os_pid > 0) {
        query_linux_proc_state();
    }
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
