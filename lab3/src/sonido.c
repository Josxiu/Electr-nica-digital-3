#include <stdint.h>
#include "sonido.h"
#include "pines.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

/* Frecuencias en Hz para cada uno de los 4 LEDs (Do4, Mi4, Sol4, Do5) */
static const uint32_t FREC_LEDS[4] = { 262, 330, 392, 523 };

static uint slice_num;

void sonido_init(void)
{
    /* 1. Inicializa el pin como GPIO digital común */
    gpio_init(PIN_BUZZER);
    gpio_set_dir(PIN_BUZZER, GPIO_OUT);
    gpio_put(PIN_BUZZER, 0); /* Forzado a 0V */

    /* 2. Obtiene el slice de PWM correspondiente */
    slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
}

void sonido_reproducir_nota(int idx_led)
{
    if (idx_led < 0 || idx_led > 3) {
        return;
    }

    gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM);

    uint32_t frec = FREC_LEDS[idx_led];
    uint32_t clock_frec = clock_get_hz(clk_sys);
    uint32_t divider = 125;
    uint32_t wrap = clock_frec / (divider * frec) - 1;

    pwm_set_clkdiv(slice_num, (float)divider);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(PIN_BUZZER), wrap / 2);
    pwm_set_enabled(slice_num, true);
}

void sonido_apagar(void)
{
    /* 1. Detiene el generador de PWM */
    pwm_set_enabled(slice_num, false);

    /* 2. Reconecta el pin a GPIO digital normal y lo fuerza a GND (0V) */
    gpio_set_function(PIN_BUZZER, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_BUZZER, GPIO_OUT);
    gpio_put(PIN_BUZZER, 0);
}