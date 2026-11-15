#!/usr/bin/env bash
# Campaña de medición del motor: 1, 2, 4 y 8 hilos, cinco repeticiones por
# configuración, y la tabla completa en mediciones/motor.csv.
#
#   bash bench/medir_completo.sh medicion.ppm medicion.pgm [salida.csv]
#
# El PPM es la imagen de color de 4096x4096 del generador. El PGM es su
# versión en grises (motor gris), que es la entrada de Sobel, Otsu y la
# ecualización. La primera corrida de cada configuración la descarta el arnés
# al resumir; el CSV conserva las cinco para que el resumen sea reproducible.
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "uso: bash bench/medir_completo.sh <medicion.ppm> <medicion.pgm> [salida.csv]" >&2
    exit 2
fi

ppm="$1"
pgm="$2"
salida="${3:-../mediciones/motor.csv}"
repeticiones="${REPETICIONES:-5}"

echo "filtro,hilos,repeticion,milisegundos" > "$salida"

medir() {
    local filtro="$1"
    local imagen="$2"
    for hilos in 1 2 4 8; do
        OMP_NUM_THREADS="$hilos" ./arnes "$filtro" "$imagen" "$repeticiones" >> "$salida"
    done
    echo "  medido $filtro" >&2
}

echo "Filtros de color sobre $ppm:" >&2
for filtro in gris caja gaussiano gaussiano_bloques cadena; do
    medir "$filtro" "$ppm"
done

echo "Filtros de un canal sobre $pgm:" >&2
for filtro in sobel otsu ecualizar otsu_compartido; do
    medir "$filtro" "$pgm"
done

# La forma directa de 5x5 es secuencial: se mide solo con un hilo, que es la
# comparación contra la separable de un hilo.
echo "Forma directa 5x5 (un hilo):" >&2
OMP_NUM_THREADS=1 ./arnes gaussiano_directo "$ppm" "$repeticiones" >> "$salida"
echo "  medido gaussiano_directo" >&2

echo "escrito $salida con $(( $(wc -l < "$salida") - 1 )) filas"
