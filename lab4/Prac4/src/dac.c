/**
 * @file dac.c
 * @brief Implementación del bus paralelo del DAC.
 */
#include "dac.h"
#include "pines.h"
#include "hardware/gpio.h"

/** Máscara de los 8 GPIO del bus dentro de la palabra de salidas. */
#define DAC_MASCARA (0xFFu << DAC_BASE_PIN)

void dac_init(void)
{
    gpio_init_mask(DAC_MASCARA);
    gpio_set_dir_out_masked(DAC_MASCARA);
    dac_escribir(DAC_MEDIO);
}

void dac_escribir(uint8_t codigo)
{
    gpio_put_masked(DAC_MASCARA, (uint32_t)codigo << DAC_BASE_PIN);
}
