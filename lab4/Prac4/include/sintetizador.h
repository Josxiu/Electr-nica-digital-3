/**
 * @file sintetizador.h
 * @brief Generación de la señal por síntesis digital directa (DDS).
 *
 * Un acumulador de fase de 32 bits avanza un "paso" en cada muestra. Sus
 * LUT_BITS_INDICE bits altos eligen la muestra de la tabla y los 8 siguientes
 * dan la fracción para la interpolación lineal. La frecuencia de salida es
 *
 *     f = paso * FS_HZ / 2^32   =>   paso = f * 2^32 / FS_HZ
 *
 * Para evitar chasquidos, la amplitud sube y baja en rampa (SINT_RAMPA_MUESTRAS
 * muestras) al iniciar y al soltar una nota.
 *
 * Polifonía: hay SINT_VOCES voces independientes (cada una con su fase, su
 * paso y su rampa). En cada muestra se suman y la suma se divide entre
 * SINT_VOCES, de modo que nunca se desborda el rango del DAC aunque suenen
 * todas a la vez. Si se pulsa una nota y no hay voz libre, se reutiliza la
 * voz que lleva más tiempo asignada. Con SINT_VOCES = 1 el instrumento es
 * monofónico ("la última nota pulsada suena").
 *
 * Reparto de trabajo: sintetizador_muestra() se llama desde la interrupción
 * del SysTick (una vez por muestra); las demás funciones, desde el bucle
 * principal. Comparten solo variables de 32 bits o menos, que el M0+ lee y
 * escribe de forma atómica.
 */
#ifndef SINTETIZADOR_H
#define SINTETIZADOR_H

#include <stdbool.h>
#include <stdint.h>

/** Frecuencia de muestreo, en Hz. 125 MHz / 40 kHz = 3125 ciclos exactos. */
#define FS_HZ 40000u

/** Voces simultáneas. Con 4, cada voz ocupa como máximo 1/4 de la escala. */
#define SINT_VOCES 4

/** Muestras que tarda la rampa de amplitud de 0 a máximo (256 = 6,4 ms). */
#define SINT_RAMPA_MUESTRAS 256u

/** @brief Deja el sintetizador en silencio y con la interpolación activa. */
void sintetizador_init(void);

/**
 * @brief Calcula el paso de fase para una frecuencia.
 * @param freq_hz frecuencia en Hz (> 0 y < FS_HZ / 2).
 * @return paso de 32 bits, redondeado al entero más cercano.
 *
 * Usa coma flotante: llamarla al pulsar una nota, no en cada muestra.
 */
uint32_t sintetizador_paso_de(float freq_hz);

/**
 * @brief Asigna una voz a una nota nueva y la hace sonar.
 * @param paso paso de fase de la nota (ver sintetizador_paso_de()).
 * @return índice de la voz usada (0 a SINT_VOCES - 1).
 *
 * Busca una voz libre (sin nota y con la rampa de salida terminada). Si no
 * hay, reutiliza la asignada hace más tiempo: esa nota cambia de altura sin
 * salto de fase. Quien llama debe olvidar la voz que tenía la nota anterior.
 */
int sintetizador_tocar(uint32_t paso);

/**
 * @brief Suelta la nota de una voz: su amplitud baja en rampa hasta 0.
 * @param voz índice devuelto por sintetizador_tocar(). Fuera de rango: no hace nada.
 */
void sintetizador_soltar(int voz);

/**
 * @brief Activa o desactiva la interpolación lineal entre muestras de la tabla.
 * @param activa true = interpolación lineal; false = muestra más cercana por
 *        truncamiento (para la comparación que pide la guía).
 */
void sintetizador_interpolacion(bool activa);

/** @return true si la interpolación está activa. */
bool sintetizador_interpolando(void);

/**
 * @brief Calcula y escribe en el DAC la siguiente muestra.
 *
 * Pensada para llamarse desde el callback del SysTick, una vez cada 1/FS_HZ.
 */
void sintetizador_muestra(void);

#endif /* SINTETIZADOR_H */
