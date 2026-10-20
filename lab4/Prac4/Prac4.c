/**
 * @file Prac4.c
 * @brief Octavia: aplicación principal.
 *
 * Reparto del trabajo:
 *   - Interrupción del SysTick (FS_HZ = 40 kHz): calcula y saca cada muestra
 *     (sintetizador_muestra()). Así la afinación no depende de lo que tarde
 *     el bucle principal.
 *   - Bucle principal: antirrebote de los botones, elección de la nota y
 *     mensajes por USB.
 *
 * La aplicación no toca registros: el SysTick se maneja solo con el driver
 * systick.h y el DAC con dac.h.
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "pines.h"
#include "entradas.h"
#include "systick.h"
#include "dac.h"
#include "sintetizador.h"

/** Instancias de los 9 botones (7 notas + 2 de octava). */
static boton_t botones[NUM_BOTONES];

#define NUM_NOTAS      7
#define OCTAVA_MIN     3
#define OCTAVA_MAX     5
#define OCTAVA_INICIAL 4

/** Semitonos de cada nota respecto a La4 (f = 440 * 2^(n/12)). */
static const int8_t N_DESDE_LA4[NUM_NOTAS] = { -9, -7, -5, -4, -2, 0, 2 };
static const char *NOMBRE_NOTA[NUM_NOTAS] = { "Do", "Re", "Mi", "Fa", "Sol", "La", "Si" };

/** Paso de fase de cada nota en la octava 4 (se calcula al arrancar). */
static uint32_t paso_oct4[NUM_NOTAS];

static int  octava_vigente = OCTAVA_INICIAL; /**< octava que usarán las próximas notas */
/** Valor de voz_de_nota[] cuando esa nota no está sonando. */
#define SIN_VOZ (-1)

/**
 * Voz del sintetizador que tiene cada nota (índice 0 = Do ... 6 = Si).
 * Sirve para que, al soltar un botón, se suelte justo la voz de esa nota.
 */
static int voz_de_nota[NUM_NOTAS] = { SIN_VOZ, SIN_VOZ, SIN_VOZ, SIN_VOZ,
                                      SIN_VOZ, SIN_VOZ, SIN_VOZ };

/** @brief Calcula el paso de fase de las 7 notas en la octava 4. */
static void notas_init(void)
{
    for (int i = 0; i < NUM_NOTAS; i++) {
        float f = 440.0f * powf(2.0f, N_DESDE_LA4[i] / 12.0f);
        paso_oct4[i] = sintetizador_paso_de(f);
    }
}

/**
 * @brief Paso de fase de una nota en cualquier octava.
 * Subir una octava duplica el paso (<< 1); bajarla lo divide entre 2 (>> 1).
 */
static uint32_t paso_nota(int nota, int octava)
{
    int d = octava - 4;
    return (d >= 0) ? (paso_oct4[nota] << d) : (paso_oct4[nota] >> -d);
}

/** @brief Frecuencia teórica de una nota, solo para mostrarla por USB. */
static float freq_nota(int nota, int octava)
{
    return 440.0f * powf(2.0f, N_DESDE_LA4[nota] / 12.0f + (octava - 4));
}

/** @brief Configura los 3 LEDs de octava como salidas. */
static void leds_init(void)
{
    for (int i = 0; i < 3; i++) {
        gpio_init(PIN_LED_OCTAVA[i]);
        gpio_set_dir(PIN_LED_OCTAVA[i], GPIO_OUT);
    }
}

/** @brief Enciende solo el LED de la octava indicada. */
static void leds_mostrar_octava(int octava)
{
    for (int i = 0; i < 3; i++) {
        gpio_put(PIN_LED_OCTAVA[i], (OCTAVA_MIN + i) == octava);
    }
}

/**
 * @brief Sube (+1) o baja (-1) la octava vigente, sin salirse del rango.
 * @param delta +1 para subir, -1 para bajar.
 *
 * En el límite (OCTAVA_MIN u OCTAVA_MAX) no hace nada. Solo afecta a las
 * notas que se pulsen después; las que ya suenan conservan su octava.
 */
static void cambiar_octava(int delta)
{
    int nueva = octava_vigente + delta;
    if (nueva < OCTAVA_MIN || nueva > OCTAVA_MAX) {
        return;
    }
    octava_vigente = nueva;
    leds_mostrar_octava(octava_vigente);
    printf("[OCTAVA] %d\n", octava_vigente);
}

/**
 * @brief Hace sonar una nota recién pulsada con la octava vigente.
 * @param nota índice de la nota (0 = Do ... 6 = Si).
 *
 * El paso se calcula aquí, una sola vez: por eso la nota conserva su octava
 * aunque después se cambie. Si las voces estaban llenas, el sintetizador
 * reutiliza la más antigua y la nota que la tenía deja de sonar.
 */
static void empezar_nota(int nota)
{
    int voz = sintetizador_tocar(paso_nota(nota, octava_vigente));

    for (int otra = 0; otra < NUM_NOTAS; otra++) {
        if (voz_de_nota[otra] == voz) {          /* esa voz era de otra nota */
            voz_de_nota[otra] = SIN_VOZ;
            printf("[NOTA] %s reemplazada (voces llenas)\n", NOMBRE_NOTA[otra]);
        }
    }
    voz_de_nota[nota] = voz;

    printf("[NOTA] %s%d = %.2f Hz (voz %d)\n", NOMBRE_NOTA[nota], octava_vigente,
           freq_nota(nota, octava_vigente), voz);
}

/**
 * @brief Suelta la voz de una nota cuyo botón se liberó.
 * @param nota índice de la nota (0 = Do ... 6 = Si).
 */
static void terminar_nota(int nota)
{
    sintetizador_soltar(voz_de_nota[nota]);
    voz_de_nota[nota] = SIN_VOZ;
    printf("[NOTA] %s suelta\n", NOMBRE_NOTA[nota]);
}

/**
 * @brief Programa el SysTick a FS_HZ y conecta el sintetizador a su interrupción.
 * @return true si el periodo cupo en el SysTick.
 */
static bool audio_iniciar(void)
{
    uint32_t clk_sys_hz = clock_get_hz(clk_sys);
    uint32_t ticks = clk_sys_hz / FS_HZ;   /* 125 MHz / 40 kHz = 3125 */

    if (!systick_configurar(ticks)) {
        return false;
    }
    systick_registrar_callback(sintetizador_muestra);
    systick_fijar_prioridad(0);            /* la más alta: no la retrasan USB ni GPIO */
    systick_iniciar(true);

    printf("SysTick: clk_sys = %lu Hz, periodo = %lu ciclos, Fs = %.3f Hz\n",
           (unsigned long)clk_sys_hz, (unsigned long)systick_periodo(),
           (double)clk_sys_hz / systick_periodo());
    return true;
}

/**
 * @brief Atiende comandos de una letra por USB (sin bloquear).
 *
 *   - 'i': activa / desactiva la interpolación (comparación de la guía).
 *   - 'f': cuenta las interrupciones del SysTick durante 1 s, medidas con el
 *          TIMER del RP2040, y muestra la frecuencia de muestreo real.
 */
static void atender_comandos(void)
{
    int c = getchar_timeout_us(0);
    if (c == 'i') {
        sintetizador_interpolacion(!sintetizador_interpolando());
        printf("[CFG] Interpolacion %s\n", sintetizador_interpolando() ? "ACTIVA" : "apagada");
    } else if (c == 'f') {
        uint32_t n0 = systick_conteo_interrupciones();
        uint64_t t0 = time_us_64();
        while (time_us_64() - t0 < 1000000u) {
            tight_loop_contents();
        }
        uint32_t n1 = systick_conteo_interrupciones();
        printf("[PRUEBA] Interrupciones en 1 s: %lu (esperado %u)\n",
               (unsigned long)(n1 - n0), FS_HZ);
    }
}

int main(void)
{
    stdio_init_all();
    dac_init();
    sintetizador_init();

    for (int i = 0; i < NUM_BOTONES; i++) {
        boton_init(&botones[i], PIN_BOTON[i]);
    }
    notas_init();
    leds_init();
    leds_mostrar_octava(octava_vigente);

    sleep_ms(1500);                        /* tiempo para abrir el monitor serie */
    printf("\n===================================================\n");
    printf("   Octavia - Sintetizador con SysTick (Fs = %u Hz)\n", FS_HZ);
    printf("   Voces simultaneas: %d\n", SINT_VOCES);
    printf("   Comandos: 'i' interpolacion, 'f' medir Fs\n");
    printf("===================================================\n\n");

    /* Al encender: si un botón ya está presionado, no debe sonar.
       Se leen los botones un rato y se descartan los flancos. */
    uint32_t t_inicio = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - t_inicio < 2 * ENT_T_ANTIRREBOTE) {
        uint32_t e = entradas_muestrear();
        uint32_t t = to_ms_since_boot(get_absolute_time());
        for (int i = 0; i < NUM_BOTONES; i++) {
            boton_leer(&botones[i], e, t);
        }
    }
    for (int i = 0; i < NUM_BOTONES; i++) {
        boton_descartar(&botones[i]);
    }

    if (!audio_iniciar()) {
        printf("[ERROR] El periodo no cabe en el SysTick\n");
        while (true) {
            tight_loop_contents();
        }
    }

    while (true) {
        // ------------------------------------------------------------------
        // 1. LECTURA Y PROCESAMIENTO DE LOS 9 BOTONES (ANTIRREBOTE)
        // ------------------------------------------------------------------
        uint32_t ahora = to_ms_since_boot(get_absolute_time());
        uint32_t entradas = entradas_muestrear();

        for (int i = 0; i < NUM_BOTONES; i++) {
            boton_leer(&botones[i], entradas, ahora);
        }

        // --- Octava (antes que las notas: si llegan juntas, la nota ya usa la nueva) ---
        if (boton_presionado(&botones[BTN_OCT_BAJA])) cambiar_octava(-1);
        if (boton_presionado(&botones[BTN_OCT_SUBE])) cambiar_octava(+1);

        // --- Notas: cada una se atiende por separado (pueden sonar varias) ---
        for (int i = 0; i < NUM_NOTAS; i++) {
            if (boton_presionado(&botones[i])) {
                empezar_nota(i);
            }
            if (voz_de_nota[i] != SIN_VOZ && !boton_esta_presionado(&botones[i])) {
                terminar_nota(i);
            }
        }

        atender_comandos();
    }

    return 0;
}
