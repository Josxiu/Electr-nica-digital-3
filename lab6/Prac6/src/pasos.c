/**
 * @file pasos.c
 * @brief Contador de pasos. Ver pasos.h.
 */
#include "pasos.h"
#include "config.h"

#include <math.h>
#include <string.h>

void pasos_iniciar(pasos_t *p) {
    memset(p, 0, sizeof(*p));
}

/** Registra un paso detectado. Devuelve cuantos pasos se suman. */
static uint32_t registrar_paso(pasos_t *p, uint32_t t_us) {
    p->hay_paso = true;
    p->t_paso_us = t_us;
    p->candidatos++;

    if (p->caminando) {
        p->total++;
        return 1;
    }
    if (p->candidatos >= PASOS_CONFIRMAR) {
        p->caminando = true;
        p->total += p->candidatos;
        return p->candidatos;
    }
    return 0;
}

uint32_t pasos_agregar(pasos_t *p, const mpu6050_datos_t *d) {
    const float mag = sqrtf(d->ax * d->ax + d->ay * d->ay + d->az * d->az);
    p->mag = mag;

    /* Primera muestra, o pausa larga: los filtros arrancan de nuevo */
    const float dt = (d->t_us - p->t_ant_us) * 1e-6f;
    p->t_ant_us = d->t_us;
    if (!p->iniciado || dt > PASOS_DT_MAX_S) {
        p->iniciado = true;
        p->media = mag;
        p->filtrada = 0.0f;
        p->armado = false;
        return 0;
    }

    /* La gravedad es el promedio lento; lo que queda es el movimiento */
    p->media += dt / (PASOS_TAU_MEDIA_S + dt) * (mag - p->media);
    const float movimiento = mag - p->media;
    p->filtrada += dt / (PASOS_TAU_FILTRO_S + dt) * (movimiento - p->filtrada);

    /* Sin pasos por mucho tiempo se acaba la marcha */
    if (p->hay_paso && (d->t_us - p->t_paso_us) > PASOS_SEP_MAX_MS * 1000u) {
        p->caminando = false;
        p->candidatos = 0;
        p->hay_paso = false;
    }

    /* Un paso es una subida por encima del umbral. La senal tiene que bajar
     * a la mitad del umbral antes de poder detectar el siguiente. */
    if (p->filtrada < PASOS_UMBRAL_G / 2.0f) {
        p->armado = true;
    }
    if (p->armado && p->filtrada > PASOS_UMBRAL_G) {
        p->armado = false;
        if (!p->hay_paso || (d->t_us - p->t_paso_us) >= PASOS_SEP_MIN_MS * 1000u) {
            return registrar_paso(p, d->t_us);
        }
    }
    return 0;
}
