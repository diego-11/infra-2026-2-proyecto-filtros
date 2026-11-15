#include "filtros.hpp"
#include "imagen.hpp"

#include <omp.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* USO =
    "uso: motor <filtros> <entrada> <salida> [--secuencial]\n"
    "  filtros: lista separada por comas de gris, caja, gaussiano, sobel, otsu, ecualizar\n"
    "  entrada o salida pueden ser - para usar la entrada o la salida estándar\n"
    "  --salud: comprueba que el binario puede arrancar e imprime los hilos\n"
    "  ejemplo: motor gris,gaussiano,sobel,otsu entrada.ppm salida.pgm\n";

std::vector<std::string> partir(const std::string& lista) {
    std::vector<std::string> salida;
    std::istringstream flujo(lista);
    std::string pieza;
    while (std::getline(flujo, pieza, ',')) {
        if (!pieza.empty()) salida.push_back(pieza);
    }
    return salida;
}

bool conocido(const std::string& filtro) {
    return filtro == "gris" || filtro == "caja" || filtro == "gaussiano" ||
           filtro == "sobel" || filtro == "otsu" || filtro == "ecualizar" ||
           // Variantes que existen para medir y verificar decisiones.
           filtro == "gaussiano_directo" || filtro == "gaussiano_bloques" ||
           filtro == "otsu_compartido";
}

Imagen aplicar(const std::string& filtro, const Imagen& entrada, bool secuencial) {
    if (filtro == "gris") return secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);
    if (filtro == "caja") return secuencial ? caja_secuencial(entrada) : caja_paralelo(entrada);
    if (filtro == "gaussiano") {
        return secuencial ? gaussiano_secuencial(entrada) : gaussiano_paralelo(entrada);
    }
    if (filtro == "sobel") return secuencial ? sobel_secuencial(entrada) : sobel_paralelo(entrada);
    if (filtro == "otsu") return secuencial ? otsu_secuencial(entrada) : otsu_paralelo(entrada);
    if (filtro == "ecualizar") return secuencial ? ecualizar_secuencial(entrada) : ecualizar_paralelo(entrada);
    if (filtro == "gaussiano_directo") return gaussiano_directo_secuencial(entrada);
    if (filtro == "gaussiano_bloques") return gaussiano_bloques_paralelo(entrada);
    if (filtro == "otsu_compartido") return otsu_compartido_paralelo(entrada);
    throw std::runtime_error("filtro desconocido: " + filtro);
}

// Devuelve el motivo por el que la cadena no se puede aplicar, o una cadena
// vacía si se puede. El trabajador responde 400 con ese motivo.
std::string cadena_invalida(const std::vector<std::string>& filtros, int canales) {
    for (const std::string& f : filtros) {
        if (!conocido(f)) return "filtro desconocido: " + f;
    }
    // Cada filtro necesita cierto número de canales y entrega otro, así que
    // la cadena se recorre llevando la cuenta: el primero que no reciba lo
    // que necesita es el que se nombra en el rechazo. Un error aquí cuesta
    // una petición; el mismo error a mitad de la cadena cuesta el tiempo de
    // los filtros que ya corrieron.
    int actuales = canales;
    for (const std::string& f : filtros) {
        if (f == "gris") {
            if (actuales != 3) return "gris necesita una imagen de tres canales";
            actuales = 1;
        } else if (f == "sobel" || f == "otsu" || f == "ecualizar" || f == "otsu_compartido") {
            if (actuales != 1) return f + " necesita una imagen de un canal";
        }
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (argc == 2 && std::string(argv[1]) == "--salud") {
        // El trabajador usa esta salida en /readyz: si el binario arranca y
        // OpenMP responde, el proceso está listo para recibir imágenes.
        std::cout << "motor ok hilos=" << omp_get_max_threads() << "\n";
        return 0;
    }
    if (argc < 4) {
        std::cerr << USO;
        return 2;
    }
    const std::vector<std::string> filtros = partir(argv[1]);
    const std::string entrada = argv[2];
    const std::string salida = argv[3];
    bool secuencial = false;
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) == "--secuencial") secuencial = true;
    }
    if (filtros.empty()) {
        std::cerr << USO;
        return 2;
    }

    try {
        Imagen img = entrada == "-" ? leer_flujo(std::cin) : leer(entrada);
        const std::string motivo = cadena_invalida(filtros, img.canales);
        if (!motivo.empty()) {
            std::cerr << "motor: " << motivo << "\n";
            return 1;
        }
        for (const std::string& filtro : filtros) {
            img = aplicar(filtro, img, secuencial);
        }
        if (salida == "-") {
            escribir_flujo(std::cout, img);
            std::cout.flush();
        } else {
            escribir(salida, img);
        }
    } catch (const std::exception& error) {
        std::cerr << "motor: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
