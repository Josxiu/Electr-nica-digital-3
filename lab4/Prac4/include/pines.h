/**
 * @file pines.h
 * @brief Asignación de GPIO del montaje para el Sintetizador de Notas con DAC.
 */
#ifndef PINES_H
#define PINES_H

#include <stdint.h>

/* --- BUS DEL DAC (8 BITS PARALELO / R-2R) ---
 * Usamos 8 pines GPIO continuos (GP11 a GP18) para actualizar la muestra en 1 solo ciclo.
 */
#define DAC_BASE_PIN 11u

/* --- BOTONES DEL SISTEMA ---
 * GP10: Botón 1 (Do4)
 * GP11: Botón 2 (Re4)
 * GP12: Botón 3 (Mi4)
 * GP13: Botón 4 (Dividir Frecuencia / 2)
 * GP14: Botón 5 (Duplicar Frecuencia * 2)
 */
static const uint8_t PIN_BOTON[5] = { 6, 7, 8, 9, 10 };

#endif /* PINES_H */