# Instrucciones de Colaboración para el Equipo

Este documento detalla las tareas asignadas para que el compañero de equipo (y su agente de IA) desarrolle e integre sus aportes al proyecto, garantizando la **trazabilidad en Git** requerida por la rúbrica del curso.

---

## 🛠️ Flujo de Trabajo Git Obligatorio

Para que los aportes queden reflejados individualmente en el historial de GitHub:

1. **Clonar el repositorio:**
   ```bash
   git clone git@github.com:Anticife/sis_op.git
   cd sis_op
   ```
2. **Configurar usuario y correo del compañero:**
   ```bash
   git config user.name "Nombre Completo Compañero"
   git config user.email "correo_companero@dominio.com"
   ```
3. **Crear su propia rama de trabajo:**
   ```bash
   git checkout -b feature/visualizacion-grafica
   ```
4. **Desarrollar, compilar y probar los cambios:**
   ```bash
   make clean && make && make test
   ```
5. **Confirmar y subir a GitHub:**
   ```bash
   git add .
   git commit -m "feat: modulo de graficas de rendimiento y diagrama de gantt visual"
   git push origin feature/visualizacion-grafica
   ```
6. **Abrir Pull Request (PR) en GitHub** hacia la rama `main` para que sea revisado y fusionado.

---

## 📌 Paquete de Trabajo Asignado: Módulo Gráfico y Diapositivas

El compañero desarrollará los siguientes módulos para enriquecer el proyecto y preparar la sustentación:

### 1. Generador Gráfico de Métricas (`scripts/plot_metrics.py`)
* **Objetivo:** Leer automáticamente los archivos `docs/resultados_escenario1.csv`, `docs/resultados_escenario2.csv` y `docs/resultados_escenario3.csv` generados por el simulador en C++.
* **Generar:**
  * `docs/graficas/escenario1_comparativa.png`: Gráfico de barras comparando Tiempo de Retorno ($T$) vs. Tiempo de Espera ($W$) entre FCFS, Round Robin, SRTF y Prioridades.
  * `docs/graficas/escenario1_gantt.png`: Diagrama de Gantt visual con colores por proceso y barras de tiempo.
  * `docs/graficas/escenario3_starvation_vs_aging.png`: Gráfico que evidencie cómo el *Aging* rescata a la tarea de fondo frente a Prioridad Estricta.
  * `docs/graficas/round_robin_sensibilidad_quantum.png`: Curva de variación del *Quantum* ($q \in [1, 15]$) contra el costo de conmutaciones de contexto y el tiempo de espera promedio.
* **Integración:** Agregar la regla `plots` al `Makefile` para que se ejecute con `make plots`.

### 2. Mazo de Diapositivas para la Sustentación de 20 minutos (`docs/presentacion_sustentacion.md` / `.pdf`)
* Diapositivas estructuradas para proyectar al profesor:
  1. Portada e integrantes del equipo.
  2. Planteamiento del problema real (cargas heterogéneas en servidores backend).
  3. Arquitectura del simulador (PCB, CPU, colas de E/S y estados).
  4. Algoritmos evaluados (FCFS, RR, SRTF, Prioridad con Aging).
  5. Evidencia experimental del Efecto Convoy (Resultados Escenario 1).
  6. Evidencia experimental de Inanición y solución por Aging (Resultados Escenario 3).
  7. Conclusiones y decisiones técnicas.

---

## 📋 Prompt para el Agente de IA del Compañero

El compañero solo debe copiar y pegar el siguiente texto en su herramienta de IA:

```markdown
Hola, estoy trabajando en el proyecto final de Sistemas Operativos en el repositorio:
https://github.com/Anticife/sis_op.git (Opción 1: Gestión de procesos y planificación de CPU).

El núcleo del simulador ya está implementado en C++17 en la rama `main`, con soporte para FCFS, Round Robin, SRTF y Prioridades con Aging, y exporta métricas a archivos CSV en `docs/resultados_escenario*.csv`.

Mi tarea en mi rama `feature/visualizacion-grafica` es:
1. Crear un script en Python `scripts/plot_metrics.py` (usando matplotlib / pandas / seaborn) que lea los archivos `docs/resultados_escenario*.csv` y genere en la carpeta `docs/graficas/`:
   - Gráfica de barras comparativa de tiempos de retorno (T) y espera (W).
   - Diagrama de Gantt visual en PNG de alta resolución para los escenarios.
   - Gráfica de impacto de Inanición vs Aging (Escenario 3).
   - Curva de sensibilidad del Quantum en Round Robin vs Context Switch Overhead.
2. Actualizar el `Makefile` para incluir la regla `make plots` que ejecute el script de gráficas.
3. Crear las diapositivas de la sustentación (20 min) en `docs/presentacion_sustentacion.md` estructuradas con los resultados obtenidos.
4. Asegurar que todo se ejecute sin errores y preparar el commit para hacer pull request a `main`.
```
