/**
 * @file reporte.c
 * @brief Formateo de las líneas de reporte con snprintf.
 */
#include "reporte.h"

#include <inttypes.h>
#include <stdio.h>

void reporte_iniciar(reporte_t *r)
{
    r->n             = 0u;
    r->hay_anterior  = false;
    r->t_anterior_us = 0u;
    r->omitidos      = 0u;
    r->descartados   = 0u;
}

size_t reporte_formatear(reporte_t *r, const medicion_resultado_t *res,
                         uint32_t t_us, char *buf, size_t tam)
{
    /* Δt en ms, redondeado. El primer reporte siempre lleva 0. */
    uint32_t dt_ms = 0u;
    if (r->hay_anterior) {
        dt_ms = (t_us - r->t_anterior_us + 500u) / 1000u;
    }
    r->hay_anterior  = true;
    r->t_anterior_us = t_us;

    int n_bytes;
    switch (res->estado) {
    case MEDICION_OK:
        /* Punto fijo -> texto: 1000000 mHz -> "1000.000", 250 -> "25.0" */
        n_bytes = snprintf(buf, tam,
                           "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ".%03" PRIu32 ",%u.%u,OK\n",
                           r->n, dt_ms,
                           res->frecuencia_mhz / 1000u, res->frecuencia_mhz % 1000u,
                           (unsigned)(res->duty_decimas / 10u),
                           (unsigned)(res->duty_decimas % 10u));
        break;
    case MEDICION_SIN_TRANSICIONES:
        n_bytes = snprintf(buf, tam, "%" PRIu32 ",%" PRIu32 ",NA,NA,SIN_TRANSICIONES\n",
                           r->n, dt_ms);
        break;
    case MEDICION_ESPERANDO:
    default:
        n_bytes = snprintf(buf, tam, "%" PRIu32 ",%" PRIu32 ",NA,NA,ESPERANDO\n",
                           r->n, dt_ms);
        break;
    }

    r->n++;   /* el número avanza aunque la línea luego se descarte */

    /* snprintf devuelve lo que habría escrito; si no cupo, no se envía
     * una línea truncada. */
    if (n_bytes < 0 || (size_t)n_bytes >= tam) {
        return 0u;
    }
    return (size_t)n_bytes;
}

void reporte_omitir(reporte_t *r)
{
    r->n++;
    r->omitidos++;
}

void reporte_contar_descarte(reporte_t *r)
{
    r->descartados++;
}
