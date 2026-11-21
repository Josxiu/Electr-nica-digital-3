/**
 * @file calorias.h
 * @brief Gasto calorico al caminar con la ecuacion de caminata del ACSM.
 *
 * Solo calculo, no usa el SDK: se puede probar en el PC.
 *
 * La energia se suma por pasos: cada grupo de pasos aporta lo del
 * movimiento (segun el largo del paso) mas lo del reposo durante el tiempo
 * que duraron. Entre marchas no se suma nada. Estima el gasto total.
 */
#ifndef CALORIAS_H
#define CALORIAS_H

#include <stdint.h>

typedef struct {
    float peso_kg;      /**< Peso del usuario (kg). */
    float paso_m;       /**< Largo del paso (m). */
    float kcal;         /**< Calorias acumuladas en la sesion. */
} calorias_t;

/** Guarda los datos del usuario y pone el acumulado en cero. */
void calorias_iniciar(calorias_t *c, float peso_kg, float paso_m);

/**
 * @brief Suma el gasto calorico de un grupo de pasos.
 * @param c          datos del usuario y acumulado.
 * @param n_pasos    cantidad de pasos del grupo.
 * @param duracion_s duracion de esos pasos (s).
 */
void calorias_sumar(calorias_t *c, uint32_t n_pasos, float duracion_s);

#endif /* CALORIAS_H */
