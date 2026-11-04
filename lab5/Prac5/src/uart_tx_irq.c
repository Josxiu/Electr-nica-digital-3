/**
 * @file uart_tx_irq.c
 * @brief Implementación del driver de transmisión UART por interrupciones.
 *
 * Registros del PL011 (UART del RP2040) que se usan:
 *   - UARTDR   : escribir un byte lo mete en la FIFO de TX.
 *   - UARTFR   : bit TXFF = FIFO de TX llena (se consulta con uart_is_writable).
 *   - UARTIMSC : bit TXIM habilita la interrupción de TX.
 *   - UARTIFLS : TXIFLSEL fija el nivel de la FIFO que dispara la interrupción.
 *
 * La interrupción de TX se activa cuando la FIFO BAJA hasta el nivel
 * programado (aquí, 1/2 = 16 bytes). Si la FIFO ya estaba vacía cuando se
 * habilita TXIM, ese "cruce" no ocurre y la interrupción no llega sola.
 * Por eso, al meter datos con el servicio inactivo, el productor deja la
 * interrupción PENDIENTE en el NVIC (irq_set_pending): la ISR corre de
 * inmediato y empieza a llenar la FIFO. Desde ahí, cada vez que la FIFO baja
 * a 16 bytes, la ISR vuelve a correr y la rellena.
 */
#include "uart_tx_irq.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "config.h"

/* El índice se recorta con una máscara, por eso el tamaño debe ser 2^k. */
#if (UART_TX_BUFFER_BYTES & (UART_TX_BUFFER_BYTES - 1u)) != 0
#error "UART_TX_BUFFER_BYTES debe ser potencia de 2"
#endif
#define MASCARA (UART_TX_BUFFER_BYTES - 1u)

/* ------------------------------------------------------------------------ */
/* Estado del driver                                                        */
/* ------------------------------------------------------------------------ */

static uart_inst_t *uart_hw_inst;          /* uart0 o uart1                 */
static unsigned     irq_num;               /* UART0_IRQ o UART1_IRQ         */

static uint8_t buffer[UART_TX_BUFFER_BYTES];

/* cabeza: la escribe solo el productor.  cola: la escribe solo la ISR.
 * Ambas son volatile porque la otra parte las lee. Son de 32 bits, así que
 * en el M0+ cada lectura/escritura es atómica. Una celda queda siempre libre
 * para distinguir "lleno" de "vacío". */
static volatile uint32_t cabeza = 0u;
static volatile uint32_t cola   = 0u;

/* true mientras la interrupción de TX está habilitada (servicio activo). */
static volatile bool servicio_activo = false;

static volatile uint32_t descartes      = 0u;
static volatile uint32_t interrupciones = 0u;

/* ------------------------------------------------------------------------ */
/* Funciones auxiliares                                                     */
/* ------------------------------------------------------------------------ */

static inline uint32_t bytes_ocupados(void)
{
    return (cabeza - cola) & MASCARA;
}

static inline void habilitar_irq_tx(bool habilitar)
{
    if (habilitar) {
        hw_set_bits(&uart_get_hw(uart_hw_inst)->imsc, UART_UARTIMSC_TXIM_BITS);
    } else {
        hw_clear_bits(&uart_get_hw(uart_hw_inst)->imsc, UART_UARTIMSC_TXIM_BITS);
    }
}

/* ------------------------------------------------------------------------ */
/* Rutina de servicio de interrupción                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief ISR de la UART: pasa bytes del buffer a la FIFO de TX.
 *
 * Solo transfiere lo que YA cabe en la FIFO (consulta TXFF) y sale; no espera
 * a que aparezca espacio nuevo. Si el buffer queda vacío, apaga el servicio.
 * Contexto: interrupción UARTx_IRQ.
 */
static void uart_tx_isr(void)
{
    interrupciones++;

    uint32_t c = cola;
    while (c != cabeza && uart_is_writable(uart_hw_inst)) {
        uart_get_hw(uart_hw_inst)->dr = buffer[c];
        c = (c + 1u) & MASCARA;
    }
    cola = c;

    if (c == cabeza) {
        /* No queda nada: apagar el servicio hasta la próxima escritura. */
        habilitar_irq_tx(false);
        servicio_activo = false;
    }
    /* La bandera de interrupción de TX se borra sola al escribir en la FIFO
     * por encima del nivel; si no se escribió nada, TXIM quedó apagado. */
}

/* ------------------------------------------------------------------------ */
/* Funciones públicas                                                       */
/* ------------------------------------------------------------------------ */

void uart_tx_irq_iniciar(uart_inst_t *uart, unsigned pin_tx, unsigned baudios,
                         uint8_t prioridad)
{
    uart_hw_inst = uart;
    irq_num      = (uart == uart0) ? UART0_IRQ : UART1_IRQ;

    cabeza = 0u;
    cola   = 0u;
    servicio_activo = false;
    descartes       = 0u;
    interrupciones  = 0u;

    /* Inicialización con el SDK (permitido por la guía). */
    uart_init(uart, baudios);
    gpio_set_function(pin_tx, GPIO_FUNC_UART);
    uart_set_format(uart, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart, false, false);
    uart_set_fifo_enabled(uart, true);

    /* Interrupción de TX cuando la FIFO baja a 1/2 (16 de 32 bytes):
     * TXIFLSEL = 0b010. A 115200 bit/s quedan ~1,4 ms de margen para que la
     * ISR alcance a correr antes de que la FIFO se vacíe. */
    hw_write_masked(&uart_get_hw(uart)->ifls,
                    2u << UART_UARTIFLS_TXIFLSEL_LSB,
                    UART_UARTIFLS_TXIFLSEL_BITS);

    /* Solo nos interesa TX: dejar todas las demás fuentes apagadas. */
    uart_get_hw(uart)->imsc = 0u;

    irq_set_exclusive_handler(irq_num, uart_tx_isr);
    irq_set_priority(irq_num, prioridad);
    irq_set_enabled(irq_num, true);
}

bool uart_tx_irq_escribir(const char *datos, size_t n)
{
    /* Espacio libre: tamaño - 1 (una celda siempre vacía) - ocupados.
     * Leer 'cola' mientras la ISR la avanza solo puede dar MENOS espacio del
     * real, nunca más, así que la comprobación es segura. */
    uint32_t libres = (UART_TX_BUFFER_BYTES - 1u) - bytes_ocupados();
    if (n == 0u) {
        return true;
    }
    if (n > libres) {
        descartes++;
        return false;              /* todo o nada: no se copia ningún byte */
    }

    /* Copiar a partir de 'cabeza'. La ISR no toca estas celdas porque
     * todavía no son visibles (cabeza aún no avanzó). */
    uint32_t h = cabeza;
    for (size_t i = 0u; i < n; i++) {
        buffer[h] = (uint8_t)datos[i];
        h = (h + 1u) & MASCARA;
    }

    /* Publicar los datos y, si el servicio estaba apagado, encenderlo.
     * Sección crítica corta para que la ISR no apague el servicio justo
     * entre la comprobación y el encendido. */
    uint32_t estado_irq = save_and_disable_interrupts();
    cabeza = h;
    if (!servicio_activo) {
        servicio_activo = true;
        habilitar_irq_tx(true);
        irq_set_pending(irq_num);   /* arranque: la ISR llena la FIFO */
    }
    restore_interrupts(estado_irq);

    return true;
}

size_t uart_tx_irq_pendientes(void)
{
    return bytes_ocupados();
}

size_t uart_tx_irq_libres(void)
{
    return (UART_TX_BUFFER_BYTES - 1u) - bytes_ocupados();
}

uint32_t uart_tx_irq_descartes(void)
{
    return descartes;
}

uint32_t uart_tx_irq_interrupciones(void)
{
    return interrupciones;
}
