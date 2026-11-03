#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "Process.hpp"
#include "Scheduler.hpp"
#include "SchedulerFCFS.hpp"
#include "SchedulerRR.hpp"
#include "SchedulerSRTF.hpp"
#include "SchedulerPriority.hpp"
#include "Simulator.hpp"
#include "Metrics.hpp"

void print_banner() {
    std::cout << "\033[1;36m";
    std::cout << "========================================================================================================\n";
    std::cout << "   PLANIFICADOR Y GESTOR DE PROCESOS POSIX REAL EN LINUX (C++17 / KERNEL SYSCALLS)                      \n";
    std::cout << "   Llamadas al Sistema Reales: fork(), kill(SIGSTOP/SIGCONT), wait4(), pipe(), mmap(), setpriority()    \n";
    std::cout << "   PID del Demonio Planificador Padre en Linux: " << getpid() << " (PPID=" << getppid() << ")\n";
    std::cout << "========================================================================================================\n";
    std::cout << "\033[0m";
}

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n"
              << "Opciones:\n"
              << "  --scenario <archivo.csv>    Ruta al archivo CSV con el escenario de prueba\n"
              << "  --algo <fcfs|rr|srtf|priority_strict|priority|all>  Algoritmo a ejecutar (defecto: all)\n"
              << "  --quantum <int>             Quantum de CPU para Round Robin (defecto: 4)\n"
              << "  --cs-cost <int>             Costo en ticks por Cambio de Contexto (defecto: 1)\n"
              << "  --aging-th <int>            Umbral de ticks en READY para Aging (defecto: 5)\n"
              << "  --tick-ms <int>             Duracion fisica real en ms de cada tick POSIX (defecto: 3, en --live: 1000)\n"
              << "  --live                      Ejecutar en tiempo real (1s/tick) mostrando estados en 'ps --forest'\n"
              << "  --live-state <archivo.json> Ruta de estado JSON en tiempo real (defecto: /tmp/simulador_live_state.json)\n"
              << "  --step                      Ejecutar en modo interactivo paso a paso (tick por tick)\n"
              << "  --export <archivo.csv>      Exportar resultados y auditoria POSIX a archivo CSV y JSON\n"
              << "  --export-json <archivo.json> Exportar traza POSIX detallada en JSON\n"
              << "  --menu                      Abrir el menu interactivo de consola\n"
              << "  --help, -h                  Mostrar esta ayuda\n\n";
}

std::vector<SimulationResults> run_scenario(const std::string& scenario_path,
                                            const std::string& algo_choice,
                                            int quantum,
                                            int cs_cost,
                                            int aging_th,
                                            bool step_mode,
                                            const std::string& export_path = "",
                                            int tick_ms = 3,
                                            const std::string& export_json_path = "",
                                            bool live_mode = false,
                                            const std::string& live_state_path = "/tmp/simulador_live_state.json") {
    auto initial_processes = Simulator::load_from_csv(scenario_path);
    if (initial_processes.empty()) {
        std::cerr << "\033[1;31mError: El escenario no contiene procesos o no se pudo leer.\033[0m\n";
        return {};
    }

    std::cout << "\n\033[1;32m>> Escenario cargado: " << scenario_path
              << " (" << initial_processes.size() << " procesos a instanciar mediante fork() en el kernel Linux)\033[0m\n";

    std::vector<std::shared_ptr<Scheduler>> schedulers_to_run;

    if (algo_choice == "fcfs") {
        schedulers_to_run.push_back(std::make_shared<SchedulerFCFS>());
    } else if (algo_choice == "rr") {
        schedulers_to_run.push_back(std::make_shared<SchedulerRR>(quantum));
    } else if (algo_choice == "srtf") {
        schedulers_to_run.push_back(std::make_shared<SchedulerSRTF>());
    } else if (algo_choice == "priority_strict") {
        schedulers_to_run.push_back(std::make_shared<SchedulerPriority>(false, aging_th));
    } else if (algo_choice == "priority" || algo_choice == "priority_aging") {
        schedulers_to_run.push_back(std::make_shared<SchedulerPriority>(true, aging_th));
    } else { // "all"
        schedulers_to_run.push_back(std::make_shared<SchedulerFCFS>());
        schedulers_to_run.push_back(std::make_shared<SchedulerRR>(quantum));
        schedulers_to_run.push_back(std::make_shared<SchedulerSRTF>());
        schedulers_to_run.push_back(std::make_shared<SchedulerPriority>(false, aging_th)); // Prioridad Estricta
        schedulers_to_run.push_back(std::make_shared<SchedulerPriority>(true, aging_th));  // Prioridad con Aging
    }

    std::vector<SimulationResults> results_list;

    for (auto& sched : schedulers_to_run) {
        Simulator sim(sched, cs_cost, step_mode, tick_ms, live_mode, live_state_path);
        sim.load_processes(initial_processes);
        auto res = sim.run();

        std::cout << Metrics::render_gantt(res.gantt_chart);
        std::cout << Metrics::render_table(res);
        results_list.push_back(res);
    }

    if (results_list.size() > 1) {
        std::cout << Metrics::render_comparison_table(results_list);
    }

    if (!export_path.empty()) {
        Metrics::export_csv(export_path, results_list);
        std::cout << "\033[1;32m[+] Métricas y auditoría de syscalls POSIX exportadas a: " << export_path << "\033[0m\n";

        std::string json_file = export_json_path;
        if (json_file.empty() && export_path.size() > 4 && export_path.substr(export_path.size() - 4) == ".csv") {
            json_file = export_path.substr(0, export_path.size() - 4) + ".json";
        }
        if (!json_file.empty()) {
            Metrics::export_json_trace(json_file, results_list);
        }
    } else if (!export_json_path.empty()) {
        Metrics::export_json_trace(export_json_path, results_list);
    }

    return results_list;
}

void interactive_menu() {
    int choice = 0;
    while (true) {
        print_banner();
        std::cout << "SELECCIONE UNA OPCION:\n";
        std::cout << "  1. [MODO EN VIVO ps --forest] Demostración Real en Consola (1 seg/tick: R+, S+, T+, Z+ <defunct>)\n";
        std::cout << "  2. Ejecutar Escenario 1: Carga Mixta y Efecto Convoy (Todos los algoritmos)\n";
        std::cout << "  3. Ejecutar Escenario 2: Procesos con Bloqueo Real en Pipe E/S (READY <-> WAITING)\n";
        std::cout << "  4. Ejecutar Escenario 3: Inanición vs. Envejecimiento con setpriority() Real\n";
        std::cout << "  5. Ejecutar Simulación Paso a Paso con Enter (Inspección interactiva de ps y /proc)\n";
        std::cout << "  6. Salir\n";
        std::cout << "Opcion [1-6]: ";

        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::getline(std::cin, discard);
            continue;
        }

        if (choice == 6) {
            std::cout << "Saliendo del planificador POSIX...\n";
            break;
        }

        std::string scenario = "scenarios/escenario2_io_bursts.csv";
        std::string algo = "rr";
        int quantum = 4;
        int cs_cost = 1;
        int aging_th = 5;
        int tick_ms = 3;
        bool step = false;
        bool live = false;
        std::string export_file = "";

        if (choice == 1) {
            std::cout << "\nSeleccione escenario para observar en tiempo real con 'ps':\n";
            std::cout << "  1. Escenario 1 (Convoy CPU-Bound)\n"
                      << "  2. Escenario 2 (Ciclo Completo: R+ en CPU, S+ en Pipe E/S, T+ en SIGSTOP, Z+ Zombie)\n"
                      << "  3. Escenario 3 (Inanición vs Aging con setpriority nice)\nOpcion [defecto 2]: ";
            int sc_choice = 2;
            std::cin >> sc_choice;
            if (sc_choice == 1) scenario = "scenarios/escenario1_convoy.csv";
            else if (sc_choice == 3) scenario = "scenarios/escenario3_starvation.csv";
            else scenario = "scenarios/escenario2_io_bursts.csv";

            std::cout << "Seleccione algoritmo (1: FCFS, 2: Round Robin, 3: SRTF, 4: Prioridad Estricta, 5: Prioridad con Aging): ";
            int al_choice = 2;
            std::cin >> al_choice;
            if (al_choice == 1) algo = "fcfs";
            else if (al_choice == 2) algo = "rr";
            else if (al_choice == 3) algo = "srtf";
            else if (al_choice == 4) algo = "priority_strict";
            else algo = "priority_aging";

            tick_ms = 1000; // 1 segundo real por tick para observar en ps
            live = true;
        } else if (choice == 2) {
            scenario = "scenarios/escenario1_convoy.csv";
            algo = "all";
            export_file = "docs/resultados_escenario1.csv";
        } else if (choice == 3) {
            scenario = "scenarios/escenario2_io_bursts.csv";
            algo = "all";
            export_file = "docs/resultados_escenario2.csv";
        } else if (choice == 4) {
            scenario = "scenarios/escenario3_starvation.csv";
            algo = "all";
            export_file = "docs/resultados_escenario3.csv";
        } else if (choice == 5) {
            scenario = "scenarios/escenario2_io_bursts.csv";
            algo = "rr";
            tick_ms = 100;
            step = true;
        }

        run_scenario(scenario, algo, quantum, cs_cost, aging_th, step, export_file, tick_ms, "", live);

        std::cout << "\nPresione [Enter] para volver al menú principal...";
        std::string dummy;
        std::getline(std::cin, dummy);
        std::getline(std::cin, dummy);
    }
}

int main(int argc, char* argv[]) {
    // Asegurar espacio suficiente en el bloque argv para que los hijos puedan renombrar su título en 'ps'
    if ( std::getenv("SIM_ARGV_PADDED") == nullptr ) {
        setenv("SIM_ARGV_PADDED", "1", 1);
        std::vector<char*> new_argv;
        for (int i = 0; i < argc; ++i) {
            new_argv.push_back(argv[i]);
        }
        char pad_arg[] = "--ps-pad=________________________________________________________________";
        new_argv.push_back(pad_arg);
        new_argv.push_back(nullptr);
        execvp(argv[0], new_argv.data());
    }

    Process::init_argv_buffer(argc, argv);

    std::string scenario = "scenarios/escenario2_io_bursts.csv";
    std::string algo = "all";
    int quantum = 4;
    int cs_cost = 1;
    int aging_th = 5;
    int tick_ms = -1;
    bool step_mode = false;
    bool live_mode = false;
    bool force_menu = false;
    std::string export_path = "";
    std::string export_json_path = "";
    std::string live_state_path = "/tmp/simulador_live_state.json";

    int real_args = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--ps-pad=", 0) == 0) {
            // Limpiar el argumento de relleno en el proceso padre para que 'ps' se vea limpio
            std::memset(argv[i], 0, std::strlen(argv[i]));
            continue;
        }
        real_args++;
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--menu") {
            force_menu = true;
        } else if (arg == "--live") {
            live_mode = true;
        } else if (arg == "--live-state" && i + 1 < argc) {
            live_state_path = argv[++i];
            real_args++;
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario = argv[++i];
            real_args++;
        } else if (arg == "--algo" && i + 1 < argc) {
            algo = argv[++i];
            real_args++;
        } else if (arg == "--quantum" && i + 1 < argc) {
            quantum = std::stoi(argv[++i]);
            real_args++;
        } else if (arg == "--cs-cost" && i + 1 < argc) {
            cs_cost = std::stoi(argv[++i]);
            real_args++;
        } else if (arg == "--aging-th" && i + 1 < argc) {
            aging_th = std::stoi(argv[++i]);
            real_args++;
        } else if (arg == "--tick-ms" && i + 1 < argc) {
            tick_ms = std::stoi(argv[++i]);
            real_args++;
        } else if (arg == "--step") {
            step_mode = true;
        } else if (arg == "--export" && i + 1 < argc) {
            export_path = argv[++i];
            real_args++;
        } else if (arg == "--export-json" && i + 1 < argc) {
            export_json_path = argv[++i];
            real_args++;
        }
    }

    if (real_args == 0 || force_menu) {
        interactive_menu();
        return 0;
    }

    if (live_mode) {
        if (tick_ms < 0) tick_ms = 1000; // 1 segundo real por tick por defecto en modo --live
        if (algo == "all") algo = "rr";  // En modo --live corremos el algoritmo seleccionado en tiempo real
    } else {
        if (tick_ms < 0) tick_ms = 3;
    }

    print_banner();
    run_scenario(scenario, algo, quantum, cs_cost, aging_th, step_mode, export_path, tick_ms, export_json_path, live_mode, live_state_path);

    return 0;
}
