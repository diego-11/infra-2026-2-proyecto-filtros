#ifndef IMAGEN_HPP
#define IMAGEN_HPP

#include <iosfwd>
#include <string>
#include <vector>

// Una imagen en memoria: los bytes van por filas, de arriba hacia abajo, y
// dentro de cada fila un byte por componente. Un PPM de color tiene tres
// canales en orden R G B; un PGM en escala de grises tiene uno.
struct Imagen {
    int ancho = 0;
    int alto = 0;
    int canales = 0;
    std::vector<unsigned char> datos;

    unsigned char en(int x, int y, int c) const {
        return datos[(static_cast<std::size_t>(y) * ancho + x) * canales + c];
    }
    void poner(int x, int y, int c, unsigned char v) {
        datos[(static_cast<std::size_t>(y) * ancho + x) * canales + c] = v;
    }
};

// Lee un PPM binario (P6) o un PGM binario (P5). El valor máximo tiene que
// ser 255; cualquier otra cosa es un error. Las variantes sobre flujos son
// las que usa el trabajador: la imagen llega y sale por una tubería, sin
// archivos temporales.
Imagen leer(const std::string& ruta);
Imagen leer_flujo(std::istream& e);

// Escribe P6 si la imagen tiene tres canales y P5 si tiene uno.
void escribir(const std::string& ruta, const Imagen& img);
void escribir_flujo(std::ostream& s, const Imagen& img);

// La vecindad de un píxel del borde se sale de la imagen, y entonces se usa
// el píxel del borde que le corresponde.
inline int recortar(int v, int limite) {
    if (v < 0) return 0;
    if (v >= limite) return limite - 1;
    return v;
}

// Las sumas ponderadas se acumulan en doble precisión; el entero sale al
// final, redondeado al más cercano con los empates hacia arriba y saturado.
inline unsigned char redondear(double v) {
    double r = v + 0.5;
    if (r <= 0.0) return 0;
    if (r >= 256.0) return 255;
    return static_cast<unsigned char>(r);
}

#endif
