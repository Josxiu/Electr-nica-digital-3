/**
 * @file Prac4.c
 * @brief Generador de tonos senoidales en tiempo real (reproducción por presión) 
 *        con SysTick, DAC de 8 bits y escala de octavas acumulativa.
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"

#include "pines.h"
#include "entradas.h"

// --- CONFIGURACIÓN DEL DAC EXTERNO (8 BITS PARALELO) ---
#define DAC_MASK          (0xFFu << DAC_BASE_PIN) // Máscara para GP2..GP9
#define LUT_SIZE          256                     // Muestras por ciclo de la senoidal
#define FREQ_MUESTREO     100000.0f               // 100 kHz (Tasa de actualización SysTick)

// Tabla Look-Up Table (LUT) para la onda senoidal (0 a 255)
static uint8_t lut_seno[LUT_SIZE];

// Variables para la Síntesis Digital Directa (DDS)
static volatile uint32_t acumulador_fase = 0;
static volatile uint32_t paso_fase = 0;

// Instancias para los 5 botones
static boton_t botones[5];

// Frecuencias base de la 4.a octava (f = 440 * 2^(n/12))
#define FREQ_DO4 261.63f  // n = -9
#define FREQ_RE4 293.66f  // n = -7
#define FREQ_MI4 329.63f  // n = -5

// Estado actual del sonido
static float factor_octava = 1.0f;       // Multiplicador acumulativo de octava
static float freq_salida_anterior = -1.0f; // Control para imprimir solo en cambios

/**
 * @brief Llena la tabla de búsqueda (LUT) con una onda senoidal calibrada a 8 bits.
 */
static void init_lut_seno(void) {
    for (int i = 0; i < LUT_SIZE; i++) {
        float angulo = (2.0f * M_PI * i) / LUT_SIZE;
        lut_seno[i] = (uint8_t)((sinf(angulo) + 1.0f) * 127.5f);
    }
}

/**
 * @brief Actualiza el incremento de fase del DDS según la frecuencia en Hz.
 */
static void actualizar_paso_fase(float freq_hz) {
    if (freq_hz <= 0.0f) {
        paso_fase = 0; // Silencio
    } else {
        paso_fase = (uint32_t)((freq_hz * 4294967296.0f) / FREQ_MUESTREO);
    }
}

/**
 * @brief Driver de SysTick mediante acceso DIRECTO a registros usando desplazamientos de bit estándar ARM.
 */
static void systick_driver_init(uint32_t ticks_periodo) {
    systick_hw->csr = 0;                                       // Deshabilitar SysTick
    systick_hw->rvr = ticks_periodo - 1;                       // Cargar período N-1
    systick_hw->cvr = 0;                                       // Limpiar contador actual
    systick_hw->csr = (1u << 2) | (1u << 0);                  // Bit 2 (CLKSOURCE) | Bit 0 (ENABLE)
}

/**
 * @brief Configura los 8 GPIO del bus paralelo para el DAC.
 */
static void dac_init(void) {
    for (int i = 0; i < 8; i++) {
        uint pin = DAC_BASE_PIN + i;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
    }
}

int main(void) {
    stdio_init_all();
    init_lut_seno();
    dac_init();

    // 1. Configurar SysTick para temporizar a FREQ_MUESTREO (100 kHz)
    uint32_t clk_sys_hz = clock_get_hz(clk_sys);
    uint32_t ticks_systick = clk_sys_hz / (uint32_t)FREQ_MUESTREO;
    systick_driver_init(ticks_systick);

    // 2. Inicializar los 5 botones con la estructura de entradas.c
    for (int i = 0; i < 5; i++) {
        boton_init(&botones[i], PIN_BOTON[i]);
    }

    printf("\n===================================================\n");
    printf("   Sintetizador de Notas con SysTick (Modo Piano)\n");
    printf("===================================================\n\n");

    while (true) {
        // ------------------------------------------------------------------
        // 1. TEMPORIZACIÓN DE MUESTRAS POR POLLING AL REGISTRO SYSTICK (CSR)
        // ------------------------------------------------------------------
        if (systick_hw->csr & (1u << 16)) { // Bit 16 = COUNTFLAG
            if (paso_fase > 0) {
                acumulador_fase += paso_fase;
                uint8_t indice = (acumulador_fase >> 24) & (LUT_SIZE - 1);
                
                // Salida simultánea a los 8 bits del DAC en 1 ciclo de reloj
                uint32_t valor_dac = (uint32_t)lut_seno[indice] << DAC_BASE_PIN;
                gpio_put_masked(DAC_MASK, valor_dac);
            } else {
                // Silencio: Punto medio del DAC (1.65 V)
                gpio_put_masked(DAC_MASK, (uint32_t)128 << DAC_BASE_PIN);
            }
        }

        // ------------------------------------------------------------------
        // 2. LECTURA Y PROCESAMIENTO DE LOS 5 BOTONES (ANTIRREBOTE)
        // ------------------------------------------------------------------
        uint32_t ahora = to_ms_since_boot(get_absolute_time());
        uint32_t entradas = entradas_muestrear();

        for (int i = 0; i < 5; i++) {
            boton_leer(&botones[i], entradas, ahora);
        }

        // --- Aumento / Disminución acumulativa de la escala (Botones 4 y 5) ---
        if (boton_presionado(&botones[3])) { // Botón 4: Dividir frecuencia acumulativamente entre 2
            factor_octava *= 0.5f;
            printf("[OCTAVA] Frecuencia reducida a la mitad (Factor acumulado: %.4fx)\n", factor_octava);
        }
        if (boton_presionado(&botones[4])) { // Botón 5: Duplicar frecuencia acumulativamente por 2
            factor_octava *= 2.0f;
            printf("[OCTAVA] Frecuencia duplicada (Factor acumulado: %.4fx)\n", factor_octava);
        }

        // --- Lectura en tiempo real del estado presionado (Botones 1, 2 y 3) ---
        float freq_nota_base = 0.0f; // Por defecto: Silencio si no hay ningún botón presionado

        if (boton_esta_presionado(&botones[0])) {        // Botón 1 presionado -> Do4
            freq_nota_base = FREQ_DO4;
        } else if (boton_esta_presionado(&botones[1])) { // Botón 2 presionado -> Re4
            freq_nota_base = FREQ_RE4;
        } else if (boton_esta_presionado(&botones[2])) { // Botón 3 presionado -> Mi4
            freq_nota_base = FREQ_MI4;
        }

        // Calcular la frecuencia final
        float freq_salida_actual = freq_nota_base * factor_octava;
        actualizar_paso_fase(freq_salida_actual);

        // Imprimir por el puerto USB solo cuando el estado de la salida cambie
        if (freq_salida_actual != freq_salida_anterior) {
            if (freq_salida_actual > 0.0f) {
                printf("[DAC OUTPUT] Reproduciendo: %.2f Hz\n", freq_salida_actual);
            } else {
                printf("[DAC OUTPUT] Silencio\n");
            }
            freq_salida_anterior = freq_salida_actual;
        }
    }

    return 0;
}