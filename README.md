# Procesamiento de imágenes por lotes

Motor de filtros en C++ con OpenMP, un trabajador que lo expone por HTTP y un
despachador que reparte el lote entre varios trabajadores.

## Integrantes

| Nombre completo | Código | Correo institucional |
|---|---|---|
|  |  |  |
|  |  |  |
|  |  |  |

El informe está en [`docs/`](docs/).

## Qué trae el repositorio

```
motor/            el motor en C++ con OpenMP
  src/            lectura y escritura de PPM y PGM, y los filtros
  tests/          pruebas de corrección y el comparador byte a byte
  bench/          arnés de medición y generador de la imagen de medición
trabajador/       capa HTTP sobre el motor
despachador/      reparto del lote
mediciones/       las tablas que alimentan el informe
deploy/local/     el servicio completo con Docker Compose
deploy/nube/      los manifiestos del clúster
docs/             el informe, en cuatro archivos más el índice
```

De los filtros vienen resueltos la escala de grises y el desenfoque de caja de
3x3, cada uno en su versión secuencial y en su versión paralela. El desenfoque
gaussiano separable (con su forma directa de 5x5 y la variante por bloques),
Sobel con la magnitud del gradiente y el histograma con ecualización y umbral
de Otsu están implementados en `motor/src/filtros.cpp`, en versión secuencial
y paralela.

## Construir y correr el motor

```console
$ cd motor
$ make
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o motor src/main.cpp src/filtros.cpp src/imagen.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o comparar tests/comparar.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o generar bench/generar.cpp src/imagen.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o arnes bench/arnes.cpp src/filtros.cpp src/imagen.cpp
```

El motor recibe la cadena de filtros separados por comas, la imagen de entrada
y la de salida. Con `--secuencial` corre la versión de un hilo:

```console
$ ./motor gris tests/datos/entrada.ppm /tmp/salida.pgm
$ ./motor caja,caja tests/datos/entrada.ppm /tmp/suave.ppm --secuencial
```

La cadena del lote es `gris,gaussiano,sobel,otsu`: entra un PPM de color y
sale un PGM binario. Cada filtro necesita cierto número de canales y entrega
otro, así que el motor recorre la cadena llevando la cuenta y la rechaza antes
de aplicar nada cuando un filtro no va a recibir lo que necesita. `gris` pide
tres canales y devuelve uno; `sobel` y `otsu` piden uno; `caja` y `gaussiano`
trabajan con cualquiera de los dos.

## Las pruebas

```console
$ make test
Salida esperada, calculada aparte:
  ok    gris sobre la imagen de prueba
  ok    caja sobre la imagen de prueba
  ok    gaussiano separable sobre la imagen de prueba
  ok    Sobel sobre la imagen de prueba
  ok    Otsu sobre la imagen de prueba
  ok    ecualización sobre la imagen de prueba
Lectura y escritura:
  ok    gris rechaza una imagen de un canal
  ok    caja sobre un canal escribe P5
Paralelo contra secuencial, imagen de 512x512 y 1, 2, 4 y 8 hilos:
  ok    gaussiano idéntico con 8 hilo(s)
  ok    sobel idéntico con 8 hilo(s)
  ok    otsu idéntico con 8 hilo(s)
  ok    ecualizar idéntico con 8 hilo(s)
Optimización de localidad y variantes de medición:
  ok    gaussiano por bloques idéntico al separable con 8 hilo(s)
  ok    gaussiano directo 5x5 idéntico al separable
  ok    Otsu con histograma compartido idéntico al privado con 8 hilo(s)
Cadena completa gris,gaussiano,sobel,otsu:
  ok    cadena idéntica con 8 hilo(s)

todas las pruebas pasaron
```

`make test` es un alias de `make pruebas`. La salida completa de la corrida
usada en el informe está en `mediciones/evidencias/pruebas.txt`.

Las salidas esperadas de `tests/datos/` se calcularon con una implementación
aparte de las mismas definiciones, así que una prueba que pasa dice que las
dos coinciden. Un filtro nuevo se prueba igual: su salida paralela tiene que
ser idéntica byte a byte a la secuencial, y `comparar` es quien lo dice.

## Medir

La imagen de medición es de 4096x4096 y no se versiona: la escribe el
generador, que produce siempre el mismo archivo.

```console
$ ./generar /tmp/medicion.ppm 4096 4096
escrito /tmp/medicion.ppm: 4096x4096, 50331648 bytes de píxeles
```

El arnés cronometra solo la llamada al filtro, con la imagen ya en memoria, y
escribe una fila por repetición. El número de hilos sale de
`OMP_NUM_THREADS`; el resumen de la mediana descarta la primera corrida.

```console
$ OMP_NUM_THREADS=1 ./arnes gris /tmp/medicion.ppm 3 --encabezado
filtro,hilos,repeticion,milisegundos
gris,1,1,2.51226
gris,1,2,2.46808
gris,1,3,2.09325
gris con 1 hilo: mediana 2.28067 ms sobre 2 corridas
```

`bench/medir.sh` recorre 1, 2, 4 y 8 hilos para los filtros que se le pasen y
deja la tabla completa en `mediciones/motor.csv`:

```console
$ bash bench/medir.sh /tmp/medicion.ppm gris caja
escrito ../mediciones/motor.csv con 40 filas
```

## Convenciones que no cambian

De ellas depende que la comparación byte a byte signifique algo.

1. **Bordes por repetición del píxel más cercano.** La vecindad que se sale de
   la imagen usa el píxel del borde: las coordenadas se recortan a
   `[0, ancho-1]` y `[0, alto-1]`. No se rellena con ceros ni se recorta la
   salida.
2. **Acumulación en `double` y redondeo al final**, al entero más cercano con
   los empates hacia arriba, saturando a `[0, 255]`. Están en `redondear()`.
3. **Las reducciones son sobre enteros.** El histograma acumula contadores
   enteros y Otsu trabaja sobre el histograma, así que el resultado no depende
   del orden en que los hilos acumulen.
