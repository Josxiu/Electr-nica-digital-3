/**
 * @file sintetizador.c
 * @brief Implementación del DDS con tabla en Flash, interpolación y rampa.
 */
#include "sintetizador.h"
#include "lut_seno.h"
#include "dac.h"
#include "pines.h"
#include "hardware/gpio.h"

/** Desplazamiento que deja en los bits bajos el índice de la tabla. */
#define DESPL_INDICE (32u - LUT_BITS_INDICE)
/** Desplazamiento que deja en los bits bajos los 8 bits de fracción. */
#define DESPL_FRACCION (DESPL_INDICE - 8u)

static volatile uint32_t paso_fase = 0;      /**< paso de la nota actual          */
static volatile bool     sonando = false;    /**< true mientras la nota esté pulsada */
static volatile bool     interpolar = true;  /**< modo de lectura de la tabla     */
static uint32_t acumulador_fase = 0;         /**< solo lo toca la interrupción    */
static uint32_t amplitud = 0;                /**< 0..SINT_RAMPA_MUESTRAS          */

void sintetizador_init(void)
{
    sonando = false;
    paso_fase = 0;
    interpolar = true;
    gpio_init(PIN_PRUEBA_ISR);
    gpio_set_dir(PIN_PRUEBA_ISR, GPIO_OUT);
}

uint32_t sintetizador_paso_de(float freq_hz)
{
    if (freq_hz <= 0.0f) {
        return 0u;
    }
    return (uint32_t)((double)freq_hz * 4294967296.0 / (double)FS_HZ + 0.5);
}

void sintetizador_tocar(uint32_t paso)
{
    paso_fase = paso;
    sonando = (paso != 0u);
}

void sintetizador_soltar(void)
{
    sonando = false;
}

void sintetizador_interpolacion(bool activa)
{
    interpolar = activa;
}

bool sintetizador_interpolando(void)
{
    return interpolar;
}

void sintetizador_muestra(void)
{
    gpio_put(PIN_PRUEBA_ISR, 1);            /* el ancho del pulso = tiempo de cálculo */

    if (sonando) {
        if (amplitud < SINT_RAMPA_MUESTRAS) amplitud++;
    } else if (amplitud > 0u) {
        amplitud--;
    }

    if (amplitud == 0u) {
        acumulador_fase = 0u;               /* la próxima nota arranca en el cruce por cero */
        dac_escribir(DAC_MEDIO);
        gpio_put(PIN_PRUEBA_ISR, 0);
        return;
    }

    acumulador_fase += paso_fase;
    uint32_t i = acumulador_fase >> DESPL_INDICE;
    int32_t s = (int32_t)lut_seno[i] - (int32_t)DAC_MEDIO;     /* -128..127 */

    if (interpolar) {
        uint32_t frac = (acumulador_fase >> DESPL_FRACCION) & 0xFFu;
        int32_t s2 = (int32_t)lut_seno[(i + 1u) & (LUT_TAMANO - 1u)] - (int32_t)DAC_MEDIO;
        s += ((s2 - s) * (int32_t)frac) / 256;
    }

    /* Escala por la amplitud de la rampa: s * amplitud / SINT_RAMPA_MUESTRAS. */
    s = (s * (int32_t)amplitud) / (int32_t)SINT_RAMPA_MUESTRAS;
    dac_escribir((uint8_t)(s + (int32_t)DAC_MEDIO));

    gpio_put(PIN_PRUEBA_ISR, 0);
}
