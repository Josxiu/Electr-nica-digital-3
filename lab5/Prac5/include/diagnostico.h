/**
 * @file diagnostico.h
 * @brief Consola de diagnóstico por el USB de la Pico (no por la UART de
 *        reportes). Permite consultar los contadores al terminar una prueba.
 *
 * Uso: abrir el puerto COM de la Pico (no el del adaptador) y enviar la letra
 * 'e'. Se imprime una sola línea con todos los contadores, con el mismo
 * formato en las tres versiones, para copiarla a las tablas del informe:
 * @verbatim
 *   [v3_hibrido] programados=1200 omitidos=0 descartados=0 flancos_perdidos=0
 *                cortes_polling=0 desbordes_cola=0 irq_gpio=24000000 irq_uart=1200
 * @endverbatim
 * (en la consola sale en una sola línea). Un contador que no aplica a la versión queda en 0.
 *
 * Si el proyecto se compila con PRAC5_CONSOLA_USB=OFF, estas funciones no
 * hacen nada y los contadores se leen con el depurador (variables estáticas
 * de cada main).
 *
 * Contexto: solo el ciclo principal. Nunca desde una ISR (printf por USB).
 */
#ifndef DIAGNOSTICO_H
#define DIAGNOSTICO_H

#include <stdbool.h>
#include <stdint.h>

/** Contadores de una prueba, acumulados desde el arranque. */
typedef struct {
    uint32_t reportes_programados;  /**< Reportes que tocaba generar (= n del próximo). */
    uint32_t reportes_omitidos;     /**< No se alcanzaron a generar (main atrasado).   */
    uint32_t reportes_descartados;  /**< Generados, pero sin espacio en el buffer TX.  */
    uint32_t flancos_perdidos;      /**< Pérdidas de flancos detectadas por el medidor. */
    uint32_t cortes_polling;        /**< Periodos descartados por el tramo ciego (v1).  */
    uint32_t desbordes_cola;        /**< Flancos que no cupieron en la cola (v3).      */
    uint32_t irq_gpio;              /**< Interrupciones del GPIO atendidas.            */
    uint32_t irq_uart;              /**< Interrupciones de la UART atendidas.          */
} diagnostico_t;

/** @brief Inicia el stdio por USB (si CONSOLA_USB = 1). */
void diagnostico_iniciar(void);

/**
 * @brief Revisa, sin esperar, si llegó la letra 'e' por la consola USB.
 * @return true si se pidió el diagnóstico.
 */
bool diagnostico_pedido(void);

/**
 * @brief Imprime los contadores en una línea por la consola USB.
 * @param version  nombre de la versión (por ejemplo "v1_polling").
 * @param d        contadores a imprimir.
 */
void diagnostico_imprimir(const char *version, const diagnostico_t *d);

/** @brief Imprime un mensaje de error por la consola USB (si existe). */
void diagnostico_error(const char *version, const char *mensaje);

#endif /* DIAGNOSTICO_H */
