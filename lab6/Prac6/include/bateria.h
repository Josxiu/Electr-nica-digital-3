/**
 * @file bateria.h
 * @brief Porcentaje de energia restante de 3 pilas AA alcalinas a partir del voltaje.
 *
 * Solo calculo, no usa el SDK: se puede probar en el PC.
 *
 * El voltaje por pila sale de las cuentas del ADC (divisor de VSYS y factor
 * de calibracion), se promedia con una constante de tiempo BATERIA_TAU_S y
 * se pasa a porcentaje con una tabla de energia restante medida bajo carga
 * (pilas alcalinas AA descargadas a 0,05 W por pila). Entre los puntos de la
 * tabla se interpola en linea recta. El punto de 1,5 V = 100 % es un
 * supuesto: la tabla medida empieza en 1,3 V.
 */
#ifndef BATERIA_H
#define BATERIA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float v_celda;      /**< Voltaje por pila, promediado (V). */
    uint8_t pct;        /**< Energia restante (0 a 100 %). */
    bool iniciado;      /**< Ya llego la primera muestra. */
} bateria_t;

/** Deja todo en cero, a la espera de la primera muestra. */
void bateria_iniciar(bateria_t *b);

/**
 * @brief Pasa las cuentas del ADC a voltaje por pila.
 * @param crudo cuentas del ADC (0 a 4095).
 * @return voltaje promedio por pila (V).
 */
float bateria_voltaje_celda(uint16_t crudo);

/**
 * @brief Energia restante segun el voltaje por pila (tabla interpolada).
 * @param v_celda voltaje por pila bajo carga (V).
 * @return porcentaje de 0 a 100.
 */
uint8_t bateria_porcentaje(float v_celda);

/**
 * @brief Agrega una muestra (una por BATERIA_PERIODO_MS) y actualiza el porcentaje.
 * @param b     estado de la bateria.
 * @param crudo cuentas del ADC.
 */
void bateria_agregar(bateria_t *b, uint16_t crudo);

#endif /* BATERIA_H */
