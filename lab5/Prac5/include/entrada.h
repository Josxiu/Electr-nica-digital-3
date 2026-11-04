/**
 * @file entrada.h
 * @brief Pin de entrada de la señal: configuración y detección de flancos
 *        por interrupción.
 *
 * - Versión polling: solo usa entrada_iniciar() y entrada_leer().
 * - Versiones IRQ e híbrida: además usan entrada_habilitar_irq(), que instala
 *   la ISR del banco de GPIO. La ISR toma el instante del flanco y llama a
 *   una función de la versión (callback). Qué hace esa función es lo que
 *   distingue a cada versión:
 *     - IRQ pura: registra el flanco en el medidor ahí mismo.
 *     - Híbrida : solo guarda el evento en una cola para el main.
 */
#ifndef ENTRADA_H
#define ENTRADA_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Función que la ISR llama en cada flanco.
 * @param subida      true si fue de 0 a 1, false si fue de 1 a 0.
 * @param t_us        instante del flanco (time_us_32), tomado al entrar a la ISR.
 * @param hubo_perdida true si la ISR encontró subida Y bajada pendientes a la
 *                    vez (llegaron dos flancos antes de atender el primero).
 *                    En ese caso @p subida indica el último, según el nivel actual.
 * Contexto: interrupción IO_IRQ_BANK0. Debe ser muy corta.
 */
typedef void (*entrada_callback_t)(bool subida, uint32_t t_us, bool hubo_perdida);

/** @brief Configura el pin como entrada digital sin resistencias internas. */
void entrada_iniciar(unsigned pin);

/** @return nivel actual del pin (true = alto). */
bool entrada_leer(void);

/**
 * @brief Habilita la interrupción por ambos flancos e instala la ISR.
 * @param cb         función a llamar en cada flanco.
 * @param prioridad  prioridad NVIC de IO_IRQ_BANK0 (0x00 = la más alta).
 */
void entrada_habilitar_irq(entrada_callback_t cb, uint8_t prioridad);

/** @return interrupciones del GPIO atendidas (diagnóstico). */
uint32_t entrada_interrupciones(void);

#endif /* ENTRADA_H */
