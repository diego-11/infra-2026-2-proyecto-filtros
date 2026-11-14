#include "filtros.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

Imagen gris_secuencial(const Imagen& e) {
    if (e.canales != 3) throw std::runtime_error("gris espera una imagen de tres canales");
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0) + 0.587 * e.en(x, y, 1) + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }
    return s;
}

Imagen gris_paralelo(const Imagen& e) {
    if (e.canales != 3) throw std::runtime_error("gris espera una imagen de tres canales");
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);
    // Cada fila es independiente de las demás: no hay nada que compartir
    // entre hilos ni dependencias entre iteraciones.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0) + 0.587 * e.en(x, y, 1) + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }
    return s;
}

namespace {

// El promedio de la vecindad de 3x3 de un canal, con las coordenadas
// recortadas al rectángulo de la imagen.
double promedio_vecindad(const Imagen& e, int x, int y, int c) {
    double suma = 0.0;
    for (int j = -1; j <= 1; ++j) {
        const int fila = recortar(y + j, e.alto);
        for (int i = -1; i <= 1; ++i) {
            suma += e.en(recortar(x + i, e.ancho), fila, c);
        }
    }
    return suma / 9.0;
}

}  // namespace

Imagen caja_secuencial(const Imagen& e) {
    Imagen s = e;
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c, redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }
    return s;
}

Imagen caja_paralelo(const Imagen& e) {
    Imagen s = e;
    // El ciclo externo recorre filas y el interno columnas, que es el orden
    // en que la imagen está en memoria. Repartir por filas deja a cada hilo
    // con una banda contigua.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c, redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// Desenfoque gaussiano separable
//
// El núcleo binomial [1 4 6 4 1]/16 se aplica primero en horizontal sobre la
// entrada y después en vertical sobre el resultado intermedio T, que se guarda
// en double. Los pesos son múltiplos exactos de 1/16 y los píxeles son
// enteros, así que todas las sumas son exactas en double: el orden en que se
// sumen no cambia el resultado, y la forma directa de 5x5 da los mismos bytes.

namespace {

constexpr double K[5] = {1.0 / 16, 4.0 / 16, 6.0 / 16, 4.0 / 16, 1.0 / 16};

// Pasada horizontal de una fila: T(x, y) = sum_i k_i I(x+i, y), con la
// columna recortada al borde. Escribe ancho*canales valores en `fila_t`.
void horizontal_fila(const Imagen& e, int y, double* fila_t) {
    const int c_total = e.canales;
    for (int x = 0; x < e.ancho; ++x) {
        for (int c = 0; c < c_total; ++c) {
            double suma = 0.0;
            for (int i = -2; i <= 2; ++i) {
                suma += K[i + 2] * e.en(recortar(x + i, e.ancho), y, c);
            }
            fila_t[static_cast<std::size_t>(x) * c_total + c] = suma;
        }
    }
}

// Pasada vertical de una fila de salida: O(x, y) = redondeo(sum_j k_j T(x, y+j)).
// `filas[j]` apunta a la fila de T que corresponde a y+j-2, ya recortada.
void vertical_fila(Imagen& s, int y, const double* const filas[5]) {
    const std::size_t n = static_cast<std::size_t>(s.ancho) * s.canales;
    unsigned char* fila_s = s.datos.data() + static_cast<std::size_t>(y) * n;
    for (std::size_t k = 0; k < n; ++k) {
        double suma = 0.0;
        for (int j = 0; j < 5; ++j) suma += K[j] * filas[j][k];
        fila_s[k] = redondear(suma);
    }
}

// Las dos pasadas con el arreglo intermedio completo. `paralelo` decide si
// cada pasada reparte las filas entre los hilos.
Imagen gaussiano_dos_pasadas(const Imagen& e, bool paralelo) {
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    std::vector<double> t(n * e.alto);
    Imagen s = e;

    // Pasada horizontal: cada fila de T depende solo de la misma fila de la
    // entrada, así que las filas se reparten sin coordinación.
    #pragma omp parallel for schedule(static) if (paralelo)
    for (int y = 0; y < e.alto; ++y) {
        horizontal_fila(e, y, t.data() + static_cast<std::size_t>(y) * n);
    }

    // La pasada vertical lee filas de T que pudo escribir otro hilo; la
    // barrera implícita al final del for anterior garantiza que ya están.
    #pragma omp parallel for schedule(static) if (paralelo)
    for (int y = 0; y < e.alto; ++y) {
        const double* filas[5];
        for (int j = 0; j < 5; ++j) {
            filas[j] = t.data() + static_cast<std::size_t>(recortar(y + j - 2, e.alto)) * n;
        }
        vertical_fila(s, y, filas);
    }
    return s;
}

}  // namespace

Imagen gaussiano_secuencial(const Imagen& e) { return gaussiano_dos_pasadas(e, false); }

Imagen gaussiano_paralelo(const Imagen& e) { return gaussiano_dos_pasadas(e, true); }

Imagen gaussiano_directo_secuencial(const Imagen& e) {
    Imagen s = e;
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                double suma = 0.0;
                for (int j = -2; j <= 2; ++j) {
                    const int fila = recortar(y + j, e.alto);
                    for (int i = -2; i <= 2; ++i) {
                        suma += K[j + 2] * K[i + 2] * e.en(recortar(x + i, e.ancho), fila, c);
                    }
                }
                s.poner(x, y, c, redondear(suma));
            }
        }
    }
    return s;
}
// ---------------------------------------------------------------------------
// Sobel y magnitud del gradiente
//
// Las dos máscaras se evalúan en el mismo recorrido: la vecindad de 3x3 se lee
// una sola vez y de ella salen gx y gy. Las sumas son de enteros, y la raíz
// se toma en double y se redondea y satura con redondear(), que ya hace el
// mín(255, ·) del enunciado.

namespace {

void sobel_fila(const Imagen& e, Imagen& s, int y) {
    const int w = e.ancho;
    const unsigned char* arriba = e.datos.data() + static_cast<std::size_t>(recortar(y - 1, e.alto)) * w;
    const unsigned char* centro = e.datos.data() + static_cast<std::size_t>(y) * w;
    const unsigned char* abajo = e.datos.data() + static_cast<std::size_t>(recortar(y + 1, e.alto)) * w;
    unsigned char* fila_s = s.datos.data() + static_cast<std::size_t>(y) * w;
    for (int x = 0; x < w; ++x) {
        const int xm = recortar(x - 1, w);
        const int xp = recortar(x + 1, w);
        const int gx = (arriba[xp] - arriba[xm]) + 2 * (centro[xp] - centro[xm]) + (abajo[xp] - abajo[xm]);
        const int gy = (abajo[xm] + 2 * abajo[x] + abajo[xp]) - (arriba[xm] + 2 * arriba[x] + arriba[xp]);
        fila_s[x] = redondear(std::sqrt(static_cast<double>(gx) * gx + static_cast<double>(gy) * gy));
    }
}

void exigir_un_canal(const Imagen& e, const char* filtro) {
    if (e.canales != 1) throw std::runtime_error(std::string(filtro) + " espera una imagen de un canal");
}

}  // namespace

Imagen sobel_secuencial(const Imagen& e) {
    exigir_un_canal(e, "sobel");
    Imagen s = e;
    for (int y = 0; y < e.alto; ++y) sobel_fila(e, s, y);
    return s;
}

Imagen sobel_paralelo(const Imagen& e) {
    exigir_un_canal(e, "sobel");
    Imagen s = e;
    // Stencil: cada fila de salida lee tres filas de entrada y escribe solo
    // la suya. El costo por fila es el mismo en toda la imagen, así que el
    // reparto estático en bandas contiguas es el que menos cuesta.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) sobel_fila(e, s, y);
    return s;
}

Imagen otsu_secuencial(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}

Imagen otsu_paralelo(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}
