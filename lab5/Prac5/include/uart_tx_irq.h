/**
 * @file uart_tx_irq.h
 * @brief Driver propio de TRANSMISIÓN UART por interrupciones.
 *
 * Se usa en las versiones IRQ e híbrida. La versión polling no lo usa.
 *
 * Idea general:
 * @verbatim
 *   productor (main o ISR del SysTick)          ISR de la UART
 *   ──────────────────────────────────          ─────────────────────────
 *   uart_tx_irq_escribir("17,100,...\n")         mientras quepa en la FIFO
 *        │                                       y haya datos en el buffer:
 *        ▼                                           FIFO <- buffer[cola]
 *   ┌──────────────── buffer circular ─────────────┐
 *   │ ... │ 1 │ 7 │ , │ 1 │ 0 │ 0 │ , │ ... │      │──► FIFO HW (32 B) ──► pin TX
 *   └──────────────────────────────────────────────┘
 *          ^cola (la ISR saca)        ^cabeza (el productor mete)
 * @endverbatim
 *
 * Decisiones:
 *   - Los datos se COPIAN al buffer del driver: el llamador puede reutilizar
 *     su arreglo apenas la función retorna (propiedad del buffer = driver).
 *   - Todo o nada: si la línea completa no cabe, no se copia ningún byte y se
 *     devuelve false. Así nunca salen líneas cortadas ni mezcladas.
 *   - Nunca se espera a que haya espacio: ni el productor ni la ISR se
 *     quedan en un while esperando a la UART.
 *   - Activación del servicio: la interrupción de TX solo está habilitada
 *     mientras hay datos pendientes. Cuando el buffer queda vacío, la ISR la
 *     deshabilita; el productor la vuelve a activar al meter datos.
 *
 * Restricción de uso: un solo productor (no llamar escribir() a la vez desde
 * dos contextos distintos).
 */
#ifndef UART_TX_IRQ_H
#define UART_TX_IRQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/uart.h"

/**
 * @brief Configura la UART (8N1, sin control de flujo), el pin TX y la
 *        interrupción. Deja la interrupción de TX deshabilitada hasta que
 *        haya algo que enviar.
 * @param uart       instancia (uart0 o uart1).
 * @param pin_tx     GPIO de transmisión.
 * @param baudios    velocidad en bits/s.
 * @param prioridad  prioridad NVIC (0x00 más alta ... 0xC0 más baja).
 *
 * Contexto: inicialización, antes de habilitar el resto de interrupciones.
 */
void uart_tx_irq_iniciar(uart_inst_t *uart, unsigned pin_tx, unsigned baudios,
                         uint8_t prioridad);

/**
 * @brief Encola @p n bytes para transmitir (todo o nada).
 * @param datos  bytes a enviar (se copian; no hace falta '\0').
 * @param n      cantidad de bytes.
 * @return true si se encolaron todos; false si no había espacio para los
 *         @p n bytes (no se encola ninguno y se cuenta un descarte).
 *
 * Contexto: main o ISR con prioridad menor o igual a la de la UART.
 * No bloquea.
 */
bool uart_tx_irq_escribir(const char *datos, size_t n);

/** @return bytes esperando en el buffer (sin contar los que ya están en la FIFO). */
size_t uart_tx_irq_pendientes(void);

/** @return bytes libres en el buffer en este momento. */
size_t uart_tx_irq_libres(void);

/** @return cantidad de llamadas a escribir() rechazadas por falta de espacio. */
uint32_t uart_tx_irq_descartes(void);

/** @return cantidad de veces que se ejecutó la ISR (para diagnóstico). */
uint32_t uart_tx_irq_interrupciones(void);

#endif /* UART_TX_IRQ_H */
