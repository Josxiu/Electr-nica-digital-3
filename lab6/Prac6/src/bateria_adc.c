/**
 * @file bateria_adc.c
 * @brief ADC de la bateria por interrupciones. Ver bateria_adc.h.
 */
#include "bateria_adc.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

static repeating_timer_t timer_bateria;
static volatile uint16_t ultima = 0;
static volatile bool hay_muestra = false;
static bool primera = true;     /* solo la toca la ISR */

/** Timer (corre en interrupcion): arranca una conversion y sale. */
static bool pedir_conversion(repeating_timer_t *t) {
    (void)t;
    hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);
    return true;
}

/**
 * ISR de la FIFO: la conversion termino. Leer la FIFO baja la interrupcion.
 * La primera conversion se bota: sale antes de que la entrada se estabilice
 * (en la placa dio ~1066 cuentas con VSYS en ~1970) y arrancaria mal el promedio.
 */
static void isr_adc(void) {
    const uint16_t dato = adc_fifo_get();
    if (primera) {
        primera = false;
        return;
    }
    ultima = dato;
    hay_muestra = true;
}

void bateria_adc_iniciar(void) {
    adc_init();
    adc_gpio_init(BATERIA_PIN);
    adc_select_input(BATERIA_ADC_ENTRADA);

    /* FIFO con interrupcion desde 1 dato, sin DMA y con los 12 bits */
    adc_fifo_setup(true, false, 1, false, false);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, isr_adc);
    irq_set_priority(ADC_IRQ_FIFO, PRIORIDAD_IRQ_ADC);
    adc_irq_set_enabled(true);
    irq_set_enabled(ADC_IRQ_FIFO, true);

    pedir_conversion(NULL);     /* la que se descarta; la primera buena llega con el timer */
    add_repeating_timer_ms(-(int32_t)BATERIA_PERIODO_MS, pedir_conversion, NULL, &timer_bateria);
}

bool bateria_adc_hay_muestra(void) {
    return hay_muestra;
}

bool bateria_adc_tomar(uint16_t *crudo) {
    if (!hay_muestra) {
        return false;
    }
    const uint32_t estado = save_and_disable_interrupts();
    *crudo = ultima;
    hay_muestra = false;
    restore_interrupts(estado);
    return true;
}
