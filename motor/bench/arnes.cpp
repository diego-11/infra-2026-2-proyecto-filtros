// Mide cuánto tarda un filtro sobre una imagen ya cargada en memoria. El
// cronómetro rodea solo la llamada al filtro: la lectura y la escritura del
// archivo quedan fuera.
//
// El número de hilos es el que fije OMP_NUM_THREADS en el entorno, y viaja
// en la salida para que cada fila diga con cuántos se tomó.
#include "filtros.hpp"
#include "imagen.hpp"

#include <omp.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

Imagen aplicar(const std::string& filtro, const Imagen& entrada, bool secuencial) {
    if (filtro == "gris") return secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);
    if (filtro == "caja") return secuencial ? caja_secuencial(entrada) : caja_paralelo(entrada);
    if (filtro == "gaussiano") {
        return secuencial ? gaussiano_secuencial(entrada) : gaussiano_paralelo(entrada);
    }
    if (filtro == "sobel") return secuencial ? sobel_secuencial(entrada) : sobel_paralelo(entrada);
    if (filtro == "otsu") return secuencial ? otsu_secuencial(entrada) : otsu_paralelo(entrada);
    if (filtro == "ecualizar") return secuencial ? ecualizar_secuencial(entrada) : ecualizar_paralelo(entrada);

    // Variantes que solo sirven para comparar decisiones. El cronómetro las
    // mide igual que a las demás.
    if (filtro == "gaussiano_directo") return gaussiano_directo_secuencial(entrada);
    if (filtro == "gaussiano_bloques") return gaussiano_bloques_paralelo(entrada);
    if (filtro == "otsu_compartido") return otsu_compartido_paralelo(entrada);

    // La cadena del lote, en el orden del enunciado. Entra un PPM de color y
    // sale un PGM binario.
    if (filtro == "cadena") {
        Imagen img = secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);
        img = secuencial ? gaussiano_secuencial(img) : gaussiano_paralelo(img);
        img = secuencial ? sobel_secuencial(img) : sobel_paralelo(img);
        img = secuencial ? otsu_secuencial(img) : otsu_paralelo(img);
        return img;
    }
    throw std::runtime_error("filtro desconocido: " + filtro);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "uso: arnes <filtro> <imagen> [repeticiones] [--secuencial]"
                     " [--encabezado]\n";
        return 2;
    }
    const std::string filtro = argv[1];
    const std::string ruta = argv[2];
    int repeticiones = 5;
    bool secuencial = false;
    bool encabezado = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--secuencial") {
            secuencial = true;
        } else if (arg == "--encabezado") {
            encabezado = true;
        } else {
            repeticiones = std::atoi(arg.c_str());
        }
    }
    if (repeticiones < 1) {
        std::cerr << "arnes: repeticiones tiene que ser al menos 1\n";
        return 2;
    }

    try {
        const Imagen entrada = leer(ruta);
        const int hilos = secuencial ? 1 : omp_get_max_threads();
        if (encabezado) std::cout << "filtro,hilos,repeticion,milisegundos\n";

        std::vector<double> medidas;
        for (int r = 1; r <= repeticiones; ++r) {
            const double inicio = omp_get_wtime();
            const Imagen salida = aplicar(filtro, entrada, secuencial);
            const double ms = (omp_get_wtime() - inicio) * 1000.0;
            // La imagen de salida se usa aquí para que el compilador no
            // pueda eliminar la llamada al filtro.
            if (salida.datos.empty()) throw std::runtime_error("el filtro devolvió una imagen vacía");
            medidas.push_back(ms);
            std::cout << filtro << "," << hilos << "," << r << "," << ms << "\n";
        }

        // La primera corrida paga la primera lectura de la imagen desde
        // memoria principal, así que el resumen la deja fuera.
        if (medidas.size() > 1) {
            std::vector<double> utiles(medidas.begin() + 1, medidas.end());
            std::sort(utiles.begin(), utiles.end());
            const std::size_t mitad = utiles.size() / 2;
            const double mediana = utiles.size() % 2 == 1
                                       ? utiles[mitad]
                                       : (utiles[mitad - 1] + utiles[mitad]) / 2.0;
            std::cerr << filtro << " con " << hilos << (hilos == 1 ? " hilo" : " hilos")
                      << ": mediana " << mediana << " ms sobre " << utiles.size()
                      << " corridas\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "arnes: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
