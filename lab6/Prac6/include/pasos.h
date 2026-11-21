/**
 * @file pasos.h
 * @brief Contador de pasos con el acelerometro.
 *
 * Solo calculo, no usa el SDK: se puede probar en el PC.
 *
 * Cada paso produce un pico en la aceleracion total |a|. Se le quita la
 * gravedad con un promedio lento, se suaviza y se cuenta cada vez que sube de
 * PASOS_UMBRAL_G, con un tiempo minimo entre pasos.
 *
 * Para no contar movimientos aislados, los pasos nuevos quedan como
 * candidatos hasta que hay PASOS_CONFIRMAR seguidos, cada uno a menos de
 * PASOS_SEP_MAX_MS del anterior. Entonces se suman todos de una vez (los
 * primeros pasos de la marcha tambien cuentan) y desde ahi se cuenta paso a
 * paso hasta que pasa PASOS_SEP_MAX_MS sin ninguno.
 */
#ifndef PASOS_H
#define PASOS_H

#include <stdbool.h>
#include <stdint.h>
#include "mpu6050.h"

typedef struct {
    uint32_t total;         /**< Pasos contados en la sesion. */
    bool caminando;         /**< La marcha ya se confirmo. */
    uint32_t candidatos;    /**< Pasos seguidos desde que empezo la marcha. */
    float mag;              /**< |a| de la ultima muestra (g). */
    float filtrada;         /**< Senal que se compara con el umbral (g). */
    uint32_t t_paso_us;     /**< Instante del ultimo paso detectado. */
    float duracion_s;       /**< Duracion de los pasos que se suman en esta muestra (s). */

    float media;           /**< Promedio lento de |a| (la gravedad). */
    uint32_t t_ant_us;      /**< Instante de la muestra anterior. */
    uint32_t t_inicio_us;   /**< Instante del primer paso de la marcha. */
    bool iniciado;          /**< Ya hubo una primera muestra. */
    bool hay_paso;          /**< Ya se detecto algun paso. */
    bool armado;            /**< La senal bajo lo suficiente para detectar otro paso. */
} pasos_t;

/** Pone todo en cero. Tambien sirve para empezar una sesion nueva. */
void pasos_iniciar(pasos_t *p);

/**
 * @brief Procesa una muestra de la IMU.
 * @return pasos que se suman con esta muestra: 0 normalmente, 1 al detectar un
 *         paso con la marcha confirmada, y PASOS_CONFIRMAR al confirmarla.
 */
uint32_t pasos_agregar(pasos_t *p, const mpu6050_datos_t *d);

#endif /* PASOS_H */
