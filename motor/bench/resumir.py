#!/usr/bin/env python3
"""Resume mediciones/motor.csv en las tablas de T(p), S(p) y E(p).

Descarta la primera repetición de cada configuración y usa la mediana de las
restantes, como pide la metodología del enunciado.

    python3 bench/resumir.py [mediciones/motor.csv]
"""
import csv
import statistics
import sys
from collections import defaultdict

HILOS = [1, 2, 4, 8]

ORDEN = [
    "gris",
    "caja",
    "gaussiano",
    "gaussiano_bloques",
    "gaussiano_directo",
    "sobel",
    "otsu",
    "ecualizar",
    "otsu_compartido",
    "cadena",
]


def cargar(ruta):
    tiempos = defaultdict(list)
    with open(ruta, newline="") as f:
        for fila in csv.DictReader(f):
            tiempos[(fila["filtro"], int(fila["hilos"]))].append(float(fila["milisegundos"]))
    return tiempos


def mediana(tiempos, filtro, hilos):
    valores = tiempos.get((filtro, hilos), [])
    if not valores:
        return None
    # La primera corrida paga la lectura inicial y se descarta.
    utiles = valores[1:] if len(valores) > 1 else valores
    return statistics.median(utiles)


def tabla(titulo, cabecera, celdas):
    print(f"\n### {titulo}\n")
    print("| Filtro | " + " | ".join(cabecera) + " |")
    print("|---|" + "---:|" * len(cabecera))
    for filtro in ORDEN:
        if filtro not in celdas:
            continue
        print(f"| `{filtro}` | " + " | ".join(celdas[filtro]) + " |")


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else "../mediciones/motor.csv"
    tiempos = cargar(ruta)

    t = {}
    for filtro in ORDEN:
        base = mediana(tiempos, filtro, 1)
        if base is None:
            continue
        fila_t, fila_s, fila_e = [], [], []
        for p in HILOS:
            valor = mediana(tiempos, filtro, p)
            if valor is None:
                fila_t.append("—")
                fila_s.append("—")
                fila_e.append("—")
                continue
            fila_t.append(f"{valor:.1f}")
            speedup = base / valor
            fila_s.append(f"{speedup:.2f}")
            fila_e.append(f"{speedup / p:.2f}")
        t[filtro] = (fila_t, fila_s, fila_e)

    tabla("T(p) en milisegundos", [f"p={p}" for p in HILOS],
          {f: v[0] for f, v in t.items()})
    tabla("S(p) = T(1)/T(p)", [f"p={p}" for p in HILOS],
          {f: v[1] for f, v in t.items()})
    tabla("E(p) = S(p)/p", [f"p={p}" for p in HILOS],
          {f: v[2] for f, v in t.items()})


if __name__ == "__main__":
    main()
