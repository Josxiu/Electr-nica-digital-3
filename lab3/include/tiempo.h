/**
 * @file tiempo.h
 * @brief Base de tiempo comun del programa.
 *
 * Todo el juego se resuelve comparando marcas de tiempo en milisegundos; no
 * hay una sola espera bloqueante. Esta funcion sustituye a millis() de
 * Arduino y permite que la aritmetica de tiempos de la practica anterior se
 * conserve sin cambios.
 *
 * El valor se toma del temporizador de 64 bits del RP2040 y se trunca a 32
 * bits, de modo que desborda cada ~49,7 dias. Las restas entre marcas se
 * hacen en aritmetica sin signo, por lo que el desbordamiento no afecta a los
 * intervalos medidos.
 */
#ifndef TIEMPO_H
#define TIEMPO_H

#include <stdint.h>
#include "pico/time.h"

/**
 * @brief Milisegundos transcurridos desde el arranque.
 * @return Marca de tiempo en ms, truncada a 32 bits.
 */
static inline uint32_t millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

#endif /* TIEMPO_H */
