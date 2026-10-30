CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude
CXXFLAGS_DEBUG = -std=c++17 -Wall -Wextra -g -Iinclude

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

.PHONY: all clean test debug run pdf

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Enlazando binario $(TARGET)..."
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
	rm -rf $(BUILD_DIR) $(TARGET) docs/*.csv

test: all
	@echo "Ejecutando suite automatizada de pruebas sobre los 3 escenarios..."
	./$(TARGET) --scenario scenarios/escenario1_convoy.csv --algo all --export docs/resultados_escenario1.csv
	./$(TARGET) --scenario scenarios/escenario2_io_bursts.csv --algo all --export docs/resultados_escenario2.csv
	./$(TARGET) --scenario scenarios/escenario3_starvation.csv --algo all --export docs/resultados_escenario3.csv
	@echo "Todas las pruebas completadas. Resultados guardados en docs/."

pdf:
	@echo "Compilando informe técnico en PDF..."
	/home/azmuth/miniconda3/bin/python scripts/generate_pdf.py docs/informe_tecnico.md docs/informe_tecnico.pdf

run: all
	./$(TARGET)
