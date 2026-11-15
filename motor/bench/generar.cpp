// Genera la imagen de medición: un PPM de color del tamaño que se pida, con
// un patrón fijo para que dos corridas produzcan el mismo archivo y las
// mediciones de equipos distintos sean comparables.
#include "imagen.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: generar <salida.ppm> [ancho] [alto] [semilla]\n";
        return 2;
    }
    const std::string salida = argv[1];
    const int ancho = argc > 2 ? std::atoi(argv[2]) : 4096;
    const int alto = argc > 3 ? std::atoi(argv[3]) : 4096;
    // La semilla cambia el patrón sin volverlo aleatorio: la misma semilla
    // produce siempre el mismo archivo. El despachador la usa para que las 64
    // imágenes del lote no sean idénticas.
    const int semilla = argc > 4 ? std::atoi(argv[4]) : 0;
    if (ancho <= 0 || alto <= 0) {
        std::cerr << "generar: tamaño inválido\n";
        return 2;
    }

    Imagen img;
    img.ancho = ancho;
    img.alto = alto;
    img.canales = 3;
    img.datos.resize(static_cast<std::size_t>(ancho) * alto * 3);

    // Dos rampas, un tablero y una textura de grano fino. Las rampas dan
    // zonas suaves donde un desenfoque casi no cambia nada; el tablero y el
    // grano dan bordes y detalle, que es donde Sobel y el histograma tienen
    // algo que encontrar. Con semilla 0 el patrón es el de siempre; las demás
    // semillas desplazan el tablero y el grano para que el lote tenga 64
    // imágenes distintas.
    for (int y = 0; y < alto; ++y) {
        for (int x = 0; x < ancho; ++x) {
            const int rampa_x = ancho > 1 ? x * 255 / (ancho - 1) : 0;
            const int rampa_y = alto > 1 ? y * 255 / (alto - 1) : 0;
            const int tablero = ((x / 64) + (y / 64) + (semilla % 2)) % 2 == 0 ? 40 : 0;
            const int grano = (x * 37 + y * 17 + semilla * 101 + (x * y + semilla * 13) % 29) % 64;
            img.poner(x, y, 0, static_cast<unsigned char>((rampa_x + grano) % 256));
            img.poner(x, y, 1, static_cast<unsigned char>((rampa_y + tablero) % 256));
            img.poner(x, y, 2, static_cast<unsigned char>((rampa_x + rampa_y) / 2));
        }
    }

    try {
        escribir(salida, img);
    } catch (const std::exception& error) {
        std::cerr << "generar: " << error.what() << "\n";
        return 1;
    }
    std::cout << "escrito " << salida << ": " << ancho << "x" << alto << ", "
              << img.datos.size() << " bytes de píxeles\n";
    return 0;
}
