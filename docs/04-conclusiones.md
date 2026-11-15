# Conclusiones

El proyecto quedó completo en sus cuatro primeras partes: los filtros nuevos en
C++, la paralelización con OpenMP, el servicio HTTP con su despachador y el
despliegue con Docker Compose y manifiestos de Kubernetes. La quinta parte
(GitHub Actions y SonarQube) no se implementó; las imágenes y los manifiestos
quedan preparados para conectarla.

## 1. Los filtros y su corrección

Los seis filtros del motor —escala de grises, caja, gaussiano separable, Sobel,
ecualización y Otsu— están implementados en versión secuencial y paralela. La
corrección no se apoya en comparar la versión paralela consigo misma: las
salidas esperadas de `motor/tests/datos/` se calcularon con una implementación
aparte de las mismas definiciones, y `comparar` decide byte a byte. Además, la
versión paralela se compara contra la secuencial con 1 y 8 hilos, y la cadena
completa con las dos.

Esa forma de probar encontró el punto delicado del proyecto: dos versiones
pueden producir el mismo resultado visual y no el mismo archivo. Por eso las
reglas de borde, el redondeo y la acumulación entera del histograma están
fijadas en las convenciones del README y no cambian entre versiones.

## 2. Lo que muestran las mediciones del motor

Las cifras completas están en [02-motor.md](./02-motor.md) y en
`mediciones/motor.csv`. Los resultados que más pesaron en las decisiones fueron:

| Hallazgo | Dato |
|---|---|
| El gaussiano separable vence a la forma directa 5x5 | 554.9 ms contra 1525.9 ms (2.75×) |
| La optimización de localidad mejora el gaussiano con 8 hilos | 310.1 ms contra 112.3 ms (2.76×); $E(8)$ pasa de 0.22 a 0.43 |
| Los histogramas privados vencen al acumulador compartido | 4.5 ms contra 60.8 ms con 8 hilos (13.5×) |
| La cadena del lote escala menos que sus filtros | $S(8)=2.20$, $E(8)=0.28$ |

La conclusión técnica es que el límite no es el número de núcleos sino la
memoria. El gaussiano es el filtro que peor escala porque escribe y relee un
intermedio de cientos de MiB; recorrerlo por bloques de filas reduce el tráfico
y el RSS a la cuarta parte. El acumulador compartido con `atomic` muestra el
otro extremo: agregar hilos a una misma línea de caché serializa el trabajo y
puede ser más lento que un solo hilo. En la cadena, el gaussiano domina el
tiempo y arrastra el speedup del conjunto.

## 3. Lo que muestran las mediciones del servicio

El trabajador y el despachador se probaron de punta a punta con el binario real:
el contrato HTTP, los códigos 400/411/413/503/504, la cadena por defecto y la
coincidencia byte a byte entre una respuesta HTTP y el motor ejecutado
directamente. La validación final se hizo con **Docker Compose real**, con las
imágenes construidas por los Dockerfiles del repositorio. El lote de 64
imágenes de 1024x1024 con `CONCURRENCIA=8` dio:

| Trabajadores | Mediana | Imágenes/s | Speedup |
|---:|---:|---:|---:|
| 1 | 1.220 s | 52.5 | 1.00× |
| 4 | 0.443 s | 144.5 | 2.75× |

Los mismos procesos sin contenedores dieron 1.079 s y 0.399 s (2.70×). Docker
añade entre un 11 % y un 13 % de tiempo, pero no cambia el escalado: el reparto
por HTTP funciona igual dentro y fuera de Compose. El escalado del servicio
(2.75×) es mejor que el de la cadena dentro de un proceso ($S(8)=2.20$) y peor
que 4×. La diferencia tiene dos causas: el gaussiano sigue siendo intensivo en
memoria y el despachador lee y escribe el lote en el mismo disco. Aun así, la
eficiencia de 0.69 con cuatro trabajadores muestra que el reparto por HTTP no
es el cuello de botella.

La decisión de arquitectura que más simplificó el despliegue fue dejar el
cómputo en C++ y hacer que el trabajador solo transporte bytes por
`stdin`/`stdout`. El trabajador no monta volúmenes, no escribe archivos
temporales y no necesita estado: cualquier réplica puede atender cualquier
imagen. El despachador es el único que ve `entrada/`, `salida/` y el CSV, y por
eso escalar trabajadores no multiplica los accesos al disco ni crea carreras
sobre el lote.

## 4. Limitaciones

- **GKE no se ejecutó en esta máquina.** Docker, Compose, `kubectl` y Google
  Cloud CLI quedaron instalados y Compose se validó de punta a punta, pero los
  tres proyectos de la cuenta no tienen facturación activa y la cuenta de
  facturación educativa no está vinculada. Los manifiestos de Kubernetes se
  validaron como YAML y se revisaron a mano; falta confirmar en un clúster real
  los tiempos absolutos y el reparto de kube-proxy.
- **El despliegue en la nube es de una sola corrida.** El despachador es un Job
  que procesa el lote y termina; no hay un servicio permanente que reciba
  imágenes nuevas. Es lo que pide el enunciado, pero un servicio de producción
  necesitaría una cola o un controlador que lance lotes.
- **El punto 5 no está implementado.** No hay flujo de integración continua ni
  análisis estático en SonarQube. Los Dockerfiles, las etiquetas inmutables y
  `deploy/nube/aplicar.sh` son el punto de entrada para agregarlo sin cambiar
  la aplicación.
- **La medición es de una sola máquina.** El speedup de 4 trabajadores mezcla
  la contención de memoria con el I/O del disco local; en un clúster con nodos
  separados el reparto puede ser distinto.

## 5. Cierre

El proyecto cumple el objetivo de separar el cómputo del transporte y del
almacenamiento. El motor concentra los filtros y las decisiones de memoria; el
trabajador es una capa fina sin estado; el despachador reparte el lote y es el
único que toca el disco. Esa separación es la que permite pasar de un proceso
local a cuatro contenedores en Compose y a tres réplicas en Kubernetes sin
reescribir la aplicación, y es también la que deja el punto 5 como una capa de
automatización encima, no como un requisito para que el servicio funcione.
