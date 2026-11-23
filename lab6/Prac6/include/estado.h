/**
 * @file estado.h
 * @brief Estado del dispositivo: activo, en reposo de bajo consumo o en vista.
 *
 * Solo calculo, no usa el SDK: se puede probar en el PC.
 *
 * La referencia de inactividad es el fin de la marcha detectada: el ultimo
 * paso contado mas PASOS_SEP_MAX_MS, que es cuando el detector da la marcha
 * por terminada. Pasados REPOSO_ESPERA_MS desde ahi se entra en reposo.
 * Los candidatos, los movimientos sueltos y los botones no cuentan como
 * pasos, asi que no reinician la espera; a activo solo se vuelve cuando se
 * confirma una marcha.
 *
 * Vista: en reposo, un boton enciende la pantalla VISTA_MS para mirar o
 * configurar. Cada boton alarga la vista; al vencer vuelve a reposo sin una
 * nueva espera de un minuto.
 */
#ifndef ESTADO_H
#define ESTADO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ESTADO_ACTIVO,      /**< Pantalla encendida y refrescandose. */
    ESTADO_REPOSO,      /**< Pantalla apagada; se siguen contando pasos. */
    ESTADO_VISTA        /**< Reposo con la pantalla encendida un momento por un boton. */
} estado_t;

typedef struct {
    estado_t actual;            /**< Estado en que esta el dispositivo. */
    uint32_t t_ultimo_paso_us;  /**< Instante del ultimo paso contado. */
    uint32_t t_vista_us;        /**< Instante del ultimo boton en vista. */
} estado_disp_t;

/** Arranca en activo, con la hora de inicio como referencia. */
void estado_iniciar(estado_disp_t *e, uint32_t ahora_us);

/**
 * @brief Actualiza el estado con cada muestra de la IMU.
 * @param e            estado del dispositivo.
 * @param ahora_us     instante de la muestra (us).
 * @param pasos_nuevos pasos que se acaban de contar (0 si ninguno).
 * @return true si el estado cambio.
 */
bool estado_actualizar(estado_disp_t *e, uint32_t ahora_us, uint32_t pasos_nuevos);

/**
 * @brief Avisa que se presiono un boton. No toca la referencia de inactividad.
 * @param e        estado del dispositivo.
 * @param ahora_us instante del boton (us).
 * @return true si el estado cambio (de reposo a vista).
 */
bool estado_boton(estado_disp_t *e, uint32_t ahora_us);

#endif /* ESTADO_H */
