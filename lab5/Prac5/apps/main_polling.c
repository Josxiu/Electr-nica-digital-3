/**
 * @file main_polling.c
 * @brief Versión 1 — POLLING: todo el trabajo se hace en el ciclo principal.
 *
 * Flujo:
 * @verbatim
 *   while (1)
 *   ├── leer el pin de entrada y el timer
 *   │     └── ¿cambió el nivel?            -> medicion_registrar_flanco()
 *   └── ¿COUNTFLAG del SysTick en 1?       (consulta, sin interrupción)
 *         ├── ¿se saltó algún tick?        -> reporte_omitir()
 *         ├── medicion_cerrar_ventana() + medicion_calcular()
 *         ├── reporte_formatear()
 *         ├── uart_write_blocking()        (envío por polling del SDK)
 *         ├── consola de diagnóstico
 *         └── medicion_revisar_tramo_ciego()
 * @endverbatim
 *
 * El SysTick usa el mismo driver y el mismo periodo que en las otras dos
 * versiones, pero aquí sin TICKINT: el ciclo consulta COUNTFLAG. Así las tres
 * versiones tienen la misma base de tiempo para los reportes.
 *
 * No hay interrupciones de aplicación: ni del GPIO, ni del SysTick, ni de la
 * UART. (Si se compila con consola USB, la pila USB del SDK sí usa sus
 * interrupciones; ver CONSOLA_USB en config.h.)
 *
 * Tramo ciego: mientras se arma y envía el reporte, el programa no mira la
 * entrada. Si en ese tramo cabe un pulso entero, los dos flancos pasan sin
 * verse. medicion_revisar_tramo_ciego() descarta el periodo en curso cuando
 * el tramo dura más que el pulso más corto conocido.
 */
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"

#include "config.h"
#include "diagnostico.h"
#include "entrada.h"
#include "medicion.h"
#include "reporte.h"
#include "systick.h"

static medicion_t medidor;   /**< Estado del medidor. */
static reporte_t  reportes;  /**< Generador de reportes. */

/**
 * @brief Configura la UART para enviar por polling con las funciones del SDK.
 * (Las versiones 2 y 3 usan en cambio el driver propio uart_tx_irq.)
 */
static void uart_iniciar_polling(void)
{
    uart_init(UART_REPORTES, UART_BAUDIOS);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    uart_set_format(UART_REPORTES, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UART_REPORTES, false, false);
    uart_set_fifo_enabled(UART_REPORTES, true);
}

/** @brief Si se pidió por USB, imprime los contadores de la prueba. */
static void atender_diagnostico(void)
{
    if (diagnostico_pedido()) {
        diagnostico_t d = {
            .reportes_programados = reportes.n,
            .reportes_omitidos    = reportes.omitidos,
            .flancos_perdidos     = medidor.eventos_perdidos,
            .cortes_polling       = medidor.cortes_tramo_ciego,
        };
        diagnostico_imprimir("v1_polling", &d);
    }
}

/**
 * @brief Arma el reporte de la ventana que acaba de cerrar y lo envía.
 * @param ahora_us  instante del cierre (lectura del timer de esta vuelta).
 */
static void generar_reporte(uint32_t ahora_us)
{
    medicion_ventana_t   ventana;
    medicion_resultado_t resultado;
    medicion_cerrar_ventana(&medidor, ahora_us, &ventana);
    medicion_calcular(&ventana, &resultado);

    char   linea[REPORTE_MAX_BYTES + 1];
    size_t largo = reporte_formatear(&reportes, &resultado, ahora_us,
                                     linea, sizeof linea);

    /* uart_write_blocking espera si la FIFO de 32 bytes está llena. Con
     * líneas cortas y 10 reportes/s casi nunca espera; su efecto se evalúa
     * al subir la tasa de reportes. */
    if (largo > 0u) {
        uart_write_blocking(UART_REPORTES, (const uint8_t *)linea, largo);
    }
}

/** @brief Punto de entrada de la versión por polling.
 *  @return no retorna: termina en un lazo infinito. */
int main(void)
{
    diagnostico_iniciar();
    entrada_iniciar(PIN_ENTRADA);
    uart_iniciar_polling();

    uint32_t ciclos = (uint32_t)((uint64_t)clock_get_hz(clk_sys)
                                 * PERIODO_REPORTE_MS / 1000u);
    if (!systick_configurar(ciclos)) {
        diagnostico_error("v1_polling", "PERIODO_REPORTE_MS no cabe en el SysTick");
        while (true) { tight_loop_contents(); }
    }

    uint32_t t_ultimo_tick = time_us_32();
    medicion_iniciar(&medidor, t_ultimo_tick);
    reporte_iniciar(&reportes);
    systick_iniciar(false);           /* sin TICKINT: se consulta COUNTFLAG */

    bool nivel_anterior = entrada_leer();

    while (true) {
        /* ---- 1. Mirar la entrada ---------------------------------------- */
        bool     nivel = entrada_leer();
        uint32_t ahora = time_us_32();

        if (nivel != nivel_anterior) {
            /* nivel == true -> subida (0 a 1); false -> bajada (1 a 0) */
            medicion_registrar_flanco(&medidor, nivel, ahora);
            nivel_anterior = nivel;
        }

        /* ---- 2. ¿Ya toca reporte? (COUNTFLAG se borra al leerlo) -------- */
        if (!systick_hubo_vuelta()) {
            continue;
        }

        /* COUNTFLAG solo dice "hubo al menos una vuelta". Si el ciclo se
         * atrasó más de un periodo, el tiempo transcurrido dice cuántos
         * ticks se saltaron; esos reportes cuentan como omitidos. */
        uint32_t ticks = (ahora - t_ultimo_tick + PERIODO_REPORTE_US / 2u)
                         / PERIODO_REPORTE_US;
        for (uint32_t i = 1u; i < ticks; i++) {
            reporte_omitir(&reportes);
        }
        t_ultimo_tick = ahora;

        /* ---- 3. Reporte (tramo ciego: no se mira la entrada) ------------ */
        generar_reporte(ahora);
        atender_diagnostico();

        medicion_revisar_tramo_ciego(&medidor, time_us_32() - ahora);
    }
}
