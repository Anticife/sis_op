# Simulador de Gestión de Procesos y Planificación de CPU
## Proyecto Final de Sistemas Operativos (Periodo 2610)

Implementación y análisis experimental de mecanismos internos de un sistema operativo, desarrollado en **C++17 puro** conforme a los lineamientos y rúbricas del curso.

---

## 📋 Características Principales

* **Modelado Riguroso del Ciclo de Vida:**
  * Estructura formal del **PCB (Process Control Block)** con registro de prioridades estáticas y dinámicas.
  * Transición determinista entre los 5 estados canónicos: `NEW`, `READY`, `RUNNING`, `WAITING` (E/S) y `TERMINATED`.
* **Cuatro Algoritmos de Planificación Implementados:**
  1. **FCFS (*First-Come, First-Served*):** Línea base no apropiativa. Demuestra cuantitativamente el **Efecto Convoy**.
  2. **Round Robin (RR):** Apropiativo por turnos. Permite parametrizar el *quantum* ($q$) y calcula la sobrecarga por conmutación de contexto (*Context Switch Overhead*).
  3. **SRTF (*Shortest Remaining Time First*):** Apropiativo por ráfaga de CPU restante. Evidencia optimización del tiempo de espera y susceptibilidad a la inanición.
  4. **Prioridades con Envejecimiento (*Aging*):** Apropiativo. Incorpora un mecanismo dinámico de promoción de prioridad para erradicar la inanición en procesos de baja prioridad.
* **Subsistema de Entrada/Salida Concurrente:**
  * Modela la cola de espera de E/S donde los procesos se bloquean y reactivan hacia la cola de listos sin detener el cómputo de otros procesos.
* **Visualización Dinámica en Consola (CLI):**
  * **Diagrama de Gantt** en formato ASCII generado ciclo a ciclo.
  * **Tablas detalladas por proceso** con cálculo exacto de tiempos de retorno, espera, respuesta y cambios de contexto.
  * **Tabla comparativa consolidada** con utilización de CPU, sobrecarga e Índice de Equidad de Jain.
  * **Modo paso a paso interactivo (`--step`):** Permite avanzar tick por tick presionando `[Enter]`, visualizando el estado de la CPU y las colas en tiempo real (ideal para la sustentación frente al docente).
* **Exportación y Documentación:**
  * Exporta los datos a formato `.csv` en `docs/`.
  * Genera el **Informe Técnico en PDF** formateado y estructurado con las 11 secciones obligatorias del curso.

---

## 📁 Estructura del Repositorio

```
sis_op/
├── Makefile                     # Automatización de compilación, pruebas y PDF
├── README.md                    # Guía de usuario y documentación del proyecto
├── docs/
│   ├── informe_tecnico.md       # Informe técnico formal (11 secciones)
│   ├── informe_tecnico.pdf      # Versión compilada en PDF del informe
│   ├── resultados_escenario1.csv # Datos experimentales de Escenario 1
│   ├── resultados_escenario2.csv # Datos experimentales de Escenario 2
│   └── resultados_escenario3.csv # Datos experimentales de Escenario 3
├── scenarios/
│   ├── escenario1_convoy.csv    # Carga heterogénea (Efecto Convoy)
│   ├── escenario2_io_bursts.csv # Procesos con ráfagas intercaladas CPU y E/S
│   └── escenario3_starvation.csv# Flujo continuo de tareas vs proceso de fondo
├── include/
│   ├── ProcessState.hpp         # Estados del proceso y colores ANSI
│   ├── Process.hpp              # Definición de la estructura PCB
│   ├── Scheduler.hpp            # Interfaz base polimórfica de planificación
│   ├── SchedulerFCFS.hpp        # Planificador FCFS
│   ├── SchedulerRR.hpp          # Planificador Round Robin
│   ├── SchedulerSRTF.hpp        # Planificador SRTF
│   ├── SchedulerPriority.hpp    # Planificador con Prioridades y Aging
│   ├── CPU.hpp                  # Modela la CPU y cambios de contexto
│   ├── IOQueue.hpp              # Modela la cola de bloqueo de E/S
│   ├── Metrics.hpp              # Motor de cálculo y formateo de métricas
│   └── Simulator.hpp            # Motor de simulación por reloj discreto
├── src/
│   ├── Process.cpp
│   ├── SchedulerFCFS.cpp
│   ├── SchedulerRR.cpp
│   ├── SchedulerSRTF.cpp
│   ├── SchedulerPriority.cpp
│   ├── CPU.cpp
│   ├── IOQueue.cpp
│   ├── Metrics.cpp
│   ├── Simulator.cpp
│   └── main.cpp
└── scripts/
    └── generate_pdf.py          # Generador de PDF con WeasyPrint
```

---

## 🚀 Compilación y Ejecución

### Requisitos
* Compilador de C++ con soporte para **C++17** (`g++` versión 7 o superior).
* Utilidad `make`.
* *(Opcional para regenerar el PDF)*: Python 3 con `weasyprint` y `markdown`.

### 1. Compilar el proyecto
```bash
make clean && make
```
Esto genera el binario ejecutable optimizado `./simulador`.

### 2. Ejecutar la suite completa de pruebas
```bash
make test
```
Ejecuta automáticamente los 3 escenarios con todos los algoritmos y guarda los resultados en `docs/`.

### 3. Abrir el Menú Interactivo en Consola
```bash
./simulador
# O alternativamente:
make run
```

### 4. Modo por Línea de Comandos (Opciones Avanzadas)
```bash
# Ejecutar un escenario específico con todos los algoritmos
./simulador --scenario scenarios/escenario1_convoy.csv --algo all

# Ejecutar Round Robin con quantum personalizado de 6 ticks
./simulador --scenario scenarios/escenario1_convoy.csv --algo rr --quantum 6

# Ejecutar en Modo Paso a Paso interactivo (ideal para sustentación)
./simulador --scenario scenarios/escenario2_io_bursts.csv --algo rr --step

# Exportar métricas a un archivo CSV personalizado
./simulador --scenario scenarios/escenario3_starvation.csv --algo all --export mis_resultados.csv
```

### 5. Compilar el Informe Técnico a PDF
```bash
make pdf
```
Genera `docs/informe_tecnico.pdf` con maquetación académica, numeración de páginas y tablas formateadas.

---

## 📊 Escenarios de Prueba Diseñados

| Escenario | Archivo | Fenómeno Evaluado | Observación Clave |
| :--- | :--- | :--- | :--- |
| **Escenario 1** | `scenarios/escenario1_convoy.csv` | **Efecto Convoy** | Un proceso batch largo ($35\text{ ticks}$) llega en $t=0$, seguido de 4 peticiones web cortas ($2\text{ ticks}$). FCFS arroja $W_{\text{prom}} = 30.00$, mientras SRTF lo reduce a $5.00$ ticks. |
| **Escenario 2** | `scenarios/escenario2_io_bursts.csv` | **Ciclo con E/S Concurrente** | Procesos con secuencias mixtas `CPU -> E/S -> CPU`. Demuestra el bloqueo a `WAITING` y retorno a `READY`. |
| **Escenario 3** | `scenarios/escenario3_starvation.csv` | **Inanición (*Starvation*) y *Aging*** | Tarea de fondo de baja prioridad compite contra flujo continuo de tareas urgentes. En prioridad estricta sufre inanición; con *Aging* se promueve y culmina con éxito. |

---

## 👥 Criterios de Evaluación Cubiertos
* **SABER:** Explicación matemática de métricas ($T, W, R$), efecto convoy, inanición, compensación del quantum vs. cambio de contexto y equidad de Jain.
* **HACER:** Implementación limpia en C++17 modular, reproducible con `make`, con diagrama de Gantt en consola y pruebas automatizadas.
* **SER:** Trazabilidad completa en Git, análisis ético de riesgos y responsabilidades profesionales en sistemas operativos de misión crítica.
