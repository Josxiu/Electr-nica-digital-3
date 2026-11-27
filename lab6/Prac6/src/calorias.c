/**
 * @file calorias.c
 * @brief Gasto calorico. Ver calorias.h.
 */
#include "calorias.h"

/* Ecuacion de caminata del ACSM: VO2 = 0,1 v + 3,5 (ml/kg/min, v en m/min) */
#define ACSM_COEF_MOVIMIENTO    0.1f
#define ACSM_REPOSO             3.5f
#define KCAL_POR_LITRO_O2       5.0f    /* energia de 1 litro de oxigeno */

#define PASO_POR_ESTATURA       0.415f  /* largo del paso / estatura */

void calorias_iniciar(calorias_t *c, float peso_kg, float paso_m) {
    c->peso_kg = peso_kg;
    c->paso_m = paso_m;
    c->kcal = 0.0f;
}

float calorias_paso_por_estatura(float estatura_m) {
    return PASO_POR_ESTATURA * estatura_m;
}

void calorias_sumar(calorias_t *c, uint32_t n_pasos, float duracion_s) {
    if (duracion_s <= 0.0f || n_pasos == 0) {
        return;
    }

    /* Duracion en minutos, como pide la ecuacion */
    const float minutos = duracion_s / 60.0f;

    /* Velocidad media del grupo de pasos (m/min) */
    const float velocidad = c->paso_m * (float)n_pasos / minutos;

    /* Oxigeno por kilo de peso (ml/kg/min) */
    const float vo2 = ACSM_COEF_MOVIMIENTO * velocidad + ACSM_REPOSO;

    /* Oxigeno total gastado en ese tiempo (litros) */
    const float litros = vo2 * c->peso_kg * minutos / 1000.0f;

    c->kcal += litros * KCAL_POR_LITRO_O2;
}

float calorias_distancia_m(const calorias_t *c, uint32_t n_pasos) {
    return c->paso_m * (float)n_pasos;
}
