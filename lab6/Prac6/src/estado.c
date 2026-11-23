/**
 * @file estado.c
 * @brief Estado del dispositivo. Ver estado.h.
 */
#include "estado.h"
#include "config.h"

/* Ultimo paso, mas lo que tarda el detector en dar la marcha por terminada,
 * mas la espera de inactividad */
#define LIMITE_INACTIVO_US  ((PASOS_SEP_MAX_MS + REPOSO_ESPERA_MS) * 1000u)
#define LIMITE_VISTA_US     (VISTA_MS * 1000u)

void estado_iniciar(estado_disp_t *e, uint32_t ahora_us) {
    e->actual = ESTADO_ACTIVO;
    e->t_ultimo_paso_us = ahora_us;
    e->t_vista_us = ahora_us;
}

bool estado_actualizar(estado_disp_t *e, uint32_t ahora_us, uint32_t pasos_nuevos) {
    if (pasos_nuevos > 0) {
        e->t_ultimo_paso_us = ahora_us;
        if (e->actual != ESTADO_ACTIVO) {
            e->actual = ESTADO_ACTIVO;
            return true;
        }
        return false;
    }

    /* En reposo no se mide nada: el tiempo puede crecer sin limite */
    if (e->actual == ESTADO_ACTIVO && (ahora_us - e->t_ultimo_paso_us) > LIMITE_INACTIVO_US) {
        e->actual = ESTADO_REPOSO;
        return true;
    }
    if (e->actual == ESTADO_VISTA && (ahora_us - e->t_vista_us) > LIMITE_VISTA_US) {
        e->actual = ESTADO_REPOSO;
        return true;
    }
    return false;
}

bool estado_boton(estado_disp_t *e, uint32_t ahora_us) {
    if (e->actual == ESTADO_ACTIVO) {
        return false;
    }
    e->t_vista_us = ahora_us;
    if (e->actual == ESTADO_REPOSO) {
        e->actual = ESTADO_VISTA;
        return true;
    }
    return false;   /* ya estaba en vista: solo se alarga */
}
