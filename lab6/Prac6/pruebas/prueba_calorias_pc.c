/**
 * @file prueba_calorias_pc.c
 * @brief Prueba en el PC del modelo calorico (ACSM) y de la distancia estimada.
 *
 * Se compila con gcc, sin el SDK:
 *   gcc -Wall -Wextra -std=c11 -Iinclude pruebas/prueba_calorias_pc.c src/calorias.c -o prueba_calorias
 *
 * Los valores esperados se calcularon a mano con VO2 = 0,1 v + 3,5 (ml/kg/min)
 * y 5 kcal por litro de O2, y d = N * L.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include "calorias.h"

static int fallas = 0;

static void revisar(const char *caso, bool cumple) {
    printf("%-56s %s\n", caso, cumple ? "OK" : "FALLA");
    if (!cumple) {
        fallas++;
    }
}

int main(void) {
    calorias_t c;

    /* Caso A: 70 kg, paso 0,70 m, 6000 pasos en 60 min -> 220,5 kcal */
    calorias_iniciar(&c, 70.0f, 0.70f);
    calorias_sumar(&c, 6000, 3600.0f);
    revisar("Caso A: 70 kg, 6000 pasos en 60 min -> 220,50 kcal",
            fabsf(c.kcal - 220.5f) / 220.5f <= 0.001f);

    /* Caso A': igual a A sumando paso por paso (0,6 s c/u) */
    calorias_iniciar(&c, 70.0f, 0.70f);
    for (int i = 0; i < 6000; i++) {
        calorias_sumar(&c, 1, 0.6f);
    }
    revisar("Caso A': 6000 pasos uno por uno -> dif <= 0,1 %",
            fabsf(c.kcal - 220.5f) / 220.5f <= 0.001f);

    /* Caso B: 80 kg, paso 0,75 m, 3600 pasos en 30 min -> 150,0 kcal */
    calorias_iniciar(&c, 80.0f, 0.75f);
    calorias_sumar(&c, 3600, 1800.0f);
    revisar("Caso B: 80 kg, 3600 pasos en 30 min -> 150,00 kcal",
            fabsf(c.kcal - 150.0f) / 150.0f <= 0.001f);

    /* Caso C: grupo sin pasos o con duracion nula -> 0 kcal */
    calorias_iniciar(&c, 70.0f, 0.70f);
    calorias_sumar(&c, 0, 10.0f);
    calorias_sumar(&c, 10, 0.0f);
    revisar("Caso C: sin pasos o duracion nula -> 0,0 kcal",
            c.kcal == 0.0f);

    /* Largo del paso a partir de la estatura */
    const float paso = calorias_paso_por_estatura(1.70f);
    revisar("Estatura 1,70 m -> paso 0,7055 m",
            fabsf(paso - 0.7055f) < 0.0001f);

    /* Distancia estimada: coherencia en cero, metros y kilometros */
    calorias_iniciar(&c, 70.0f, 0.70f);
    revisar("Distancia con 0 pasos -> 0,0 m",
            calorias_distancia_m(&c, 0) == 0.0f);
    revisar("Distancia con 200 pasos (0,70 m) -> 140,0 m",
            fabsf(calorias_distancia_m(&c, 200) - 140.0f) < 0.01f);
    revisar("Distancia con 2000 pasos (0,70 m) -> 1400,0 m",
            fabsf(calorias_distancia_m(&c, 2000) - 1400.0f) < 0.01f);

    printf("\n%s (%d fallas)\n", fallas == 0 ? "Todo bien" : "Hay fallas", fallas);
    return fallas;
}
