/**
 * @file pines.h
 * @brief Asignacion de GPIO del montaje. Unica fuente de verdad del hardware.
 *
 * Los numeros son GPIO (GP0..GP29), NO numeros de pin fisico del conector.
 * Equivalencia de los pines usados en este montaje:
 *
 *   GP0 -> pin 1    GP9  -> pin 12   GP18 -> pin 24
 *   GP1 -> pin 2    GP10 -> pin 14   GP19 -> pin 25
 *   GP2 -> pin 4    GP11 -> pin 15   GP20 -> pin 26
 *   GP3 -> pin 5    GP12 -> pin 16   GP21 -> pin 27
 *   GP4 -> pin 6    GP13 -> pin 17   GP22 -> pin 29
 *   GP5 -> pin 7    GP14 -> pin 19
 *   GP6 -> pin 9    GP17 -> pin 22
 *   GP7 -> pin 10
 *   GP8 -> pin 11
 *
 * Los arreglos se declaran 'static const' y no 'const uint8_t': en C una
 * variable const en un encabezado tiene enlace externo y el enlazador la
 * rechazaria si dos unidades de compilacion incluyeran este archivo. En C++
 * no ocurria porque alli const implica enlace interno.
 *
 * @warning Los segmentos E y D estan en GP0 y GP1, que son los pines por
 *          defecto de UART0. El CMakeLists desactiva la salida estandar por
 *          UART (pico_enable_stdio_uart 0) para que el SDK no se apropie de
 *          ellos. Si se reactiva, esos dos segmentos dejan de funcionar.
 */
#ifndef PINES_H
#define PINES_H

#include <stdint.h>

/** GPIO de los segmentos A, B, C, D, E, F, G en ese orden. */
static const uint8_t PIN_SEGMENTO[7] = { 19, 17, 2, 1, 0, 18, 3 };

/** GPIO del comun de los digitos 1 a 4 (display de catodo comun). */
static const uint8_t PIN_COMUN[4] = { 22, 21, 20, 4 };

/** GPIO de los LEDs 1 a 4 de la secuencia, en el orden de los pulsadores. */
static const uint8_t PIN_LED[4] = { 6, 7, 8, 9 };

/** GPIO del LED 5, indicador de tiempo disponible. */
#define PIN_LED_TIEMPO 5u

/** GPIO de los pulsadores 1 a 4 del jugador. */
static const uint8_t PIN_BOTON[4] = { 11, 12, 13, 14 };

/** GPIO del pulsador de inicio / reinicio. */
#define PIN_BOTON_INICIO 10u

#endif /* PINES_H */
