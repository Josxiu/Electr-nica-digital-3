/**
 * @file pines.h
 * @brief Asignación de GPIO del montaje de Octavia (Raspberry Pi Pico, RP2040).
 */
#ifndef PINES_H
#define PINES_H

#include <stdint.h>

/* --- BUS DEL DAC (8 BITS PARALELO / R-2R) ---
 * GP11 (bit 0, LSB) a GP18 (bit 7, MSB). Son consecutivos para escribir la
 * muestra completa con una sola operación enmascarada.
 */
#define DAC_BASE_PIN 11u

/* --- BOTONES (a GND, con pull-up interno) ---
 * GP2..GP8: notas Do, Re, Mi, Fa, Sol, La, Si
 * GP9: bajar octava    GP10: subir octava
 */
#define NUM_BOTONES  9
#define BTN_OCT_BAJA 7   /* índice dentro de PIN_BOTON */
#define BTN_OCT_SUBE 8
static const uint8_t PIN_BOTON[NUM_BOTONES] = { 2, 3, 4, 5, 6, 7, 8, 9, 10 };

/* --- LEDS DE OCTAVA (GPIO -> resistencia 220-330 ohm -> LED -> GND) ---
 * GP19: octava 3    GP20: octava 4    GP21: octava 5
 */
static const uint8_t PIN_LED_OCTAVA[3] = { 19, 20, 21 };

/* --- PIN DE PRUEBA ---
 * Queda en alto mientras la interrupción calcula una muestra. En el
 * osciloscopio: periodo = 1/FS_HZ (25 us) y ancho = tiempo de cálculo.
 */
#define PIN_PRUEBA_ISR 22u

#endif /* PINES_H */
