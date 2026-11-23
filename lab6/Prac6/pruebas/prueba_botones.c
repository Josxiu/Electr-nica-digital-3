/**
 * @file prueba_botones.c
 * @brief Prueba de los botones por el monitor serie.
 *
 * Imprime cada evento (boton, corto o largo) para comprobar el cableado, el
 * antirrebote y el umbral de pulsacion larga. No es parte del programa final.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "config.h"
#include "botones.h"

/** Duerme hasta la proxima interrupcion si no hay eventos pendientes. */
static void dormir(void) {
    const uint32_t estado = save_and_disable_interrupts();
    if (!botones_hay_evento()) {
        __wfi();
    }
    restore_interrupts(estado);
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                 /* tiempo para abrir el monitor serie */
    botones_iniciar();
    printf("Prueba de botones: MODO en GP%u, MAS en GP%u, MENOS en GP%u\n",
           BOTON_PIN_MODO, BOTON_PIN_MAS, BOTON_PIN_MENOS);

    uint32_t cuenta = 0;
    while (true) {
        boton_evento_t ev;
        if (botones_tomar_evento(&ev)) {
            cuenta++;
            static const char *const nombres[BOTONES_N] = { "MODO", "MAS", "MENOS" };
            printf("%lu: %s %s\n", (unsigned long)cuenta, nombres[ev.boton],
                   ev.largo ? "largo" : "corto");
        }
        dormir();
    }
}
