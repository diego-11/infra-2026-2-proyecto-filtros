#include "filtros.hpp"

#include <omp.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
// Optimización de localidad del gaussiano
//
// La forma separable escribe el intermedio T completo (ancho*alto doubles) y
// después lo vuelve a leer cinco veces por fila. En 4096x4096 eso son más de
// 130 MB escritos y varios cientos de MB leídos de memoria principal. Aquí la
// pasada vertical se hace por bloques de filas: para cada bloque se calcula
// solo el horizontal de las filas que hacen falta (el bloque más dos de
// halo), de modo que ese intermedio cabe en caché y no se recorre la imagen
// entera dos veces. El halo se recalcula, que es trabajo redundante pequeño a
// cambio de no tocar memoria principal. La salida debe ser idéntica a la de
// la versión separable: el horizontal no cambia y el vertical suma los mismos
// cinco valores.

namespace {

// 32 filas de un PPM de 4096 píxeles son ~3.4 MB de intermedio, un tamaño que
// evita recorrer los 384 MB del arreglo completo sin pagar demasiado halo.
constexpr int BLOQUE_FILAS = 32;

}  // namespace

Imagen gaussiano_bloques_paralelo(const Imagen& e) {
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    Imagen s = e;
    const int bloques = (e.alto + BLOQUE_FILAS - 1) / BLOQUE_FILAS;

    #pragma omp parallel
    {
        // Un intermedio privado por hilo, reutilizado en todos los bloques
        // que le toquen. BLOQUE_FILAS + 4 filas cubren el bloque y su halo.
        std::vector<double> t(static_cast<std::size_t>(BLOQUE_FILAS + 4) * n);

        #pragma omp for schedule(static)
        for (int b = 0; b < bloques; ++b) {
            const int y0 = b * BLOQUE_FILAS;
            const int y1 = std::min(e.alto, y0 + BLOQUE_FILAS);
            const int base = std::max(0, y0 - 2);
            const int fin = std::min(e.alto - 1, y1 + 1);

            for (int y = base; y <= fin; ++y) {
                horizontal_fila(e, y, t.data() + static_cast<std::size_t>(y - base) * n);
            }
            for (int y = y0; y < y1; ++y) {
                const double* filas[5];
                for (int j = 0; j < 5; ++j) {
                    const int fy = recortar(y + j - 2, e.alto);
                    filas[j] = t.data() + static_cast<std::size_t>(fy - base) * n;
                }
                vertical_fila(s, y, filas);
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

// ---------------------------------------------------------------------------
// Histograma, ecualización y umbral de Otsu
//
// El histograma es una reducción: todos los píxeles escriben en el mismo
// arreglo de 256 contadores. La versión secuencial recorre la imagen una vez;
// la paralela le da un histograma privado a cada hilo y los suma al final; la
// compartida incrementa un solo arreglo con atomic, que es la versión ingenua
// que serializa el recorrido. El barrido de los 255 umbrales de Otsu queda
// secuencial porque cuesta nada frente a la imagen.

namespace {

// Cada histograma privado ocupa 256 contadores (2048 bytes, 32 líneas de
// caché) más 8 contadores de relleno. El relleno separa un histograma del
// siguiente por una línea completa, de modo que dos hilos no comparten línea
// al escribir el último contador de uno y el primero del otro (false sharing).
constexpr int PASO_HISTOGRAMA = 256 + 8;

void exigir_imagen(const Imagen& e, const char* filtro) {
    if (e.ancho <= 0 || e.alto <= 0) throw std::runtime_error(std::string(filtro) + ": imagen vacía");
}

// Aplica un umbral binario: 0 si el píxel es menor o igual a t, 255 si no.
void binarizar(const Imagen& e, Imagen& s, int t, bool paralelo) {
    const int ancho = e.ancho;
    #pragma omp parallel for schedule(static) if (paralelo)
    for (int y = 0; y < e.alto; ++y) {
        const unsigned char* fila_e = e.datos.data() + static_cast<std::size_t>(y) * ancho;
        unsigned char* fila_s = s.datos.data() + static_cast<std::size_t>(y) * ancho;
        for (int x = 0; x < ancho; ++x) fila_s[x] = fila_e[x] <= t ? 0 : 255;
    }
}

// Construye la tabla de la ecualización a partir del histograma. El primer
// acumulado distinto de cero es el mínimo de la imagen; restarlo deja el nivel
// más bajo en 0 y el más alto en 255.
void tabla_ecualizacion(const Histograma h, long long total, unsigned char lut[256]) {
    long long acumulado[256];
    long long c = 0;
    for (int v = 0; v < 256; ++v) {
        c += h[v];
        acumulado[v] = c;
    }
    long long cmin = 0;
    for (int v = 0; v < 256; ++v) {
        if (acumulado[v] > 0) {
            cmin = acumulado[v];
            break;
        }
    }
    const long long rango = total - cmin;
    for (int v = 0; v < 256; ++v) {
        if (rango <= 0) {
            lut[v] = 0;
        } else {
            const double factor = static_cast<double>(acumulado[v] - cmin) / static_cast<double>(rango);
            lut[v] = redondear(factor * 255.0);
        }
    }
}

}  // namespace

void histograma_secuencial(const Imagen& e, Histograma h) {
    exigir_un_canal(e, "histograma");
    exigir_imagen(e, "histograma");
    for (int v = 0; v < 256; ++v) h[v] = 0;
    for (std::size_t i = 0; i < e.datos.size(); ++i) ++h[e.datos[i]];
}

void histograma_paralelo(const Imagen& e, Histograma h) {
    exigir_un_canal(e, "histograma");
    exigir_imagen(e, "histograma");
    for (int v = 0; v < 256; ++v) h[v] = 0;

    const int hilos = omp_get_max_threads();
    // Los histogramas privados viven en un solo arreglo, uno por hilo y con
    // relleno entre ellos para que no compartan línea de caché.
    std::vector<long long> parciales(static_cast<std::size_t>(hilos) * PASO_HISTOGRAMA, 0);

    #pragma omp parallel
    {
        long long* local = parciales.data() + static_cast<std::size_t>(omp_get_thread_num()) * PASO_HISTOGRAMA;
        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            const unsigned char* fila = e.datos.data() + static_cast<std::size_t>(y) * e.ancho;
            for (int x = 0; x < e.ancho; ++x) ++local[fila[x]];
        }
        // La barrera implícita del for anterior garantiza que ya nadie escribe
        // en `parciales`; la suma se reparte entre los hilos.
        #pragma omp for schedule(static)
        for (int v = 0; v < 256; ++v) {
            long long suma = 0;
            for (int t = 0; t < hilos; ++t) {
                suma += parciales[static_cast<std::size_t>(t) * PASO_HISTOGRAMA + v];
            }
            h[v] = suma;
        }
    }
}

void histograma_compartido(const Imagen& e, Histograma h) {
    exigir_un_canal(e, "histograma");
    exigir_imagen(e, "histograma");
    for (int v = 0; v < 256; ++v) h[v] = 0;
    // Versión ingenua: todos los píxeles compiten por los mismos 256
    // contadores. El atomic protege cada incremento, pero serializa el
    // recorrido y es la que sirve de comparación contra los privados.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        const unsigned char* fila = e.datos.data() + static_cast<std::size_t>(y) * e.ancho;
        for (int x = 0; x < e.ancho; ++x) {
            #pragma omp atomic
            ++h[fila[x]];
        }
    }
}

int umbral_otsu(const Histograma h, long long total) {
    if (total <= 0) return 0;

    double suma_total = 0.0;
    for (int v = 0; v < 256; ++v) suma_total += static_cast<double>(v) * h[v];

    double peso0 = 0.0;
    double suma0 = 0.0;
    double mejor = -1.0;
    int mejor_t = 0;
    // El enunciado barre 0 <= t < 255: el último nivel no puede quedar de un
    // lado y dejar la otra clase vacía.
    for (int t = 0; t < 255; ++t) {
        peso0 += static_cast<double>(h[t]);
        suma0 += static_cast<double>(t) * h[t];
        const double peso1 = static_cast<double>(total) - peso0;
        if (peso0 <= 0.0 || peso1 <= 0.0) continue;
        const double mu0 = suma0 / peso0;
        const double mu1 = (suma_total - suma0) / peso1;
        const double varianza = peso0 * peso1 * (mu0 - mu1) * (mu0 - mu1);
        if (varianza > mejor) {
            mejor = varianza;
            mejor_t = t;
        }
    }
    return mejor_t;
}

Imagen otsu_secuencial(const Imagen& e) {
    exigir_un_canal(e, "otsu");
    exigir_imagen(e, "otsu");
    Histograma h;
    histograma_secuencial(e, h);
    const int t = umbral_otsu(h, static_cast<long long>(e.ancho) * e.alto);
    Imagen s = e;
    binarizar(e, s, t, false);
    return s;
}

Imagen otsu_paralelo(const Imagen& e) {
    exigir_un_canal(e, "otsu");
    exigir_imagen(e, "otsu");
    Histograma h;
    histograma_paralelo(e, h);
    // El barrido de los umbrales se deja secuencial: son 255 iteraciones
    // frente a los ancho*alto píxeles del histograma.
    const int t = umbral_otsu(h, static_cast<long long>(e.ancho) * e.alto);
    Imagen s = e;
    binarizar(e, s, t, true);
    return s;
}

Imagen otsu_compartido_paralelo(const Imagen& e) {
    exigir_un_canal(e, "otsu");
    exigir_imagen(e, "otsu");
    Histograma h;
    histograma_compartido(e, h);
    const int t = umbral_otsu(h, static_cast<long long>(e.ancho) * e.alto);
    Imagen s = e;
    binarizar(e, s, t, true);
    return s;
}

Imagen ecualizar_secuencial(const Imagen& e) {
    exigir_un_canal(e, "ecualizar");
    exigir_imagen(e, "ecualizar");
    Histograma h;
    histograma_secuencial(e, h);
    unsigned char lut[256];
    tabla_ecualizacion(h, static_cast<long long>(e.ancho) * e.alto, lut);

    Imagen s = e;
    for (std::size_t i = 0; i < e.datos.size(); ++i) s.datos[i] = lut[e.datos[i]];
    return s;
}

Imagen ecualizar_paralelo(const Imagen& e) {
    exigir_un_canal(e, "ecualizar");
    exigir_imagen(e, "ecualizar");
    Histograma h;
    histograma_paralelo(e, h);
    unsigned char lut[256];
    tabla_ecualizacion(h, static_cast<long long>(e.ancho) * e.alto, lut);

    Imagen s = e;
    const int ancho = e.ancho;
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        const unsigned char* fila_e = e.datos.data() + static_cast<std::size_t>(y) * ancho;
        unsigned char* fila_s = s.datos.data() + static_cast<std::size_t>(y) * ancho;
        for (int x = 0; x < ancho; ++x) fila_s[x] = lut[fila_e[x]];
    }
    return s;
}
