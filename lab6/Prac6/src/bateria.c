/**
 * @file bateria.c
 * @brief Porcentaje de bateria. Ver bateria.h.
 */
#include "bateria.h"
#include "config.h"

#include <stddef.h>

#define ADC_CUENTAS     4096.0f

/* Energia restante (%) segun el voltaje por pila bajo carga, columna de
 * 0,05 W por pila (Duracell Plus Power AA, lygte-info.dk). El ultimo punto
 * (1,5 V = 100 %) es un supuesto. Por debajo del primer punto: 0 %. */
static const float tabla_v[]   = { 0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.10f, 1.20f, 1.30f, 1.50f };
static const float tabla_pct[] = { 0.5f,  1.2f,  2.5f,  5.2f,  8.3f,  17.0f, 26.0f, 56.0f, 100.0f };
#define TABLA_N  (sizeof(tabla_v) / sizeof(tabla_v[0]))

/* Promedio exponencial con una muestra por periodo */
#define PERIODO_S       (BATERIA_PERIODO_MS / 1000.0f)
#define ALFA            (PERIODO_S / (BATERIA_TAU_S + PERIODO_S))

void bateria_iniciar(bateria_t *b) {
    b->v_celda = 0.0f;
    b->pct = 0;
    b->iniciado = false;
}

float bateria_voltaje_celda(uint16_t crudo) {
    const float v_pin = (float)crudo * ADC_VREF_V / ADC_CUENTAS;
    const float v_vsys = v_pin * BATERIA_DIVISOR * BATERIA_CORRECCION;
    return v_vsys / (float)BATERIA_N_CELDAS;
}

uint8_t bateria_porcentaje(float v_celda) {
    if (v_celda < tabla_v[0]) {
        return 0;
    }
    if (v_celda >= tabla_v[TABLA_N - 1]) {
        return 100;
    }
    size_t i = 1;
    while (v_celda >= tabla_v[i]) {
        i++;
    }
    /* Recta entre el punto i - 1 y el punto i */
    const float fraccion = (v_celda - tabla_v[i - 1]) / (tabla_v[i] - tabla_v[i - 1]);
    const float pct = tabla_pct[i - 1] + fraccion * (tabla_pct[i] - tabla_pct[i - 1]);
    return (uint8_t)(pct + 0.5f);
}

void bateria_agregar(bateria_t *b, uint16_t crudo) {
    const float v = bateria_voltaje_celda(crudo);
    if (!b->iniciado) {
        b->v_celda = v;         /* la primera muestra arranca el promedio */
        b->iniciado = true;
    } else {
        b->v_celda += ALFA * (v - b->v_celda);
    }
    b->pct = bateria_porcentaje(b->v_celda);
}
