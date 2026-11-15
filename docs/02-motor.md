# El motor: filtros, paralelización y mediciones

El motor está en `motor/`. Cada filtro existe en versión secuencial y en
versión OpenMP, y las dos producen la misma salida byte a byte. Este archivo
recoge cómo quedó implementado cada filtro nuevo, la estrategia de
paralelización, la metodología de medición, las tablas de $T(p)$, $S(p)$ y
$E(p)$ para $p \in \{1,2,4,8\}$, la comparación del gaussiano separable contra
la forma directa, la del histograma privado contra el compartido y la
optimización de localidad con su medición antes y después.

## 1. Los filtros

Las convenciones del enunciado valen para todos: los bordes se resuelven
repitiendo el píxel más cercano (`recortar()` en `motor/src/imagen.hpp`), las
sumas ponderadas se acumulan en `double`, y el entero de salida se redondea al
más cercano con los empates hacia arriba y se satura a $[0,255]$
(`redondear()`).

### 1.1 Escala de grises y desenfoque de caja

Vienen resueltos en el repositorio base. Son la línea base de un `map` y de un
`stencil`, y se conservaron como estaban.

### 1.2 Desenfoque gaussiano separable

Se usa el núcleo binomial de 5 elementos

$$
k = \frac{1}{16}\,[1,4,6,4,1],
$$

en dos pasadas: primero horizontal sobre la entrada y después vertical sobre
el intermedio,

$$
T(x,y)=\sum_{i=-2}^{2} k_{i+2}\,I(x+i,y),
\qquad
O(x,y)=\operatorname{round}\!\left(\sum_{j=-2}^{2} k_{j+2}\,T(x,y+j)\right).
$$

La forma directa de $5\times5$ cuesta 25 multiplicaciones por píxel; la
separable cuesta 10. El intermedio $T$ se guarda en `double` y no se redondea,
así que la separable y la directa producen exactamente los mismos bytes. Está
en `gaussiano_secuencial`, `gaussiano_paralelo` y `gaussiano_directo_secuencial`
(`motor/src/filtros.cpp`).

### 1.3 Sobel con magnitud del gradiente

Las dos máscaras de $3\times3$

$$
G_x=\begin{bmatrix}-1&0&1\\-2&0&2\\-1&0&1\end{bmatrix},
\qquad
G_y=\begin{bmatrix}-1&-2&-1\\0&0&0\\1&2&1\end{bmatrix}
$$

se evalúan en un solo recorrido de la vecindad: se leen los nueve vecinos una
vez y de ellos salen $g_x$ y $g_y$. La salida es

$$
O(x,y)=\min\!\left(255,\operatorname{round}\!\left(\sqrt{g_x^2+g_y^2}\right)\right).
$$

Leer la vecindad una sola vez ahorra la mitad de los accesos frente a aplicar
las dos máscaras en pasadas separadas. Está en `sobel_secuencial` y
`sobel_paralelo`.

### 1.4 Histograma, ecualización y umbral de Otsu

El histograma $h(v)$ cuenta los píxeles de cada nivel $v\in[0,255]$. La
ecualización usa el acumulado $c(v)=\sum_{u\le v}h(u)$, el primer acumulado
distinto de cero $c_{\min}$ y el total $N=W\cdot H$:

$$
O(x,y)=\operatorname{round}\!\left(
\frac{c(I(x,y))-c_{\min}}{N-c_{\min}}\cdot 255
\right).
$$

Otsu recorre los 255 umbrales y se queda con el que maximiza la varianza entre
clases

$$
\sigma_B^2(t)=\omega_0(t)\,\omega_1(t)\,\bigl(\mu_0(t)-\mu_1(t)\bigr)^2,
\qquad
t^*=\arg\max_{0\le t<255}\sigma_B^2(t),
$$

con $\omega_0(t)=\sum_{v\le t}h(v)/N$, $\omega_1=1-\omega_0$ y $\mu_0,\mu_1$
las medias de cada clase. La salida es binaria: 0 si $I(x,y)\le t^*$ y 255 en
otro caso. Está en `histograma_*`, `umbral_otsu`, `ecualizar_*` y `otsu_*`.

### 1.5 La cadena del lote

`gris → gaussiano separable → Sobel → umbral de Otsu`, siempre en ese orden.
Entra un PPM de color y sale un PGM binario. La cadena completa también tiene
versión secuencial y paralela, y las dos son idénticas byte a byte.

## 2. Paralelización con OpenMP

| Filtro | Patrón | Directiva | Reparto |
|---|---|---|---|
| `gaussiano` (ambas pasadas) | stencil | `#pragma omp parallel for schedule(static)` | filas |
| `gaussiano_bloques` | stencil fusionado | `#pragma omp parallel` + `for` sobre bloques | bloques de filas |
| `sobel` | stencil | `#pragma omp parallel for schedule(static)` | filas |
| `histograma` | reducción | `#pragma omp parallel` + histograma privado por hilo | filas |
| `otsu` | reducción + barrido | histograma paralelo + barrido secuencial + binarizado paralelo | filas |
| `ecualizar` | reducción + `map` | histograma paralelo + tabla secuencial + aplicación paralela | filas |

El reparto va sobre las **filas** y no sobre las columnas porque la imagen está
guardada por filas: una banda de filas es un bloque contiguo de memoria,
mientras que avanzar por columnas en el ciclo interno toca una línea de caché
nueva en cada paso. Con `schedule(static)` cada hilo recibe una banda contigua
y el costo por fila es uniforme, así que no hace falta balanceo dinámico.

El histograma no se puede acumular en un arreglo compartido protegido con
`atomic` sin serializar el recorrido. La versión que se usa en el motor le da
un histograma privado a cada hilo, con 8 contadores de relleno entre uno y otro
para que no compartan línea de caché, y los suma al final con un `for`
paralelo. La versión con `atomic` sobre un solo arreglo (`otsu_compartido`) se
conserva solo para medir la diferencia; sus resultados están en la sección 7.

El barrido de los 255 umbrales de Otsu se deja secuencial: son 255 iteraciones
frente a los $4096^2=16\,777\,216$ píxeles del histograma, así que su peso en
el tiempo total es despreciable. La fracción secuencial que se deduce de las
mediciones está en la sección 9.

## 3. Corrección

La prueba de corrección es el comparador byte a byte del repositorio
(`motor/tests/comparar.cpp`). `make pruebas` verifica:

- las salidas esperadas de `tests/datos/` para gris, caja, gaussiano, Sobel,
  Otsu y ecualización;
- que la versión paralela sea idéntica a la secuencial para todos los filtros,
  con **1 y 8 hilos**;
- que el gaussiano por bloques, el gaussiano directo de $5\times5$ y el Otsu
  con histograma compartido coincidan con su versión de referencia;
- que la cadena completa `gris,gaussiano,sobel,otsu` sea idéntica con 1 y 8
  hilos.

Las salidas esperadas de `tests/datos/` se calcularon con una implementación
aparte en Python de las mismas definiciones, no con el código C++.

## 4. Metodología y máquina

Todas las cifras de este archivo salen de la misma máquina:

| Componente | Valor |
|---|---|
| Procesador | AMD Ryzen 5 4600G with Radeon Graphics |
| Núcleos físicos / hilos | 6 / 12 (2 hilos por núcleo) |
| Caché | L1d 192 KiB (6 instancias), L2 3 MiB (6 instancias), L3 8 MiB (2 instancias) |
| Memoria | 15 GiB |
| Compilador | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| Sistema | Linux 7.0.0-43-generic |
| Banderas | `-std=c++17 -O2 -Wall -Wextra -fopenmp` |

La metodología es la del enunciado:

- una sola imagen de $4096\times4096$ generada con `bench/generar.cpp`;
- hilos $p\in\{1,2,4,8\}$, fijados con `OMP_NUM_THREADS`;
- cinco repeticiones por configuración, se descarta la primera y se reporta la
  mediana de las cuatro restantes;
- tiempo de pared con `omp_get_wtime()` alrededor de la llamada al filtro, con
  la imagen ya en memoria y sin contar lectura ni escritura;
- los datos crudos quedan en `mediciones/motor.csv`, con las columnas
  `filtro,hilos,repeticion,milisegundos`.

`bench/medir_completo.sh` corre la campaña y `bench/resumir.py` produce las
tablas de esta sección. El PGM de entrada de Sobel, Otsu y ecualización se
obtiene con `./motor gris medicion.ppm medicion.pgm`.

## 5. Tiempos, speedup y eficiencia

### 5.1 $T(p)$ en milisegundos

| Filtro | p=1 | p=2 | p=4 | p=8 |
|---|---:|---:|---:|---:|
| `gris` | 40.1 | 20.1 | 10.4 | 10.2 |
| `caja` | 652.2 | 340.9 | 182.5 | 155.2 |
| `gaussiano` | 554.9 | 388.1 | 306.8 | 310.1 |
| `gaussiano_bloques` | 384.2 | 204.4 | 116.9 | 112.3 |
| `gaussiano_directo` | 1525.9 | — | — | — |
| `sobel` | 61.9 | 31.6 | 16.5 | 16.3 |
| `otsu` | 17.8 | 9.6 | 5.5 | 4.5 |
| `ecualizar` | 14.5 | 8.0 | 5.0 | 4.3 |
| `otsu_compartido` | 77.8 | 59.9 | 62.4 | 60.8 |
| `cadena` | 313.0 | 198.6 | 141.4 | 142.3 |

### 5.2 $S(p)=T(1)/T(p)$

| Filtro | p=1 | p=2 | p=4 | p=8 |
|---|---:|---:|---:|---:|
| `gris` | 1.00 | 1.99 | 3.86 | 3.93 |
| `caja` | 1.00 | 1.91 | 3.57 | 4.20 |
| `gaussiano` | 1.00 | 1.43 | 1.81 | 1.79 |
| `gaussiano_bloques` | 1.00 | 1.88 | 3.29 | 3.42 |
| `gaussiano_directo` | 1.00 | — | — | — |
| `sobel` | 1.00 | 1.96 | 3.75 | 3.80 |
| `otsu` | 1.00 | 1.85 | 3.24 | 3.97 |
| `ecualizar` | 1.00 | 1.83 | 2.93 | 3.36 |
| `otsu_compartido` | 1.00 | 1.30 | 1.25 | 1.28 |
| `cadena` | 1.00 | 1.58 | 2.21 | 2.20 |

### 5.3 $E(p)=S(p)/p$

| Filtro | p=1 | p=2 | p=4 | p=8 |
|---|---:|---:|---:|---:|
| `gris` | 1.00 | 1.00 | 0.96 | 0.49 |
| `caja` | 1.00 | 0.96 | 0.89 | 0.53 |
| `gaussiano` | 1.00 | 0.71 | 0.45 | 0.22 |
| `gaussiano_bloques` | 1.00 | 0.94 | 0.82 | 0.43 |
| `gaussiano_directo` | 1.00 | — | — | — |
| `sobel` | 1.00 | 0.98 | 0.94 | 0.47 |
| `otsu` | 1.00 | 0.93 | 0.81 | 0.50 |
| `ecualizar` | 1.00 | 0.91 | 0.73 | 0.42 |
| `otsu_compartido` | 1.00 | 0.65 | 0.31 | 0.16 |
| `cadena` | 1.00 | 0.79 | 0.55 | 0.28 |

### 5.4 Lectura de las cifras

- **El gaussiano es el filtro que peor escala**: su eficiencia cae a 0.22 con
  8 hilos. La causa es la memoria, no el cómputo: el intermedio $T$ ocupa
  $4096\cdot4096\cdot3\cdot8 = 402\,653\,184$ bytes (≈384 MiB) y la pasada
  vertical lo relee cinco veces por fila. Por eso es el candidato de la
  optimización de localidad de la sección 8.
- **Sobel escala bien** (eficiencia 0.47), porque cada fila lee tres filas
  contiguas y estas caben en caché; el cuello de botella es el ancho de banda.
- **Otsu y la ecualización tienen tiempos absolutos pequeños** (4–5 ms con 8
  hilos) y una eficiencia de 0.50 y 0.42. Su histograma es una reducción de
  256 contadores y el resto es un recorrido de memoria.
- **La cadena completa** baja de 313.0 ms a 142.3 ms ($S(8)=2.20$,
  $E(8)=0.28$). Queda dominada por el gaussiano: en la cadena, el gaussiano
  aporta más de la mitad del tiempo a 8 hilos.
- `otsu_compartido` es la versión ingenua: su eficiencia es 0.16 y casi no
  mejora al agregar hilos. Es la comparación de la sección 7.

### 5.5 Gráfica de speedup

```mermaid
xychart-beta
    title "Speedup contra número de hilos"
    x-axis "Hilos" [1, 2, 4, 8]
    y-axis "S(p)" 0 --> 8
    line "ideal" [1, 2, 4, 8]
    line "gaussiano" [1.00, 1.43, 1.81, 1.79]
    line "gaussiano_bloques" [1.00, 1.88, 3.29, 3.42]
    line "sobel" [1.00, 1.96, 3.75, 3.80]
    line "otsu" [1.00, 1.85, 3.24, 3.97]
    line "cadena" [1.00, 1.58, 2.21, 2.20]
```

## 6. Gaussiano separable contra la forma directa de 5x5

La comparación se hace con **un solo hilo**, porque lo que se mide es la
decisión algorítmica y no el reparto entre núcleos.

| Forma | T(1) en ms | Multiplicaciones por píxel |
|---|---:|---:|
| Directa $5\times5$ (`gaussiano_directo`) | 1525.9 | 25 |
| Separable (`gaussiano`) | 554.9 | 10 |
| Mejora | **2.75×** | 2.5× teórica |

La mejora algorítmica (2.75×) es mayor que el speedup que aportan los núcleos
en este filtro (1.79× con 8 hilos). La separable con 8 hilos (310.1 ms) sigue
siendo más rápida que la directa con un hilo (1525.9 ms), pero la decisión que
más pesa es haber factorizado el núcleo. Ambas formas dan el mismo archivo
byte a byte.

## 7. Histograma con acumulador compartido contra histogramas privados

| Hilos | `otsu` (privado + relleno) | `otsu_compartido` (`atomic`) | Veces más rápido el privado |
|---:|---:|---:|---:|
| 1 | 17.8 ms | 77.8 ms | 4.4× |
| 2 | 9.6 ms | 59.9 ms | 6.2× |
| 4 | 5.5 ms | 62.4 ms | 11.3× |
| 8 | 4.5 ms | 60.8 ms | 13.5× |

El acumulador compartido no escala: $S(8)=1.28$ y $E(8)=0.16$. Cada incremento
toma la misma línea de caché y el `atomic` serializa a los hilos. Los
histogramas privados sí escalan ($S(8)=3.97$, $E(8)=0.50$). El relleno de 8
contadores (64 bytes) entre histogramas evita que el último contador de un
hilo y el primero del siguiente compartan línea, que es el false sharing que
aparecería si los arreglos privados estuvieran uno pegado al otro.

## 8. Optimización de localidad

Se elige el gaussiano porque es el filtro que peor escala de los tres nuevos:
$E(8)=0.22$, el valor más bajo de la tabla. La optimización es **recorrer la
pasada vertical por bloques de filas** en vez de sobre el arreglo intermedio
completo (`gaussiano_bloques`).

Para cada bloque de 32 filas se calcula el horizontal solo de las filas que
hacen falta (el bloque más dos de halo arriba y dos abajo) en un intermedio
privado del hilo. La pasada vertical consume ese bloque de inmediato, de modo
que el intermedio cabe en caché y no se escribe y relee la imagen entera. El
halo se recalcula: es trabajo redundante pequeño (4 filas de 32) a cambio de
no tocar memoria principal. La salida es idéntica a la separable porque el
horizontal no cambia y el vertical suma los mismos cinco valores.

### 8.1 Antes y después

| Hilos | `gaussiano` antes (ms) | `gaussiano_bloques` después (ms) | Mejora |
|---:|---:|---:|---:|
| 1 | 554.9 | 384.2 | 1.44× |
| 2 | 388.1 | 204.4 | 1.90× |
| 4 | 306.8 | 116.9 | 2.62× |
| 8 | 310.1 | 112.3 | **2.76×** |

El speedup del filtro pasa de $S(8)=1.79$ a $S(8)=3.42$, y la eficiencia de
0.22 a 0.43.

### 8.2 Qué cambió en el acceso a memoria

Antes, la pasada horizontal escribía los 384 MiB de $T$ y la vertical los
recorría cinco veces por fila: el tráfico a memoria principal era de varios
cientos de MiB por pasada. Después, cada hilo trabaja con un intermedio de
$(32+4)$ filas, es decir $36\cdot4096\cdot3\cdot8\approx3.5$ MiB, y lo consume
antes de pasar al siguiente bloque. El volumen de datos que se escribe y se
vuelve a leer baja en un factor cercano al número de bloques.

La evidencia del cambio se midió con `/usr/bin/time -v` (la misma corrida,
8 hilos, dos repeticiones):

| Versión | Tiempo de pared | Máximo RSS | Tiempo de sistema |
|---|---:|---:|---:|
| `gaussiano` | 306.6 ms | 495 924 KiB (≈484 MiB) | 0.42 s |
| `gaussiano_bloques` | 111.1 ms | 130 440 KiB (≈127 MiB) | 0.12 s |

El RSS baja a la cuarta parte y el tiempo de sistema se reduce porque hay
menos fallos de página y menos presión sobre el asignador. `perf stat` no se
pudo usar en esta máquina: `perf_event_paranoid` está en 4 y el núcleo niega
los contadores de hardware. Por eso la herramienta de perfilado documentada es
`/usr/bin/time -v`, que está entre las aceptadas por el enunciado.

## 9. Fracción secuencial y Ley de Amdahl

El barrido de los 255 umbrales de Otsu es la única parte que queda
deliberadamente secuencial. Comparado con el recorrido de $16\,777\,216$
píxeles, su peso es menor que 0.02 %, así que no explica la caída de
eficiencia.

Si se despeja la fracción paralelizable $f$ de $S(p)=1/((1-f)+f/p)$ con las
cifras medidas:

- gaussiano por bloques, $S(8)=3.42$: $f\approx0.84$, es decir, un 16 % del
  tiempo no escala. Ese 16 % es el tráfico de memoria y no el barrido de Otsu.
- Otsu con histograma privado, $S(8)=3.97$: $f\approx0.85$.
- cadena completa, $S(8)=2.20$: $f\approx0.69$; el límite lo pone el gaussiano,
  que es la etapa más costosa.

## 10. Reproducir las mediciones

```console
$ cd motor
$ make
$ make pruebas
$ ./generar /tmp/medicion.ppm 4096 4096
$ ./motor gris /tmp/medicion.ppm /tmp/medicion.pgm
$ bash bench/medir_completo.sh /tmp/medicion.ppm /tmp/medicion.pgm
$ python3 bench/resumir.py ../mediciones/motor.csv
```

`mediciones/motor.csv` se versiona con las 185 filas de la campaña; las
imágenes de medición no se versionan y las escribe el generador.
