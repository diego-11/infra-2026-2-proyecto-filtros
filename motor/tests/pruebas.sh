#!/usr/bin/env bash
# Pruebas de corrección del motor. Cada filtro se compara contra la salida
# que el repositorio trae calculada, y después la versión paralela contra la
# secuencial sobre una imagen grande, donde el reparto entre hilos sí tiene
# efecto. La comparación es byte a byte y no admite tolerancia.
set -uo pipefail

cd "$(dirname "$0")/.."
fallos=0
trabajo=$(mktemp -d)
trap 'rm -rf "$trabajo"' EXIT

revisar() {
    local nombre="$1"
    shift
    if "$@" > "$trabajo/salida.txt" 2>&1; then
        echo "  ok    $nombre"
    else
        echo "  FALLA $nombre"
        sed 's/^/        /' "$trabajo/salida.txt"
        fallos=$((fallos + 1))
    fi
}

# La versión paralela tiene que dar exactamente lo mismo que la secuencial.
paralelo_contra_secuencial() {
    local filtro="$1"
    local entrada="$2"
    local hilos="$3"
    OMP_NUM_THREADS="$hilos" ./motor "$filtro" "$entrada" "$trabajo/par" 2> "$trabajo/par.err"
    ./motor "$filtro" "$entrada" "$trabajo/sec" --secuencial 2> "$trabajo/sec.err"
    revisar "$filtro idéntico con $hilos hilo(s)" ./comparar "$trabajo/par" "$trabajo/sec"
}

echo "Salida esperada, calculada aparte:"
./motor gris tests/datos/entrada.ppm "$trabajo/gris.pgm"
revisar "gris sobre la imagen de prueba" ./comparar "$trabajo/gris.pgm" tests/datos/gris.pgm
./motor caja tests/datos/entrada.ppm "$trabajo/caja.ppm"
revisar "caja sobre la imagen de prueba" ./comparar "$trabajo/caja.ppm" tests/datos/caja.ppm
./motor gaussiano tests/datos/entrada.ppm "$trabajo/gaussiano.ppm"
revisar "gaussiano separable sobre la imagen de prueba" ./comparar "$trabajo/gaussiano.ppm" tests/datos/gaussiano.ppm
./motor sobel tests/datos/gris.pgm "$trabajo/sobel.pgm"
revisar "Sobel sobre la imagen de prueba" ./comparar "$trabajo/sobel.pgm" tests/datos/sobel.pgm
./motor otsu tests/datos/gris.pgm "$trabajo/otsu.pgm"
revisar "Otsu sobre la imagen de prueba" ./comparar "$trabajo/otsu.pgm" tests/datos/otsu.pgm
./motor ecualizar tests/datos/gris.pgm "$trabajo/ecualizar.pgm"
revisar "ecualización sobre la imagen de prueba" ./comparar "$trabajo/ecualizar.pgm" tests/datos/ecualizar.pgm

echo "Lectura y escritura:"
./motor gris tests/datos/gris.pgm "$trabajo/vuelta.pgm" 2> "$trabajo/err.txt"
if grep -q "tres canales" "$trabajo/err.txt"; then
    echo "  ok    gris rechaza una imagen de un canal"
else
    echo "  FALLA gris debería rechazar una imagen de un canal"
    fallos=$((fallos + 1))
fi
./motor caja tests/datos/gris.pgm "$trabajo/caja1.pgm"
revisar "caja sobre un canal escribe P5" bash -c \
    "head -c 2 '$trabajo/caja1.pgm' | grep -q P5"

echo "Paralelo contra secuencial, imagen de 512x512 y 1 y 8 hilos:"
./generar "$trabajo/grande.ppm" 512 512 > /dev/null
./motor gris "$trabajo/grande.ppm" "$trabajo/grande.pgm" > /dev/null
for filtro in gris caja gaussiano; do
    for hilos in 1 8; do
        paralelo_contra_secuencial "$filtro" "$trabajo/grande.ppm" "$hilos"
    done
done
for filtro in sobel otsu ecualizar; do
    for hilos in 1 8; do
        paralelo_contra_secuencial "$filtro" "$trabajo/grande.pgm" "$hilos"
    done
done

echo "Optimización de localidad y variantes de medición:"
# El gaussiano por bloques tiene que dar lo mismo que el separable secuencial.
OMP_NUM_THREADS=8 ./motor gaussiano_bloques "$trabajo/grande.ppm" "$trabajo/gbloques.ppm"
./motor gaussiano "$trabajo/grande.ppm" "$trabajo/gsep.ppm" --secuencial
revisar "gaussiano por bloques idéntico al separable" ./comparar "$trabajo/gbloques.ppm" "$trabajo/gsep.ppm"
# La forma directa de 5x5 y la separable comparten la definición sin redondeo
# intermedio, así que también tienen que coincidir.
./motor gaussiano_directo "$trabajo/grande.ppm" "$trabajo/gdirecto.ppm"
revisar "gaussiano directo 5x5 idéntico al separable" ./comparar "$trabajo/gdirecto.ppm" "$trabajo/gsep.ppm"
# El histograma compartido con atomic y el privado por hilo dan el mismo Otsu.
OMP_NUM_THREADS=8 ./motor otsu_compartido "$trabajo/grande.pgm" "$trabajo/ocomp.pgm"
./motor otsu "$trabajo/grande.pgm" "$trabajo/opriv.pgm" --secuencial
revisar "Otsu con histograma compartido idéntico al privado" ./comparar "$trabajo/ocomp.pgm" "$trabajo/opriv.pgm"

echo "Cadena completa gris,gaussiano,sobel,otsu:"
./motor gris,gaussiano,sobel,otsu "$trabajo/grande.ppm" "$trabajo/cadena-sec.pgm" --secuencial
for hilos in 1 8; do
    OMP_NUM_THREADS="$hilos" ./motor gris,gaussiano,sobel,otsu "$trabajo/grande.ppm" "$trabajo/cadena-par.pgm"
    revisar "cadena idéntica con $hilos hilo(s)" ./comparar "$trabajo/cadena-par.pgm" "$trabajo/cadena-sec.pgm"
done

echo "Filtro desconocido:"
# El motor sale con código 1 al rechazar la cadena, así que su salida se
# recoge antes de buscar en ella: con pipefail, la tubería heredaría ese
# código y la prueba fallaría por el motivo contrario al que busca.
queja=$(./motor nosexiste tests/datos/entrada.ppm "$trabajo/x" 2>&1 || true)
if printf '%s' "$queja" | grep -q "desconocido"; then
    echo "  ok    la cadena con un filtro inexistente se rechaza"
else
    echo "  FALLA una cadena con un filtro inexistente debería rechazarse"
    fallos=$((fallos + 1))
fi

echo "Cadenas que no se pueden aplicar:"
for caso in "sobel:un canal" "gris,gris:tres canales" "otsu:un canal" "ecualizar:un canal"; do
    cadena="${caso%%:*}"
    espera="${caso##*:}"
    queja=$(./motor "$cadena" tests/datos/entrada.ppm "$trabajo/x" 2>&1 || true)
    if printf '%s' "$queja" | grep -q "$espera"; then
        echo "  ok    $cadena se rechaza antes de aplicar nada"
    else
        echo "  FALLA $cadena debería rechazarse: $queja"
        fallos=$((fallos + 1))
    fi
done

echo
if [ "$fallos" -eq 0 ]; then
    echo "todas las pruebas pasaron"
else
    echo "$fallos prueba(s) fallaron"
fi
exit "$fallos"
