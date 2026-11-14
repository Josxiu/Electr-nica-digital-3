/**
 * @file entrada.c
 * @brief Implementación del pin de entrada y de su ISR de flancos.
 */
#include "entrada.h"

#include <stddef.h>
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

static unsigned pin_entrada;                         /**< GPIO configurado en entrada_iniciar(). */
static volatile entrada_callback_t callback = NULL;  /**< Función de la versión que recibe cada flanco. */
static volatile uint32_t conteo_irq = 0u;           /**< Veces que entró la ISR (diagnóstico). */

void entrada_iniciar(unsigned pin)
{
    pin_entrada = pin;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);   /* la señal la impone el generador */
}

bool entrada_leer(void)
{
    return gpio_get(pin_entrada);
}

/**
 * @brief ISR del banco de GPIO (IO_IRQ_BANK0).
 *
 * Pasos, en este orden:
 *   1. Leer el timer PRIMERO: así el instante queda lo más cerca posible
 *      del flanco real (la latencia de entrada es casi constante y se
 *      cancela al restar dos instantes).
 *   2. Leer qué eventos están pendientes (subida, bajada o ambos) y
 *      borrarlos (acknowledge), para que puedan volver a marcarse.
 *   3. Avisar a la versión mediante el callback.
 *
 * Se ubica en RAM (__not_in_flash_func) para no depender de la caché de la
 * flash y reducir la latencia.
 */
static void __not_in_flash_func(entrada_isr)(void)
{
    uint32_t t_us = time_us_32();

    uint32_t eventos = gpio_get_irq_event_mask(pin_entrada)
                     & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
    gpio_acknowledge_irq(pin_entrada, eventos);

    conteo_irq++;

    bool subida;
    bool perdida = false;
    if (eventos == (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
        /* Llegaron dos flancos antes de atender el primero: no se sabe el
         * orden exacto. El nivel actual dice cuál fue el último. */
        perdida = true;
        subida  = gpio_get(pin_entrada);
    } else if (eventos == GPIO_IRQ_EDGE_RISE) {
        subida = true;
    } else if (eventos == GPIO_IRQ_EDGE_FALL) {
        subida = false;
    } else {
        return;   /* interrupción sin eventos de este pin: nada que hacer */
    }

    entrada_callback_t cb = callback;
    if (cb != NULL) {
        cb(subida, t_us, perdida);
    }
}

void entrada_habilitar_irq(entrada_callback_t cb, uint8_t prioridad)
{
    callback = cb;

    /* Borrar eventos viejos antes de habilitar. */
    gpio_acknowledge_irq(pin_entrada, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
    gpio_set_irq_enabled(pin_entrada, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    /* Manejador exclusivo: es el único pin con interrupción en este programa,
     * así se evita el despachador genérico del SDK (más latencia). */
    irq_set_exclusive_handler(IO_IRQ_BANK0, entrada_isr);
    irq_set_priority(IO_IRQ_BANK0, prioridad);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

uint32_t entrada_interrupciones(void)
{
    return conteo_irq;
}
