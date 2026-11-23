/**
 * @file prueba_bateria_pc.c
 * @brief Prueba en el PC de la conversion y de la tabla de la bateria.
 *
 * Se compila con gcc, sin el SDK:
 *   gcc -Wall -Wextra -std=c11 -Iinclude pruebas/prueba_bateria_pc.c src/bateria.c -o prueba_bateria
 *
 * Los valores esperados se calcularon a mano con la tabla de bateria.c.
 */
#include <math.h>
#include <stdio.h>
#include "bateria.h"

static int fallas = 0;

static void revisar(const char *caso, bool cumple) {
    printf("%-52s %s\n", caso, cumple ? "OK" : "FALLA");
    if (!cumple) {
        fallas++;
    }
}

int main(void) {
    /* Tabla: puntos exactos, interpolacion y extremos */
    revisar("1,30 V por pila -> 56 %", bateria_porcentaje(1.30f) == 56);
    revisar("1,25 V (entre 26 y 56 %) -> 41 %", bateria_porcentaje(1.25f) == 41);
    revisar("1,40 V (entre 56 y 100 %) -> 78 %", bateria_porcentaje(1.40f) == 78);
    revisar("1,00 V -> 8 %", bateria_porcentaje(1.00f) == 8);
    revisar("1,60 V (pilas nuevas o USB) -> 100 %", bateria_porcentaje(1.60f) == 100);
    revisar("0,50 V (agotadas) -> 0 %", bateria_porcentaje(0.50f) == 0);

    /* Conversion: 1613 cuentas = 1,2995 V en GP29 = 3,898 V en VSYS = 1,2995 V por pila */
    const float v = bateria_voltaje_celda(1613);
    revisar("1613 cuentas -> 1,2995 V por pila", fabsf(v - 1.2995f) < 0.0005f);

    /* Promedio: arranca con la primera muestra y un pico aislado casi no lo mueve */
    bateria_t b;
    bateria_iniciar(&b);
    bateria_agregar(&b, 1613);
    revisar("primera muestra: arranca en 1,2995 V y 56 %",
            fabsf(b.v_celda - 1.2995f) < 0.0005f && b.pct == 56);
    bateria_agregar(&b, 1613 + 248);    /* +0,2 V por pila en una sola muestra */
    revisar("un pico de +0,2 V mueve el promedio menos de 0,01 V",
            fabsf(b.v_celda - 1.2995f) < 0.01f);

    printf("\n%s (%d fallas)\n", fallas == 0 ? "Todo bien" : "Hay fallas", fallas);
    return fallas;
}
