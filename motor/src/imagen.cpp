#include "imagen.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

// La cabecera es texto: el número mágico, el ancho, el alto y el valor
// máximo, separados por espacios en blanco. Entre ellos puede haber líneas
// que empiezan con almohadilla, que son comentarios y se descartan.
std::string campo(std::istream& e) {
    std::string s;
    while (e >> s) {
        if (s.empty() || s[0] != '#') return s;
        e.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    throw std::runtime_error("cabecera incompleta");
}

}  // namespace

Imagen leer_flujo(std::istream& e) {
    const std::string magico = campo(e);
    Imagen img;
    if (magico == "P6") {
        img.canales = 3;
    } else if (magico == "P5") {
        img.canales = 1;
    } else {
        throw std::runtime_error("no es P5 ni P6");
    }

    img.ancho = std::stoi(campo(e));
    img.alto = std::stoi(campo(e));
    const int maximo = std::stoi(campo(e));
    if (img.ancho <= 0 || img.alto <= 0) throw std::runtime_error("tamaño inválido");
    if (maximo != 255) throw std::runtime_error("el valor máximo tiene que ser 255");

    // Después del valor máximo viene un solo carácter de separación, y en
    // seguida los bytes de los píxeles en crudo.
    e.get();
    const std::size_t total = static_cast<std::size_t>(img.ancho) * img.alto * img.canales;
    img.datos.resize(total);
    e.read(reinterpret_cast<char*>(img.datos.data()), static_cast<std::streamsize>(total));
    if (static_cast<std::size_t>(e.gcount()) != total) {
        throw std::runtime_error("faltan bytes de píxeles");
    }
    return img;
}

Imagen leer(const std::string& ruta) {
    std::ifstream e(ruta, std::ios::binary);
    if (!e) throw std::runtime_error("no se puede abrir " + ruta);
    try {
        return leer_flujo(e);
    } catch (const std::exception& error) {
        throw std::runtime_error(ruta + ": " + error.what());
    }
}

void escribir_flujo(std::ostream& s, const Imagen& img) {
    s << (img.canales == 3 ? "P6" : "P5") << "\n"
      << img.ancho << " " << img.alto << "\n255\n";
    s.write(reinterpret_cast<const char*>(img.datos.data()),
            static_cast<std::streamsize>(img.datos.size()));
    if (!s) throw std::runtime_error("la escritura falló");
}

void escribir(const std::string& ruta, const Imagen& img) {
    std::ofstream s(ruta, std::ios::binary);
    if (!s) throw std::runtime_error("no se puede escribir " + ruta);
    try {
        escribir_flujo(s, img);
    } catch (const std::exception& error) {
        throw std::runtime_error(ruta + ": " + error.what());
    }
}
