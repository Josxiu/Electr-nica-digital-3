/**
 * @file main_irq.c
 * @brief Versión 2 — IRQs PURAS: todo el trabajo de la aplicación ocurre en
 *        interrupciones. El main solo inicializa y duerme.
 *
 * Contextos de ejecución:
 * @verbatim
 *   ISR GPIO (prioridad 0x00, la más alta)
 *     └── al_flanco(): medicion_registrar_flanco()
 *
 *   ISR SysTick (prioridad 2 = 0x80), cada PERIODO_REPORTE_MS
 *     └── al_tick(): cerrar ventana -> calcular -> formatear -> uart_tx_irq_escribir()
 *
 *   ISR UART (prioridad 0x40)
 *     └── driver uart_tx_irq: pasa bytes del buffer a la FIFO
 *
 *   main
 *     └── while (1) { __wfi(); consola de diagnóstico (no es trabajo de la aplicación); }
 * @endverbatim
 *
 * Datos compartidos: el medidor lo modifican la ISR del GPIO (cada flanco) y
 * la del SysTick (al cerrar la ventana). Como la del GPIO tiene más prioridad
 * y puede interrumpir a la del SysTick, el cierre de la ventana se hace con
 * las interrupciones deshabilitadas (solo se copian unos pocos campos, el
 * cálculo se hace después, ya con las interrupciones activas).
 */
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "config.h"
#include "diagnostico.h"
#include "entrada.h"
#include "medicion.h"
#include "reporte.h"
#include "systick.h"
#include "uart_tx_irq.h"

static medicion_t medidor;
static reporte_t  reportes;

/* ------------------------------------------------------------------------ */
/* Manejadores de la aplicación (corren dentro de ISRs)                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Llamada por la ISR del GPIO en cada flanco.
 * Contexto: IO_IRQ_BANK0. Registra el flanco directamente en el medidor.
 */
static void al_flanco(bool subida, uint32_t t_us, bool hubo_perdida)
{
    if (hubo_perdida) {
        medicion_marcar_perdida(&medidor);
    }
    medicion_registrar_flanco(&medidor, subida, t_us);
}

/**
 * @brief Llamada por la ISR del SysTick cada PERIODO_REPORTE_MS.
 * Contexto: excepción SysTick. Hace todo el reporte: medición, cálculo,
 * formateo y entrega al driver UART (que no bloquea).
 */
static void al_tick(void)
{
    medicion_ventana_t   ventana;
    medicion_resultado_t resultado;

    /* Sección crítica corta: la ISR del GPIO no puede tocar el medidor
     * mientras se copian y reinician los acumuladores. */
    uint32_t estado_irq = save_and_disable_interrupts();
    uint32_t ahora = time_us_32();
    medicion_cerrar_ventana(&medidor, ahora, &ventana);
    restore_interrupts(estado_irq);

    medicion_calcular(&ventana, &resultado);

    char   linea[REPORTE_MAX_BYTES + 1];
    size_t largo = reporte_formatear(&reportes, &resultado, ahora,
                                     linea, sizeof linea);

    if (largo > 0u && !uart_tx_irq_escribir(linea, largo)) {
        /* No cupo la línea completa: se descarta entera (n ya avanzó). */
        reporte_contar_descarte(&reportes);
    }
}

/* ------------------------------------------------------------------------ */
/* Consola de diagnóstico (USB) — no es trabajo de la aplicación             */
/* ------------------------------------------------------------------------ */

/** @brief Si se pidió por USB, imprime los contadores de la prueba. */
static void atender_diagnostico(void)
{
    if (diagnostico_pedido()) {
        /* Copia rápida: las ISR siguen corriendo; un desfase de un conteo
         * entre campos no importa para el diagnóstico. */
        diagnostico_t d = {
            .reportes_programados = reportes.n,
            .reportes_omitidos    = reportes.omitidos,
            .reportes_descartados = reportes.descartados,
            .flancos_perdidos     = medidor.eventos_perdidos,
            .irq_gpio             = entrada_interrupciones(),
            .irq_uart             = uart_tx_irq_interrupciones(),
        };
        diagnostico_imprimir("v2_irq", &d);
    }
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    diagnostico_iniciar();

    entrada_iniciar(PIN_ENTRADA);
    uart_tx_irq_iniciar(UART_REPORTES, PIN_UART_TX, UART_BAUDIOS, PRIORIDAD_IRQ_UART);

    medicion_iniciar(&medidor, time_us_32());
    reporte_iniciar(&reportes);

    /* SysTick: un tick por reporte. 100 ms a 125 MHz = 12 500 000 ciclos,
     * cabe en los 24 bits (máximo 16 777 216 ≈ 134 ms). */
    uint32_t ciclos = (uint32_t)((uint64_t)clock_get_hz(clk_sys)
                                 * PERIODO_REPORTE_MS / 1000u);
    if (!systick_configurar(ciclos)) {
        diagnostico_error("v2_irq", "PERIODO_REPORTE_MS no cabe en el SysTick");
        while (true) { tight_loop_contents(); }
    }
    systick_fijar_prioridad(PRIORIDAD_SYSTICK);
    systick_registrar_callback(al_tick);

    /* Encender las fuentes de interrupción al final, con todo ya listo. */
    entrada_habilitar_irq(al_flanco, PRIORIDAD_IRQ_GPIO);
    systick_iniciar(true);

    /* El ciclo principal no despacha trabajo de la aplicación. */
    while (true) {
        __wfi();              /* dormir hasta la próxima interrupción */
        atender_diagnostico();
    }
}
