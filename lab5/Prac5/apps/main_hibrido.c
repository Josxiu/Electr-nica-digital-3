/**
 * @file main_hibrido.c
 * @brief Versión 3 — POLLING + IRQs: las ISR solo atienden el hardware y
 *        avisan; el main hace el cálculo y arma los reportes.
 *
 * Contextos de ejecución:
 * @verbatim
 *   ISR GPIO (0x00)     al_flanco(): guarda {t, subida} en la cola. Nada más.
 *   ISR SysTick (0x80)  al_tick():   ticks_pendientes++ y guarda la hora.
 *   ISR UART (0x40)     driver uart_tx_irq: buffer -> FIFO.
 *
 *   main (while 1):
 *     1. Sacar flancos de la cola -> medicion_registrar_flanco()
 *     2. Si hay tick pendiente   -> cerrar ventana, calcular, formatear,
 *                                   uart_tx_irq_escribir()
 *     3. Consola de diagnóstico (solo al reportar)
 * @endverbatim
 *
 * Diferencia con la versión IRQ: aquí el trabajo pesado (divisiones de
 * 64 bits y snprintf) no ocupa ninguna ISR, así que nunca retrasa la
 * atención de un flanco. A cambio, si el main se atrasa, la cola se llena
 * (se cuenta como pérdida) o se acumulan ticks (se cuentan como omitidos).
 *
 * Datos compartidos y quién los escribe:
 *   - cola de flancos: ISR GPIO mete, main saca (sin bloqueo, ver cola_flancos.h).
 *   - ticks_pendientes / t_ultimo_tick: los escribe la ISR del SysTick; el
 *     main los lee y borra dentro de una sección crítica corta.
 *   - medidor y reportes: solo el main (no hay competencia).
 */
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "config.h"
#include "cola_flancos.h"
#include "diagnostico.h"
#include "entrada.h"
#include "medicion.h"
#include "reporte.h"
#include "systick.h"
#include "uart_tx_irq.h"

static medicion_t medidor;
static reporte_t  reportes;

/* Avisos del SysTick al main. */
static volatile uint32_t ticks_pendientes = 0u;
static volatile uint32_t t_ultimo_tick_us = 0u;

/* La ISR del GPIO recuerda si no pudo guardar un evento, para marcar la
 * pérdida en el siguiente que sí logre guardar (así queda en su lugar). */
static bool perdida_pendiente = false;

/* ------------------------------------------------------------------------ */
/* ISRs: solo atienden el hardware y dejan constancia                       */
/* ------------------------------------------------------------------------ */

/** @brief Contexto IO_IRQ_BANK0. Solo encola el flanco. */
static void al_flanco(bool subida, uint32_t t_us, bool hubo_perdida)
{
    evento_flanco_t e = {
        .t_us    = t_us,
        .subida  = subida,
        .perdida = hubo_perdida || perdida_pendiente,
    };
    if (cola_flancos_meter(&e)) {
        perdida_pendiente = false;
    } else {
        perdida_pendiente = true;    /* cola llena: se perdió este flanco */
    }
}

/** @brief Contexto SysTick. Solo avisa que toca reporte y anota la hora. */
static void al_tick(void)
{
    t_ultimo_tick_us = time_us_32();
    ticks_pendientes++;
}

/* ------------------------------------------------------------------------ */
/* Trabajo diferido (main)                                                  */
/* ------------------------------------------------------------------------ */

/** @brief Pasa al medidor todos los flancos que la ISR dejó en la cola. */
static void procesar_flancos(void)
{
    evento_flanco_t e;
    while (cola_flancos_sacar(&e)) {
        if (e.perdida) {
            medicion_marcar_perdida(&medidor);
        }
        medicion_registrar_flanco(&medidor, e.subida, e.t_us);
    }
}

/** @brief Si se pidió por USB, imprime los contadores de la prueba. */
static void atender_diagnostico(void)
{
    if (diagnostico_pedido()) {
        diagnostico_t d = {
            .reportes_programados = reportes.n,
            .reportes_omitidos    = reportes.omitidos,
            .reportes_descartados = reportes.descartados,
            .flancos_perdidos     = medidor.eventos_perdidos,
            .desbordes_cola       = cola_flancos_desbordes(),
            .irq_gpio             = entrada_interrupciones(),
            .irq_uart             = uart_tx_irq_interrupciones(),
        };
        diagnostico_imprimir("v3_hibrido", &d);
    }
}

/** @brief Si el SysTick pidió reporte, lo arma y lo entrega al driver. */
static void procesar_reporte(void)
{
    /* Leer y borrar el aviso del SysTick sin que cambie a mitad de camino. */
    uint32_t estado_irq = save_and_disable_interrupts();
    uint32_t ticks  = ticks_pendientes;
    uint32_t t_tick = t_ultimo_tick_us;
    ticks_pendientes = 0u;
    restore_interrupts(estado_irq);

    if (ticks == 0u) {
        return;
    }
    /* Si el main se atrasó más de un periodo, los reportes intermedios no
     * se generan: se cuentan como omitidos y n avanza igual. */
    for (uint32_t i = 1u; i < ticks; i++) {
        reporte_omitir(&reportes);
    }

    medicion_ventana_t   ventana;
    medicion_resultado_t resultado;
    medicion_cerrar_ventana(&medidor, t_tick, &ventana);
    medicion_calcular(&ventana, &resultado);

    char   linea[REPORTE_MAX_BYTES + 1];
    size_t largo = reporte_formatear(&reportes, &resultado, t_tick,
                                     linea, sizeof linea);

    if (largo > 0u && !uart_tx_irq_escribir(linea, largo)) {
        reporte_contar_descarte(&reportes);
    }

    atender_diagnostico();
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    diagnostico_iniciar();

    entrada_iniciar(PIN_ENTRADA);
    uart_tx_irq_iniciar(UART_REPORTES, PIN_UART_TX, UART_BAUDIOS, PRIORIDAD_IRQ_UART);

    cola_flancos_iniciar();
    medicion_iniciar(&medidor, time_us_32());
    reporte_iniciar(&reportes);

    uint32_t ciclos = (uint32_t)((uint64_t)clock_get_hz(clk_sys)
                                 * PERIODO_REPORTE_MS / 1000u);
    if (!systick_configurar(ciclos)) {
        diagnostico_error("v3_hibrido", "PERIODO_REPORTE_MS no cabe en el SysTick");
        while (true) { tight_loop_contents(); }
    }
    systick_fijar_prioridad(PRIORIDAD_SYSTICK);
    systick_registrar_callback(al_tick);

    entrada_habilitar_irq(al_flanco, PRIORIDAD_IRQ_GPIO);
    systick_iniciar(true);

    while (true) {
        procesar_flancos();     /* primero los flancos: la ventana queda al día */
        procesar_reporte();
    }
}
