#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <iomanip>

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
    std::cout << "========================================================================================\n";
    std::cout << "   SIMULADOR DE GESTION DE PROCESOS Y PLANIFICACION DE CPU - SISTEMAS OPERATIVOS        \n";
    std::cout << "   Modelado de Estados, Desalojo, Efecto Convoy, Inanicion y Costo de Context Switch    \n";
    std::cout << "========================================================================================\n";
    std::cout << "\033[0m";
}

void print_usage(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n"
              << "Opciones:\n"
              << "  --scenario <archivo.csv>    Ruta al archivo CSV con el escenario de prueba\n"
              << "  --algo <fcfs|rr|srtf|priority|all>  Algoritmo a ejecutar (defecto: all)\n"
              << "  --quantum <int>             Quantum de CPU para Round Robin (defecto: 4)\n"
              << "  --cs-cost <int>             Costo en ticks por Cambio de Contexto (defecto: 1)\n"
              << "  --aging-th <int>            Umbral de ticks en READY para Aging (defecto: 5)\n"
              << "  --step                      Ejecutar en modo interactivo paso a paso (tick por tick)\n"
              << "  --export <archivo.csv>      Exportar resultados consolidados a archivo CSV\n"
              << "  --menu                      Abrir el menu interactivo de consola\n"
              << "  --help, -h                  Mostrar esta ayuda\n\n";
}

std::vector<SimulationResults> run_scenario(const std::string& scenario_path,
                                            const std::string& algo_choice,
                                            int quantum,
                                            int cs_cost,
                                            int aging_th,
                                            bool step_mode,
                                            const std::string& export_path = "") {
    auto initial_processes = Simulator::load_from_csv(scenario_path);
    if (initial_processes.empty()) {
        std::cerr << "\033[1;31mError: El escenario no contiene procesos o no se pudo leer.\033[0m\n";
        return {};
    }

    std::cout << "\n\033[1;32m>> Escenario cargado: " << scenario_path 
              << " (" << initial_processes.size() << " procesos detectados)\033[0m\n";

    std::vector<std::shared_ptr<Scheduler>> schedulers_to_run;

    if (algo_choice == "fcfs") {
        schedulers_to_run.push_back(std::make_shared<SchedulerFCFS>());
    } else if (algo_choice == "rr") {
        schedulers_to_run.push_back(std::make_shared<SchedulerRR>(quantum));
    } else if (algo_choice == "srtf") {
        schedulers_to_run.push_back(std::make_shared<SchedulerSRTF>());
    } else if (algo_choice == "priority") {
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
        Simulator sim(sched, cs_cost, step_mode);
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
        std::cout << "\033[1;32m[+] Métricas exportadas exitosamente a: " << export_path << "\033[0m\n";
    }

    return results_list;
}

void interactive_menu() {
    int choice = 0;
    while (true) {
        print_banner();
        std::cout << "SELECCIONE UNA OPCION:\n";
        std::cout << "  1. Ejecutar Escenario 1: Carga Mixta y Efecto Convoy (FCFS vs RR vs SRTF)\n";
        std::cout << "  2. Ejecutar Escenario 2: Procesos con Ciclo Completo de E/S (READY <-> WAITING)\n";
        std::cout << "  3. Ejecutar Escenario 3: Riesgo de Inanición vs. Envejecimiento (Aging)\n";
        std::cout << "  4. Ejecutar Simulación Paso a Paso (Interactivo con Gantt en vivo)\n";
        std::cout << "  5. Ejecutar archivo CSV personalizado\n";
        std::cout << "  6. Salir\n";
        std::cout << "Opcion [1-6]: ";

        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::getline(std::cin, discard);
            continue;
        }

        if (choice == 6) {
            std::cout << "Saliendo del simulador...\n";
            break;
        }

        std::string scenario = "scenarios/escenario1_convoy.csv";
        std::string algo = "all";
        int quantum = 4;
        int cs_cost = 1;
        int aging_th = 5;
        bool step = false;
        std::string export_file = "";

        if (choice == 1) {
            scenario = "scenarios/escenario1_convoy.csv";
            export_file = "docs/resultados_escenario1.csv";
        } else if (choice == 2) {
            scenario = "scenarios/escenario2_io_bursts.csv";
            export_file = "docs/resultados_escenario2.csv";
        } else if (choice == 3) {
            scenario = "scenarios/escenario3_starvation.csv";
            export_file = "docs/resultados_escenario3.csv";
        } else if (choice == 4) {
            std::cout << "\nSeleccione escenario para el modo paso a paso:\n";
            std::cout << "  1. Escenario 1 (Convoy)\n  2. Escenario 2 (E/S)\n  3. Escenario 3 (Inanición)\nOpcion: ";
            int sc_choice = 1;
            std::cin >> sc_choice;
            if (sc_choice == 2) scenario = "scenarios/escenario2_io_bursts.csv";
            else if (sc_choice == 3) scenario = "scenarios/escenario3_starvation.csv";
            else scenario = "scenarios/escenario1_convoy.csv";

            std::cout << "Seleccione algoritmo (1: FCFS, 2: Round Robin, 3: SRTF, 4: Prioridad con Aging): ";
            int al_choice = 2;
            std::cin >> al_choice;
            if (al_choice == 1) algo = "fcfs";
            else if (al_choice == 2) algo = "rr";
            else if (al_choice == 3) algo = "srtf";
            else algo = "priority";

            step = true;
        } else if (choice == 5) {
            std::cout << "Ingrese la ruta del archivo CSV: ";
            std::cin >> scenario;
        }

        run_scenario(scenario, algo, quantum, cs_cost, aging_th, step, export_file);

        std::cout << "\nPresione [Enter] para volver al menú principal...";
        std::string dummy;
        std::getline(std::cin, dummy);
        std::getline(std::cin, dummy);
    }
}

int main(int argc, char* argv[]) {
    std::string scenario = "scenarios/escenario1_convoy.csv";
    std::string algo = "all";
    int quantum = 4;
    int cs_cost = 1;
    int aging_th = 5;
    bool step_mode = false;
    bool force_menu = false;
    std::string export_path = "";

    if (argc <= 1) {
        interactive_menu();
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--menu") {
            force_menu = true;
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--algo" && i + 1 < argc) {
            algo = argv[++i];
        } else if (arg == "--quantum" && i + 1 < argc) {
            quantum = std::stoi(argv[++i]);
        } else if (arg == "--cs-cost" && i + 1 < argc) {
            cs_cost = std::stoi(argv[++i]);
        } else if (arg == "--aging-th" && i + 1 < argc) {
            aging_th = std::stoi(argv[++i]);
        } else if (arg == "--step") {
            step_mode = true;
        } else if (arg == "--export" && i + 1 < argc) {
            export_path = argv[++i];
        }
    }

    if (force_menu) {
        interactive_menu();
        return 0;
    }

    print_banner();
    run_scenario(scenario, algo, quantum, cs_cost, aging_th, step_mode, export_path);

    return 0;
}
