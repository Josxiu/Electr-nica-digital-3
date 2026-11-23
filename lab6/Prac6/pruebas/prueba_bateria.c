/**
 * @file prueba_bateria.c
 * @brief Calibracion de la medida de la bateria por el monitor serie.
 *
 * Imprime cada segundo las cuentas del ADC, VSYS, el voltaje por pila y el
 * porcentaje.
 *
 * Calibracion: se hace solo con el USB y SIN las pilas (no se conectan las
 * dos cosas a la vez). Con el USB, VSYS queda en unos 4,7 a 5 V; se mide con
 * el multimetro en el pin 39 (VSYS) y GND, y en BATERIA_CORRECCION se pone
 * el cociente multimetro / VSYS impreso. Asi se corrige la cadena divisor +
 * ADC, que es la misma con las pilas. No es parte del programa final.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "config.h"
#include "bateria.h"
#include "bateria_adc.h"

/** Duerme hasta la proxima interrupcion si no hay muestra pendiente. */
static void dormir(void) {
    const uint32_t estado = save_and_disable_interrupts();
    if (!bateria_adc_hay_muestra()) {
        __wfi();
    }
    restore_interrupts(estado);
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                 /* tiempo para abrir el monitor serie */

    bateria_t bateria;
    bateria_iniciar(&bateria);
    bateria_adc_iniciar();
    printf("cuentas, VSYS (V), por pila (V), por pila promediado (V), %%\n");

    while (true) {
        uint16_t crudo;
        if (bateria_adc_tomar(&crudo)) {
            bateria_agregar(&bateria, crudo);
            const float v_celda = bateria_voltaje_celda(crudo);
            printf("%u, %.3f, %.3f, %.3f, %u\n", (unsigned)crudo,
                   v_celda * BATERIA_N_CELDAS, v_celda, bateria.v_celda, (unsigned)bateria.pct);
        }
        dormir();
    }
}
