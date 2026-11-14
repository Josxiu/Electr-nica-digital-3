/**
 * @file config.h
 * @brief Parámetros del montaje y de la aplicación. Única fuente de verdad.
 *
 * Todo lo que se puede ajustar sin cambiar la lógica está aquí: pines,
 * velocidad de la UART, periodo de reporte, tiempo límite sin flancos,
 * tamaño del buffer de transmisión y prioridades de las interrupciones.
 * Las tres versiones (polling, IRQ e híbrida) usan este mismo archivo, de modo
 * que el montaje y las condiciones de prueba son idénticos entre ellas.
 *
 * Los números de pin son GPIO (GPx), no números de pin físico del conector:
 *   GP0  -> pin 1  (UART0 TX)
 *   GP1  -> pin 2  (UART0 RX, no se usa: solo se transmite)
 *   GP15 -> pin 20 (entrada de la señal a medir)
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ------------------------------------------------------------------------ */
/* Entrada                                                                  */
/* ------------------------------------------------------------------------ */

/** GPIO por donde entra la señal rectangular (0 a 3,3 V). */
#define PIN_ENTRADA             15u

/* ------------------------------------------------------------------------ */
/* UART de reportes                                                         */
/* ------------------------------------------------------------------------ */

/** Instancia de UART del RP2040 usada para los reportes. */
#define UART_REPORTES           uart0

/** GPIO de transmisión. Va al pin RX del adaptador USB-UART. */
#define PIN_UART_TX             0u

/** Velocidad en bits/s. Formato fijo 8N1 sin control de flujo. */
#define UART_BAUDIOS            115200u

/**
 * Tamaño del buffer circular de transmisión, en bytes.
 * Debe ser potencia de 2 (el índice se recorta con una máscara).
 * 512 bytes alcanzan para unas 20 líneas de reporte típicas.
 */
#define UART_TX_BUFFER_BYTES    512u

/* ------------------------------------------------------------------------ */
/* Reportes                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * Periodo entre reportes, en milisegundos (nominal: 100 ms = 10 reportes/s).
 * Lo define CMake (PRAC5_PERIODO_REPORTE_MS). Para la prueba "efecto de la
 * comunicación" se configura con otro valor, por ejemplo:
 *   cmake -B build -DPRAC5_PERIODO_REPORTE_MS=20
 * Con el SysTick a 125 MHz el máximo es 134 ms (24 bits).
 */
#ifndef PERIODO_REPORTE_MS
#define PERIODO_REPORTE_MS      100u
#endif
#if (PERIODO_REPORTE_MS < 1) || (PERIODO_REPORTE_MS > 134)
#error "PERIODO_REPORTE_MS debe estar entre 1 y 134 ms (límite de 24 bits del SysTick a 125 MHz)"
#endif

/** Periodo de reporte en µs (comparaciones con time_us_32). */
#define PERIODO_REPORTE_US      (PERIODO_REPORTE_MS * 1000u)

/** Longitud máxima de una línea de reporte, incluido el '\n' (guía: 64). */
#define REPORTE_MAX_BYTES       64u

/* ------------------------------------------------------------------------ */
/* Medición                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * Si pasan más de este tiempo sin flancos, la medición se invalida y se
 * reporta SIN_TRANSICIONES (guía: 500 ms).
 */
#define TIMEOUT_SIN_FLANCOS_US  500000u

/* ------------------------------------------------------------------------ */
/* Prioridades de interrupción                                              */
/* ------------------------------------------------------------------------ */
/*
 * El Cortex-M0+ solo implementa 2 bits de prioridad: 4 niveles.
 * En el SDK (irq_set_priority) se escriben como 0x00, 0x40, 0x80, 0xC0;
 * en el driver del SysTick (SHPR3) como 0, 1, 2, 3. Menor número = más urgente.
 *
 * Orden elegido:
 *   1. GPIO  (0x00): un flanco no puede esperar; si se atrasa, el timestamp
 *                    queda corrido o se pierde el evento.
 *   2. UART  (0x40): la FIFO de TX tiene margen (16 bytes = 1,4 ms a 115200).
 *   3. SysTick (2 = 0x80): el reporte puede atrasarse unos microsegundos
 *                    sin afectar la tolerancia de ±20 ms.
 */
#define PRIORIDAD_IRQ_GPIO      0x00u  /**< Prioridad NVIC del GPIO (la más alta). */
#define PRIORIDAD_IRQ_UART      0x40u  /**< Prioridad NVIC de la UART de reportes. */
#define PRIORIDAD_SYSTICK       2u     /**< Prioridad de SysTick (campo de 2 bits del SHPR3). */

/* ------------------------------------------------------------------------ */
/* Diagnóstico                                                              */
/* ------------------------------------------------------------------------ */

/**
 * 1 = consola de diagnóstico por el USB de la Pico; 0 = sin USB.
 * Lo define CMake (PRAC5_CONSOLA_USB). La pila USB del SDK usa sus propias
 * interrupciones; en la versión polling pueden interrumpir el ciclo principal
 * unos microsegundos. Para medir el límite de frecuencia sin ese efecto se
 * compila con -DPRAC5_CONSOLA_USB=OFF y los contadores se leen con el depurador.
 */
#ifndef CONSOLA_USB
#define CONSOLA_USB             1
#endif

#endif /* CONFIG_H */
