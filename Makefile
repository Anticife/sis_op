CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -MMD -MP -pthread -Iinclude
CXXFLAGS_DEBUG = -std=c++17 -Wall -Wextra -g -MMD -MP -pthread -Iinclude

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
TARGET = simulador

SRCS = $(SRC_DIR)/Process.cpp \
       $(SRC_DIR)/SchedulerFCFS.cpp \
       $(SRC_DIR)/SchedulerRR.cpp \
       $(SRC_DIR)/SchedulerSRTF.cpp \
       $(SRC_DIR)/SchedulerPriority.cpp \
       $(SRC_DIR)/CPU.cpp \
       $(SRC_DIR)/IOQueue.cpp \
       $(SRC_DIR)/Metrics.cpp \
       $(SRC_DIR)/Simulator.cpp \
       $(SRC_DIR)/main.cpp

OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)

.PHONY: all clean test debug run pdf plots web live monitor

all: $(TARGET)

-include $(DEPS)

$(TARGET): $(OBJS)
	@echo "Enlazando binario POSIX $(TARGET)..."
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)
	@echo "Compilación exitosa: $(TARGET)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@echo "Compilando $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

debug: CXXFLAGS = $(CXXFLAGS_DEBUG)
debug: clean all

clean:
	@echo "Limpiando archivos objeto y binarios..."
	rm -rf $(BUILD_DIR) $(TARGET)

test: all
	@echo "Ejecutando suite automatizada con llamadas al sistema POSIX reales sobre los 3 escenarios..."
	./$(TARGET) --scenario scenarios/escenario1_convoy.csv --algo all --export docs/resultados_escenario1.csv
	./$(TARGET) --scenario scenarios/escenario2_io_bursts.csv --algo all --export docs/resultados_escenario2.csv
	./$(TARGET) --scenario scenarios/escenario3_starvation.csv --algo all --export docs/resultados_escenario3.csv
	@echo "Todas las pruebas POSIX completadas. Resultados y auditoría guardados en docs/."

plots:
	@echo "Generando visualizaciones gráficas y diagramas de Gantt desde métricas POSIX reales..."
	@mkdir -p docs/graficas
	python3 scripts/plot_metrics.py
	@echo "Gráficas generadas exitosamente en docs/graficas/."

web: all
	@echo "Iniciando servidor interactivo conectado al binario POSIX ./simulador en http://localhost:8080 ..."
	python3 scripts/live_server.py

live: all
	@echo "Ejecutando ./simulador en modo TIEMPO REAL (1 seg/tick) para observar procesos en 'ps --forest'..."
	./$(TARGET) --live --scenario scenarios/escenario2_io_bursts.csv --algo rr --tick-ms 1000

monitor:
	watch -n 0.5 "ps -C simulador -o pid,ppid,stat,ni,pcpu,wchan:18,etime,comm,args --forest || true"

pdf:
	@echo "Compilando informe técnico en PDF..."
	python3 scripts/generate_pdf.py docs/informe_tecnico.md docs/informe_tecnico.pdf

run: all
	./$(TARGET)
