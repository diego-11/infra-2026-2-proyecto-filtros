CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fopenmp
BIN      := motor comparar generar arnes

all: $(BIN)

motor: src/main.cpp src/filtros.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

comparar: tests/comparar.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

generar: bench/generar.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

arnes: bench/arnes.cpp src/filtros.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

pruebas: all
	bash tests/pruebas.sh

# El flujo de integración continua del enunciado invoca `make test`; el
# objetivo es el mismo que `make pruebas` para no duplicar la lista de pasos.
test: pruebas

clean:
	rm -f $(BIN)

.PHONY: all pruebas test clean
