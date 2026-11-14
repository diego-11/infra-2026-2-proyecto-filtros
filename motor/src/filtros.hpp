#ifndef FILTROS_HPP
#define FILTROS_HPP

#include "imagen.hpp"

// Escala de grises con la ponderación BT.601. Entra un PPM de color y sale
// un PGM de un canal. Cada píxel de salida depende solo del píxel de entrada
// en la misma posición.
Imagen gris_secuencial(const Imagen& e);
Imagen gris_paralelo(const Imagen& e);

// Desenfoque de caja: cada píxel pasa a ser el promedio de los nueve de su
// vecindad de 3x3. Trabaja igual sobre uno o tres canales.
Imagen caja_secuencial(const Imagen& e);
Imagen caja_paralelo(const Imagen& e);

// Desenfoque gaussiano separable con el núcleo binomial [1 4 6 4 1]/16, en
// una pasada horizontal y otra vertical sobre el resultado intermedio.
Imagen gaussiano_secuencial(const Imagen& e);
Imagen gaussiano_paralelo(const Imagen& e);

// Magnitud del gradiente de Sobel sobre una imagen de un canal.
Imagen sobel_secuencial(const Imagen& e);
Imagen sobel_paralelo(const Imagen& e);

// Umbral de Otsu: busca el umbral que maximiza la varianza entre clases del
// histograma y binariza con él.
Imagen otsu_secuencial(const Imagen& e);
Imagen otsu_paralelo(const Imagen& e);

#endif
// Ecualización del histograma sobre una imagen de un canal: redistribuye las
// intensidades con el histograma acumulado para levantar el contraste.
Imagen ecualizar_secuencial(const Imagen& e);
Imagen ecualizar_paralelo(const Imagen& e);

// ---------------------------------------------------------------------------
// Variantes que solo existen para medir. No hacen parte de la cadena del
// servicio: el arnés las llama para comparar una decisión contra otra.

// Gaussiano en su forma directa: una máscara de 5x5 por píxel (25
// multiplicaciones), contra la que se compara la forma separable (10).
Imagen gaussiano_directo_secuencial(const Imagen& e);

// Otsu paralelo con un único histograma compartido protegido con atomic. Es
// la versión ingenua contra la que se mide la de histogramas por hilo.
Imagen otsu_compartido_paralelo(const Imagen& e);

// Los 256 contadores de intensidad de una imagen de un canal. La versión
// paralela usa un histograma privado por hilo y los suma al final; la
// compartida usa un solo arreglo con incrementos atómicos.
using Histograma = long long[256];
void histograma_secuencial(const Imagen& e, Histograma h);
void histograma_paralelo(const Imagen& e, Histograma h);
void histograma_compartido(const Imagen& e, Histograma h);

// El umbral t* de Otsu a partir de un histograma. Es el barrido de los 255
// umbrales: cuesta nada frente al recorrido de la imagen y queda secuencial.
int umbral_otsu(const Histograma h, long long total);
