"""Genera include/lut_seno.h: un ciclo de seno en 8 bits sin signo (offset binario).

La tabla queda como `const`, así el enlazador la deja en Flash y no se calcula
al arrancar. Uso, desde la carpeta Prac4:

    python tools/generar_lut.py            # 256 muestras (por defecto)
    python tools/generar_lut.py 512        # otro tamaño (potencia de 2)
"""
import math
import pathlib
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 256
if N & (N - 1) or N < 16:
    sys.exit("El tamaño debe ser potencia de 2 y >= 16")

BITS_INDICE = N.bit_length() - 1
muestras = [round(127.5 + 127.5 * math.sin(2 * math.pi * i / N)) for i in range(N)]
muestras = [min(255, max(0, m)) for m in muestras]

filas = []
for i in range(0, N, 16):
    filas.append("    " + ", ".join(f"{m:3d}" for m in muestras[i:i + 16]) + ",")

salida = pathlib.Path(__file__).resolve().parent.parent / "include" / "lut_seno.h"
salida.write_text(
    f"""/**
 * @file lut_seno.h
 * @brief Tabla de un ciclo de seno, {N} muestras de 8 bits (0-255, 128 = cero).
 *
 * Archivo generado por tools/generar_lut.py; no editar a mano.
 * Fórmula: lut[i] = round(127.5 + 127.5*sin(2*pi*i/{N})).
 */
#ifndef LUT_SENO_H
#define LUT_SENO_H

#include <stdint.h>

/** Cantidad de muestras de la tabla (potencia de 2). */
#define LUT_TAMANO {N}u
/** Bits del índice: log2(LUT_TAMANO). */
#define LUT_BITS_INDICE {BITS_INDICE}u

/** Un ciclo de seno en offset binario; queda en Flash por ser const. */
static const uint8_t lut_seno[LUT_TAMANO] = {{
{chr(10).join(filas)}
}};

#endif /* LUT_SENO_H */
""",
    encoding="utf-8",
    newline="\n",
)
print(f"Escrito {salida} ({N} muestras)")
