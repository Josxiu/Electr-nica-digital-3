#ifndef SONIDO_H
#define SONIDO_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa el módulo de sonido usando PWM en el GPIO asignado.
 */
void sonido_init(void);

/**
 * @brief Reproduce el tono asignado al LED indicado (0 a 3).
 * @param idx_led Índice del LED (0 = LED 1, 1 = LED 2, etc.)
 */
void sonido_reproducir_nota(int idx_led);

/**
 * @brief Apaga la emisión de sonido en el buzzer.
 */
void sonido_apagar(void);

#endif /* SONIDO_H */