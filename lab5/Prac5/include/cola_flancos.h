/**
 * @file cola_flancos.h
 * @brief Cola circular de eventos de flanco entre la ISR del GPIO y el main
 *        (solo la usa la versión híbrida).
 *
 * Un productor (la ISR) y un consumidor (el main). No necesita deshabilitar
 * interrupciones: la ISR solo escribe 'cabeza' y el main solo escribe 'cola';
 * cada uno publica su índice DESPUÉS de tocar los datos.
 */
#ifndef COLA_FLANCOS_H
#define COLA_FLANCOS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Capacidad de la cola (potencia de 2). A 10 kHz llegan 20 000 flancos/s,
 * o sea 1 cada 50 µs: 256 eventos dan ~12 ms de margen para que el main
 * alcance a vaciarla aunque esté formateando un reporte.
 */
#define COLA_FLANCOS_TAM 256u

/** Un flanco tal como lo vio la ISR. */
typedef struct {
    uint32_t t_us;      /**< Instante del flanco (time_us_32).              */
    bool     subida;    /**< true = 0->1, false = 1->0.                     */
    bool     perdida;   /**< Antes de este evento se perdieron flancos.     */
} evento_flanco_t;

/** @brief Vacía la cola. Llamar antes de habilitar la interrupción. */
void cola_flancos_iniciar(void);

/**
 * @brief Mete un evento (lo llama la ISR).
 * @return false si la cola estaba llena (el evento no se guardó).
 */
bool cola_flancos_meter(const evento_flanco_t *e);

/**
 * @brief Saca el evento más antiguo (lo llama el main).
 * @return false si la cola estaba vacía.
 */
bool cola_flancos_sacar(evento_flanco_t *e);

/** @return veces que la cola estuvo llena y se descartó un evento. */
uint32_t cola_flancos_desbordes(void);

#endif /* COLA_FLANCOS_H */
