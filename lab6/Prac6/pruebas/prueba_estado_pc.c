/**
 * @file prueba_estado_pc.c
 * @brief Prueba en el PC de las transiciones entre activo y reposo.
 *
 * Se compila con gcc, sin el SDK:
 *   gcc -Wall -Wextra -std=c11 -Iinclude pruebas/prueba_estado_pc.c src/estado.c -o prueba_estado
 *
 * Con la espera de 60 s y PASOS_SEP_MAX_MS de 2 s, el reposo llega 62 s
 * despues del ultimo paso contado.
 */
#include <stdio.h>
#include "estado.h"

#define S(x) ((uint32_t)((x) * 1000000.0))   /* segundos a microsegundos */

static int fallas = 0;

static void revisar(const char *caso, bool cumple) {
    printf("%-58s %s\n", caso, cumple ? "OK" : "FALLA");
    if (!cumple) {
        fallas++;
    }
}

int main(void) {
    estado_disp_t e;

    /* Sin caminar desde el arranque */
    estado_iniciar(&e, S(0));
    revisar("arranca en activo", e.actual == ESTADO_ACTIVO);
    revisar("a los 61,9 s sin pasos sigue activo",
            !estado_actualizar(&e, S(61.9), 0) && e.actual == ESTADO_ACTIVO);
    revisar("a los 62,1 s sin pasos entra a reposo",
            estado_actualizar(&e, S(62.1), 0) && e.actual == ESTADO_REPOSO);

    /* Caminata de 10 s y luego quieto */
    estado_iniciar(&e, S(0));
    for (double t = 0.5; t <= 10.0; t += 0.5) {
        estado_actualizar(&e, S(t), 1);
    }
    revisar("caminando sigue activo", e.actual == ESTADO_ACTIVO);
    /* Un movimiento suelto a los 40 s: el detector no cuenta pasos */
    revisar("movimiento suelto (0 pasos) no cambia nada",
            !estado_actualizar(&e, S(40.0), 0) && e.actual == ESTADO_ACTIVO);
    revisar("a los 71,9 s (61,9 s tras el ultimo paso) sigue activo",
            !estado_actualizar(&e, S(71.9), 0) && e.actual == ESTADO_ACTIVO);
    revisar("a los 72,1 s (62,1 s tras el ultimo paso) entra a reposo",
            estado_actualizar(&e, S(72.1), 0) && e.actual == ESTADO_REPOSO);

    /* En reposo, sin marcha confirmada, se queda ahi mucho tiempo */
    bool quieto = true;
    for (double t = 80.0; t < 4000.0; t += 10.0) {
        if (estado_actualizar(&e, S(t), 0)) {
            quieto = false;
        }
    }
    revisar("en reposo sin pasos contados se queda en reposo", quieto && e.actual == ESTADO_REPOSO);

    /* Se confirma una marcha: el detector entrega 4 pasos de una vez */
    revisar("al confirmar la marcha (4 pasos) vuelve a activo",
            estado_actualizar(&e, S(4000.0), 4) && e.actual == ESTADO_ACTIVO);
    revisar("62,1 s despues vuelve a reposo",
            estado_actualizar(&e, S(4062.1), 0) && e.actual == ESTADO_REPOSO);

    /* Botones */
    estado_iniciar(&e, S(0));
    estado_actualizar(&e, S(5.0), 1);
    revisar("en activo un boton no cambia el estado", !estado_boton(&e, S(60.0)));
    revisar("el boton no reinicia la espera: a los 67,1 s entra a reposo",
            estado_actualizar(&e, S(67.1), 0) && e.actual == ESTADO_REPOSO);
    revisar("en reposo un boton enciende la vista",
            estado_boton(&e, S(100.0)) && e.actual == ESTADO_VISTA);
    revisar("otro boton a los 105 s solo alarga la vista", !estado_boton(&e, S(105.0)));
    revisar("a los 114,9 s (9,9 s del ultimo boton) sigue en vista",
            !estado_actualizar(&e, S(114.9), 0) && e.actual == ESTADO_VISTA);
    revisar("a los 115,1 s vuelve a reposo, sin otro minuto de espera",
            estado_actualizar(&e, S(115.1), 0) && e.actual == ESTADO_REPOSO);
    estado_boton(&e, S(200.0));
    revisar("en vista, al confirmar la marcha pasa a activo",
            estado_actualizar(&e, S(202.0), 4) && e.actual == ESTADO_ACTIVO);

    printf("\n%s (%d fallas)\n", fallas == 0 ? "Todo bien" : "Hay fallas", fallas);
    return fallas;
}
