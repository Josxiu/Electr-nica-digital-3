/**
 * @file reporte.h
 * @brief Arma las líneas de texto que se envían por la UART.
 *
 * Formato (una línea ASCII terminada en '\n', máximo 64 bytes):
 * @verbatim
 *   n,Δt,f,D,estado
 *   17,100,1000.000,25.0,OK
 *   18,101,NA,NA,SIN_TRANSICIONES
 *   0,0,NA,NA,ESPERANDO
 * @endverbatim
 *   - n:  número de reporte programado (uint32), empieza en 0. Avanza también
 *         cuando un reporte se omite o se descarta, así los huecos en n
 *         muestran qué se perdió.
 *   - Δt: ms desde la medición reportada anterior. En el primero vale 0.
 *   - f:  frecuencia en Hz con 3 decimales.
 *   - D:  ciclo de trabajo en % con 1 decimal.
 *
 * Este módulo solo formatea texto en memoria (snprintf); no envía nada.
 * Quien lo llama decide cómo transmitir la línea.
 */
#ifndef REPORTE_H
#define REPORTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "medicion.h"

/** Estado del generador de reportes y contadores para diagnóstico. */
typedef struct {
    uint32_t n;                 /**< Número del próximo reporte programado. */
    bool     hay_anterior;      /**< Ya se generó al menos un reporte.      */
    uint32_t t_anterior_us;     /**< Instante de la medición anterior.      */
    uint32_t omitidos;          /**< Reportes que no alcanzaron a generarse. */
    uint32_t descartados;       /**< Reportes generados que no cupieron en el buffer de TX. */
} reporte_t;

/**
 * @brief Pone n en 0 y borra los contadores.
 * @param r  estado del generador.
 */
void reporte_iniciar(reporte_t *r);

/**
 * @brief Escribe en @p buf la línea del reporte n y avanza n.
 * @param r      estado del generador.
 * @param res    resultado de la medición.
 * @param t_us   instante en que se cerró la ventana (para Δt).
 * @param buf    [salida] texto terminado en '\n' y '\0'.
 * @param tam    tamaño de @p buf (al menos REPORTE_MAX_BYTES + 1).
 * @return cantidad de bytes de la línea (sin el '\0'), o 0 si no cupo.
 */
size_t reporte_formatear(reporte_t *r, const medicion_resultado_t *res,
                         uint32_t t_us, char *buf, size_t tam);

/**
 * @brief Cuenta un reporte programado que no se alcanzó a generar
 *        (por ejemplo, el main llegó tarde a dos ticks seguidos). Avanza n.
 * @param r  estado del generador.
 */
void reporte_omitir(reporte_t *r);

/**
 * @brief Cuenta un reporte que se generó pero el driver UART no aceptó.
 * @param r  estado del generador.
 */
void reporte_contar_descarte(reporte_t *r);

#endif /* REPORTE_H */
