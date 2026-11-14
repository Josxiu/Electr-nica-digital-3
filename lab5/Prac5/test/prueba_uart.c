/**
 * @file test/prueba_uart.c
 * @brief Prueba de componente del driver uart_tx_irq (sin medición).
 *
 * Se carga sola en la Pico (prueba_uart.uf2). Cada 2 s repite tres pruebas y
 * escribe el resultado por la consola USB. Lo transmitido sale por la UART
 * (GP0) y se revisa en la terminal del adaptador.
 *
 *   P1 - Distintas longitudes: líneas de 2, 10, 32, 33 y 64 bytes.
 *        Se espera: todas aceptadas y recibidas completas.
 *   P2 - Datos pendientes: se encolan 3 líneas seguidas sin esperar.
 *        Se espera: salen en orden y sin mezclarse.
 *   P3 - Falta de espacio: se intentan encolar 30 líneas de 64 bytes
 *        (1920 B) en un buffer de 511 B, sin esperar.
 *        Se espera: se aceptan las primeras ~7, el resto se rechaza ENTERA
 *        (ninguna línea cortada en la terminal) y el contador de descartes
 *        sube en la misma cantidad.
 *
 * Al final de cada ronda se mide cuánto tarda el buffer en vaciarse, para
 * comparar con el tiempo teórico: bytes · 10 bits / 115200 bit/s.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pico/stdlib.h"

#include "config.h"
#include "uart_tx_irq.h"

/**
 * @brief Arma una línea de exactamente @p largo bytes (incluido el '\n').
 * Formato: "P<prueba>,<num>," seguido de letras hasta completar.
 * @param buf     [salida] destino; debe tener al menos @p largo + 1 bytes.
 * @param prueba  número de la prueba.
 * @param num     número de la línea dentro de la prueba.
 * @param largo   bytes de la línea, incluido el '\n'.
 * @return @p largo.
 */
static size_t armar_linea(char *buf, unsigned prueba, unsigned num, size_t largo)
{
    int n = snprintf(buf, largo + 1u, "P%u,%02u,", prueba, num);
    size_t i = (size_t)n;
    while (i < largo - 1u) {
        buf[i] = (char)('a' + (i % 26u));
        i++;
    }
    buf[largo - 1u] = '\n';
    buf[largo] = '\0';
    return largo;
}

/**
 * @brief Espera (en el main, es solo una prueba) a que el buffer se vacíe.
 * @return tiempo de espera en µs.
 */
static uint32_t esperar_vaciado_us(void)
{
    uint32_t t0 = time_us_32();
    while (uart_tx_irq_pendientes() > 0u) {
        tight_loop_contents();
    }
    return time_us_32() - t0;
}

/** @brief Punto de entrada de la prueba del driver UART.
 *  @return no retorna: termina en un lazo infinito. */
int main(void)
{
    stdio_init_all();
    uart_tx_irq_iniciar(UART_REPORTES, PIN_UART_TX, UART_BAUDIOS, PRIORIDAD_IRQ_UART);

    char linea[REPORTE_MAX_BYTES + 1];
    unsigned ronda = 0u;

    while (true) {
        sleep_ms(2000);
        ronda++;
        printf("\n=== Ronda %u ===\n", ronda);

        /* P1: distintas longitudes */
        const size_t largos[] = { 2u, 10u, 32u, 33u, 64u };
        unsigned ok = 0u;
        for (unsigned i = 0u; i < sizeof largos / sizeof largos[0]; i++) {
            size_t n = armar_linea(linea, 1u, i, largos[i]);
            ok += uart_tx_irq_escribir(linea, n) ? 1u : 0u;
            esperar_vaciado_us();
        }
        printf("P1 longitudes: %u/5 aceptadas\n", ok);

        /* P2: datos pendientes (tres líneas sin esperar entre ellas) */
        ok = 0u;
        for (unsigned i = 0u; i < 3u; i++) {
            size_t n = armar_linea(linea, 2u, i, 40u);
            ok += uart_tx_irq_escribir(linea, n) ? 1u : 0u;
        }
        printf("P2 pendientes: %u/3 aceptadas, pendientes tras encolar=%u B\n",
               ok, (unsigned)uart_tx_irq_pendientes());
        esperar_vaciado_us();

        /* P3: falta de espacio */
        uint32_t descartes_antes = uart_tx_irq_descartes();
        unsigned aceptadas = 0u;
        for (unsigned i = 0u; i < 30u; i++) {
            size_t n = armar_linea(linea, 3u, i, 64u);
            aceptadas += uart_tx_irq_escribir(linea, n) ? 1u : 0u;
        }
        uint32_t rechazadas = uart_tx_irq_descartes() - descartes_antes;
        uint32_t bytes = aceptadas * 64u;
        uint32_t t_vaciado = esperar_vaciado_us();
        uint32_t t_teorico = (uint32_t)((uint64_t)bytes * 10u * 1000000u / UART_BAUDIOS);

        printf("P3 sin espacio: aceptadas=%u rechazadas=%" PRIu32
               " (total descartes=%" PRIu32 ")\n",
               aceptadas, rechazadas, uart_tx_irq_descartes());
        printf("   vaciado: %" PRIu32 " us (teorico ~%" PRIu32 " us), irq_uart=%" PRIu32 "\n",
               t_vaciado, t_teorico, uart_tx_irq_interrupciones());
    }
}
