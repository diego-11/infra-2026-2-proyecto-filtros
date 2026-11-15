# Despliegue

El servicio se despliega de dos formas con la misma imagen y el mismo código:
Docker Compose en local y Kubernetes/GKE en la nube. En los dos casos el
trabajador no monta volúmenes y el despachador es el único que ve el lote.

## 1. Imágenes

Hay dos imágenes multi-stage:

| Imagen | Dockerfile | Contenido |
|---|---|---|
| `trabajador` | `trabajador/Dockerfile` | Motor C++ compilado con OpenMP, Python 3.12, `libgomp1` y `trabajador/app.py`. |
| `despachador` | `despachador/Dockerfile` | Generador C++ del lote, Python 3.12, `libgomp1` y `despachador/despachador.py`. |

La etapa de compilación usa `gcc:12-bookworm`; la etapa final usa
`python:3.12-slim-bookworm`. El binario del motor se compila en la primera y se
copia a la segunda, así que la imagen final no lleva compilador. El trabajador
corre con el UID `10001` y no necesita escribir en disco. El despachador es el
único que monta volúmenes.

Para construir localmente:

```console
$ docker build -f trabajador/Dockerfile -t filtros-trabajador:local-1 .
$ docker build -f despachador/Dockerfile -t filtros-despachador:local-1 .
```

El contexto de construcción es la raíz del repositorio porque los dos
Dockerfiles copian `motor/`. `.dockerignore` deja fuera `.git`, los binarios
locales, las imágenes `.ppm`/`.pgm` y los directorios del lote.

## 2. Despliegue local con Docker Compose

### 2.1 Un comando

El archivo `deploy/local/docker-compose.yml` levanta los trabajadores y el
despachador. El script `deploy/local/correr.sh` construye las imágenes, espera
a que los trabajadores estén sanos, ejecuta el lote y baja todo cuando el
despachador termina:

```console
$ bash deploy/local/correr.sh 1
$ bash deploy/local/correr.sh 4
```

El número es el de réplicas del servicio `trabajador`. Por dentro ejecuta:

```console
$ docker compose -f deploy/local/docker-compose.yml up --build \
    --abort-on-container-exit --exit-code-from despachador \
    --scale trabajador=4
```

`--scale trabajador=4` crea cuatro contenedores del trabajador. El nombre
`trabajador` resuelve a las cuatro IPs y el despachador reparte las peticiones
entre ellas. No hay que cambiar el despachador para pasar de 1 a 4.

### 2.2 Qué pasa en una corrida

1. Compose construye las dos imágenes y arranca los trabajadores.
2. El `healthcheck` de cada trabajador consulta `/readyz`; el despachador
   espera a que el servicio esté sano.
3. El despachador mira `entrada/`. Si no hay archivos `.ppm`, genera 64
   imágenes de 1024x1024 con el binario C++ `generar`. La generación no se
   cuenta en el throughput.
4. Envía las imágenes con `CONCURRENCIA=8` peticiones en vuelo.
5. Escribe las salidas en `salida/` y agrega una fila a
   `mediciones/servicio.csv`.
6. Al terminar el despachador, `--abort-on-container-exit` baja los
   trabajadores.

Los directorios locales son:

```text
entrada/                 imágenes del lote (se ignoran en git)
salida/                  resultados (se ignoran en git)
mediciones/servicio.csv  tabla de throughput (se versiona)
```

La primera corrida genera el lote; las siguientes lo reutilizan. Para
regenerarlo, borra `entrada/*.ppm` y `salida/*`. Para empezar el CSV de cero,
borra `mediciones/servicio.csv`.

### 2.3 Variables de Compose

Se pueden pasar por el entorno antes de `correr.sh`:

| Variable | Valor por defecto | Efecto |
|---|---|---|
| `OMP_NUM_THREADS` | `1` | Hilos de OpenMP por proceso del motor. |
| `MAXIMO_EN_VUELO` | `2` | Procesos del motor simultáneos por trabajador. |
| `CONCURRENCIA` | `8` | Peticiones en vuelo del despachador. |
| `FILTROS` | `gris,gaussiano,sobel,otsu` | Cadena que se aplica. |
| `IMAGENES` | `64` | Tamaño del lote si hay que generarlo. |
| `ANCHO`, `ALTO` | `1024` | Tamaño de las imágenes generadas. |
| `REINTENTOS`, `ESPERA_REINTENTO` | `30`, `0.5` | Tolerancia a trabajadores que aún no responden. |

Por ejemplo, para usar dos hilos por proceso:

```console
$ OMP_NUM_THREADS=2 CONCURRENCIA=4 bash deploy/local/correr.sh 4
```

### 2.4 Medición local

La medición se hizo con la misma imagen lógica del servicio: un despachador
Python y procesos del trabajador Python, cada uno con el binario `motor`
real, `OMP_NUM_THREADS=1`, `MAXIMO_EN_VUELO=2`, `CONCURRENCIA=8` y la cadena
`gris,gaussiano,sobel,otsu`. El lote son 64 imágenes PPM de 1024x1024.

Hay dos entornos en `mediciones/servicio.csv`:

- `local`: la corrida real de Docker Compose con `deploy/local/correr.sh`,
  usando las imágenes construidas por los Dockerfiles del repositorio.
- `local-sin-docker`: los mismos procesos arrancados directamente en
  localhost con las mismas variables de entorno, sin contenedores. Sirve para
  separar el costo de la aplicación del costo de Docker.

En ambos casos se hicieron tres repeticiones para 1 trabajador y tres para 4.
La primera corrida de Compose con 1 trabajador fue además la que generó el
lote; la generación no está incluida en el tiempo, así que las seis corridas de
Compose usan las mismas 64 imágenes ya en caché de página.

Los datos crudos están en `mediciones/servicio.csv`:

```csv
entorno,trabajadores,concurrencia,imagenes,segundos
local-sin-docker,1,8,64,1.079
local-sin-docker,1,8,64,1.092
local-sin-docker,1,8,64,1.008
local-sin-docker,4,8,64,0.399
local-sin-docker,4,8,64,0.394
local-sin-docker,4,8,64,0.401
local,1,8,64,1.163
local,4,8,64,0.443
local,1,8,64,1.220
local,1,8,64,1.291
local,4,8,64,0.436
local,4,8,64,0.448
```

| Entorno | Trabajadores | Repeticiones (s) | Mediana (s) | Imágenes/s | Tiempo por imagen |
|---|---|---:|---:|---:|---:|
| Compose | 1 | 1.163, 1.220, 1.291 | 1.220 | 52.5 | 19.1 ms |
| Compose | 4 | 0.443, 0.436, 0.448 | 0.443 | 144.5 | 6.9 ms |
| Sin Docker | 1 | 1.079, 1.092, 1.008 | 1.079 | 59.3 | 16.9 ms |
| Sin Docker | 4 | 0.399, 0.394, 0.401 | 0.399 | 160.4 | 6.2 ms |

El speedup de 1 a 4 trabajadores es **2.75× en Compose** (eficiencia 0.69) y
**2.70× sin Docker** (eficiencia 0.68): el reparto escala igual dentro y fuera
de contenedores. Docker añade un 13 % de tiempo con un trabajador (1.220 s
frente a 1.079 s) y un 11 % con cuatro (0.443 s frente a 0.399 s). Ese costo
es el esperado por la red virtual de Compose y por el despachador corriendo
contra bind mounts; no crece al escalar, así que no es el cuello de botella.

El escalado no llega a 4× porque el gaussiano es intensivo en memoria y el
despachador también lee y escribe el lote en el mismo disco. El tiempo medio
dentro del motor fue de unos 33 ms con un trabajador y unos 40 ms con cuatro:
al haber más procesos simultáneos, cada uno tarda un poco más por la presión
sobre la memoria, pero el conjunto termina antes. La comparación con la cadena
del motor en [02-motor.md](./02-motor.md) es consistente: la cadena de
1024x1024 es la dieciseisava parte de la de 4096x4096.

Para reproducir la medición con Compose:

```console
$ bash deploy/local/correr.sh 1
$ bash deploy/local/correr.sh 4
```

Para reproducirla sin Docker:

```console
$ ./motor/generar /tmp/lote/imagen_0001.ppm 1024 1024 1
$ PUERTO=8000 RUTA_MOTOR=./motor/motor OMP_NUM_THREADS=1 \
    MAXIMO_EN_VUELO=2 python3 trabajador/app.py
$ ENTRADA=/tmp/lote SALIDA=/tmp/salida CSV_SERVICIO=/tmp/servicio.csv \
    TRABAJADOR_URL=http://127.0.0.1:8000 CONCURRENCIA=8 \
    python3 despachador/despachador.py
```

## 3. Despliegue en GKE

### 3.1 Publicar las imágenes

Las etiquetas de los manifiestos son marcadores
`${IMAGEN_TRABAJADOR}` y `${IMAGEN_DESPACHADOR}`. `deploy/nube/aplicar.sh` las
sustituye por las que se pasen. La etiqueta debe ser inmutable, por ejemplo el
SHA corto del commit; nunca `latest`.

```console
$ PROYECTO=mi-proyecto
$ REGION=us-central1
$ REPO=filtros
$ TAG=$(git rev-parse --short HEAD)

$ gcloud auth configure-docker "$REGION-docker.pkg.dev"
$ docker build -f trabajador/Dockerfile \
    -t "$REGION-docker.pkg.dev/$PROYECTO/$REPO/trabajador:$TAG" .
$ docker build -f despachador/Dockerfile \
    -t "$REGION-docker.pkg.dev/$PROYECTO/$REPO/despachador:$TAG" .
$ docker push "$REGION-docker.pkg.dev/$PROYECTO/$REPO/trabajador:$TAG"
$ docker push "$REGION-docker.pkg.dev/$PROYECTO/$REPO/despachador:$TAG"
```

El repositorio de Artifact Registry se crea una vez:

```console
$ gcloud artifacts repositories create "$REPO" \
    --repository-format=docker --location="$REGION"
```

### 3.2 Aplicar los manifiestos

```console
$ gcloud container clusters get-credentials NOMBRE_DEL_CLUSTER --region "$REGION"
$ IMAGEN_TRABAJADOR="$REGION-docker.pkg.dev/$PROYECTO/$REPO/trabajador:$TAG" \
  IMAGEN_DESPACHADOR="$REGION-docker.pkg.dev/$PROYECTO/$REPO/despachador:$TAG" \
  bash deploy/nube/aplicar.sh
```

El script:

1. aplica `00-namespace.yaml`, `01-configmap.yaml` y `02-pvc.yaml`;
2. sustituye las imágenes en el Deployment y el Service, y los aplica;
3. espera a que las 3 réplicas estén listas;
4. borra el Job anterior si existe y aplica `05-despachador-job.yaml`;
5. espera a que el Job termine y muestra sus logs.

Los archivos también se pueden aplicar a mano, sustituyendo las imágenes:

```console
$ kubectl apply -f deploy/nube/00-namespace.yaml
$ kubectl apply -f deploy/nube/01-configmap.yaml
$ kubectl apply -f deploy/nube/02-pvc.yaml
$ # sustituir ${IMAGEN_TRABAJADOR} y ${IMAGEN_DESPACHADOR} en los dos archivos
$ kubectl apply -f deploy/nube/03-trabajador-deployment.yaml
$ kubectl apply -f deploy/nube/04-trabajador-service.yaml
$ kubectl -n filtros rollout status deployment/trabajador
$ kubectl apply -f deploy/nube/05-despachador-job.yaml
```

### 3.3 Los manifiestos

| Archivo | Recurso | Para qué |
|---|---|---|
| `00-namespace.yaml` | Namespace `filtros` | Aísla el servicio. |
| `01-configmap.yaml` | ConfigMap `filtros-config` | `OMP_NUM_THREADS`, `MAXIMO_EN_VUELO`, `FILTROS`, `CONCURRENCIA`, `TRABAJADORES`, `IMAGENES`, `ANCHO`, `ALTO`. |
| `02-pvc.yaml` | PVC `filtros-datos` | 5 Gi para entrada, salida y CSV. Usa la StorageClass por defecto (en GKE, `standard-rwo`). |
| `03-trabajador-deployment.yaml` | Deployment `trabajador` | 3 réplicas, probes, límites y `readOnlyRootFilesystem`. |
| `04-trabajador-service.yaml` | Service `trabajador` | ClusterIP en el puerto 8000; es el nombre que usa el despachador. |
| `05-despachador-job.yaml` | Job `despachador` | Ejecuta el lote una vez, monta el PVC y escribe el CSV. |

### 3.4 Configuración del ConfigMap

```yaml
OMP_NUM_THREADS: "2"
MAXIMO_EN_VUELO: "1"
FILTROS: "gris,gaussiano,sobel,otsu"
CONCURRENCIA: "6"
TRABAJADORES: "3"
IMAGENES: "64"
ANCHO: "1024"
ALTO: "1024"
```

La combinación `OMP_NUM_THREADS=2` y `MAXIMO_EN_VUELO=1` usa dos hilos por
trabajador y un proceso de motor a la vez. Con 3 réplicas, el despachador
mantiene 6 peticiones en vuelo: dos por trabajador, una ejecutándose y otra
esperando turno. Si se cambia el número de réplicas, hay que cambiar también
`TRABAJADORES` para que el CSV diga la verdad.

### 3.5 Recursos, probes y seguridad

| Contenedor | `requests` | `limits` |
|---|---|---|
| `trabajador` | 500m CPU, 256 Mi | 2 CPU, 512 Mi |
| `despachador` | 100m CPU, 128 Mi | 500m CPU, 256 Mi |

- **Readiness del trabajador:** `GET /readyz` cada 5 s. Comprueba que el
  binario `motor` responde a `--salud`, no solo que el proceso HTTP esté vivo.
- **Liveness del trabajador:** `GET /healthz` cada 10 s. Si el proceso HTTP se
  cuelga, Kubernetes reinicia el pod.
- **Trabajador sin root:** `runAsNonRoot: true`, UID/GID `10001`, sin
  capacidades, `allowPrivilegeEscalation: false` y
  `readOnlyRootFilesystem: true`. Solo tiene un `emptyDir` en `/tmp`.
- **Despachador sin root:** `runAsUser: 1000`, `fsGroup: 1000` para que el PVC
  sea escribible, sin capacidades y con el sistema de archivos de solo
  lectura.
- **Imágenes inmutables:** las etiquetas se sustituyen por SHA; no se usa
  `latest`. `imagePullPolicy: IfNotPresent` evita descargas repetidas de una
  etiqueta que no cambia.

### 3.6 Ver el resultado

```console
$ kubectl -n filtros get pods,svc,pvc,job
$ kubectl -n filtros logs job/despachador
```

El despachador imprime la fila que agregó, por ejemplo:

```text
fila CSV: gke,3,6,64,0.812
```

El CSV completo queda en `/datos/servicio.csv` del PVC `filtros-datos`. Para
copiarlo a la máquina local se puede lanzar un pod lector que monte el mismo
PVC:

```console
$ kubectl -n filtros run lector --rm -it --restart=Never --image=busybox:1.36 \
    --overrides='{"spec":{"containers":[{"name":"lector","image":"busybox:1.36","command":["cat","/datos/servicio.csv"],"volumeMounts":[{"name":"datos","mountPath":"/datos"}]}],"volumes":[{"name":"datos","persistentVolumeClaim":{"claimName":"filtros-datos"}}]}}'
```

### 3.7 Repetir el lote o cambiar el número de trabajadores

Un Job terminado es inmutable. `aplicar.sh` lo borra antes de volver a
aplicarlo, así que para repetir el lote basta con volver a ejecutar el script.
Para probar otro número de réplicas:

```console
$ kubectl -n filtros scale deployment/trabajador --replicas=4
$ kubectl -n filtros edit configmap filtros-config   # TRABAJADORES: "4"
$ kubectl -n filtros delete job despachador
$ IMAGEN_TRABAJADOR=... IMAGEN_DESPACHADOR=... bash deploy/nube/aplicar.sh
```

Los manifiestos versionados dejan 3 réplicas, que es la configuración pedida.

## 4. Limitaciones de esta entrega

- No se incluye el flujo de GitHub Actions ni el análisis con SonarQube
  (punto 5). La construcción de las imágenes y los manifiestos están listos
  para que ese flujo solo tenga que construir, publicar y llamar a
  `aplicar.sh`.
- La máquina de desarrollo no tiene un proyecto de GCP con facturación activa,
  así que no se pudo crear el clúster ni desplegar en GKE desde aquí; el
  detalle está en la sección 4 de [04-conclusiones.md](./04-conclusiones.md).
  Sí se ejecutó Compose de punta a punta con las imágenes reales: trabajador,
  despachador, modo `stdin`/`stdout` del motor, códigos de error y medición de
  throughput. Los manifiestos de Kubernetes se validaron como YAML, se revisó
  su contenido a mano y `aplicar.sh` se probó en modo de sustitución, pero no
  se aplicaron contra un API server real.
