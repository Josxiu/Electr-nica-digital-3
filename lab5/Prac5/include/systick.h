/**
 * @file systick.h
 * @brief Driver propio del temporizador SysTick del Cortex-M0+ (RP2040).
 *
 * El SysTick es un contador descendente de 24 bits que hace parte del núcleo
 * ARM. Cuenta ciclos del reloj del procesador (clk_sys); al llegar a 0 se
 * recarga con RVR, pone COUNTFLAG en 1 y, si TICKINT está activo, genera la
 * excepción SysTick. El periodo es (RVR + 1) ciclos.
 *
 * Registros usados (base PPB 0xE000E010):
 *   - CSR  (0xE010): ENABLE (bit 0), TICKINT (bit 1), CLKSOURCE (bit 2),
 *                    COUNTFLAG (bit 16, se borra al leer CSR).
 *   - RVR  (0xE014): valor de recarga, 24 bits.
 *   - CVR  (0xE018): valor actual; cualquier escritura lo pone en 0 y borra
 *                    COUNTFLAG.
 *   - SHPR3(0xED20): bits 31:30 = prioridad de la excepción SysTick.
 *
 * Solo este módulo accede a esos registros. Se usan únicamente las cabeceras
 * de estructuras y máscaras del SDK (systick_hw, M0PLUS_*); ninguna función
 * del SDK configura ni opera el SysTick.
 *
 * Uso típico:
 * @code
 *   systick_configurar(3125);               // 125 MHz / 3125 = 40 kHz
 *   systick_registrar_callback(mi_funcion); // se llama en cada periodo
 *   systick_iniciar(true);                  // con interrupción
 * @endcode
 */
#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdbool.h>
#include <stdint.h>

/** Mayor periodo programable: RVR es de 24 bits, periodo = RVR + 1. */
#define SYSTICK_PERIODO_MAX (1u << 24)

/** Función que se ejecuta en cada interrupción del SysTick. */
typedef void (*systick_callback_t)(void);

/**
 * @brief Detiene el SysTick y programa su periodo con el reloj del procesador.
 * @param ticks periodo en ciclos de clk_sys, de 1 a SYSTICK_PERIODO_MAX.
 *              Ejemplo: 3125 ciclos a 125 MHz = 25 us (40 kHz).
 * @return true si se programó; false si @p ticks está fuera de rango (en ese
 *         caso no se modifica ningún registro).
 *
 * Efectos: escribe CSR = 0 (lo deja detenido), RVR = ticks - 1 y limpia CVR,
 * lo que también borra COUNTFLAG. No arranca el contador.
 */
bool systick_configurar(uint32_t ticks);

/**
 * @brief Registra la función a llamar en cada interrupción.
 * @param cb función sin argumentos, o NULL para no llamar nada.
 *
 * Restricción: @p cb corre en modo excepción; debe ser corta (mucho menos que
 * el periodo programado) y no usar printf ni funciones bloqueantes.
 */
void systick_registrar_callback(systick_callback_t cb);

/**
 * @brief Arranca el contador.
 * @param con_interrupcion true para activar TICKINT (llama al callback en cada
 *        periodo); false para trabajar solo con COUNTFLAG (polling).
 *
 * Requiere haber llamado antes a systick_configurar().
 */
void systick_iniciar(bool con_interrupcion);

/** @brief Detiene el contador y desactiva su interrupción (CSR = 0). */
void systick_detener(void);

/**
 * @brief Fija la prioridad de la excepción SysTick en SHPR3.
 * @param prioridad 0 (la más alta) a 3 (la más baja); el M0+ implementa 2 bits.
 *        Valores mayores se recortan a 3.
 */
void systick_fijar_prioridad(uint8_t prioridad);

/**
 * @brief Consulta si el contador llegó a 0 desde la última consulta.
 * @return valor de COUNTFLAG.
 *
 * Efecto de lectura: leer CSR borra COUNTFLAG. Solo indica que hubo al menos
 * una vuelta, no cuántas.
 */
bool systick_hubo_vuelta(void);

/**
 * @brief Lee el valor actual del contador (CVR), útil para medir duraciones
 *        dentro de un periodo.
 * @return ciclos que faltan para la próxima recarga (0 a periodo - 1).
 */
uint32_t systick_leer_actual(void);

/** @return periodo programado en ciclos (RVR + 1). */
uint32_t systick_periodo(void);

/**
 * @brief Cantidad de interrupciones atendidas desde que se inició.
 * @return contador de 32 bits (da la vuelta cada ~29 h a 40 kHz).
 *
 * Sirve para verificar la frecuencia real de interrupción contra un reloj
 * independiente (por ejemplo, el TIMER del RP2040).
 */
uint32_t systick_conteo_interrupciones(void);

#endif /* SYSTICK_H */
