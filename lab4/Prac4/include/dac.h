/**
 * @file dac.h
 * @brief DAC externo de 8 bits con entrada paralela (R-2R u otro DAC paralelo).
 *
 * Los 8 bits van en GPIO consecutivos, DAC_BASE_PIN (bit 0, LSB) a
 * DAC_BASE_PIN + 7 (bit 7, MSB), y se escriben juntos con una sola operación
 * enmascarada, de modo que todos cambian en el mismo instante.
 */
#ifndef DAC_H
#define DAC_H

#include <stdint.h>

/** Código de salida que corresponde a la mitad de la escala (silencio). */
#define DAC_MEDIO 128u

/**
 * @brief Configura los 8 GPIO del bus como salidas y deja la salida en DAC_MEDIO.
 */
void dac_init(void);

/**
 * @brief Escribe un código en el DAC.
 * @param codigo 0 (0 V) a 255 (escala completa).
 *
 * Tarda unos pocos ciclos; se puede llamar desde una interrupción.
 */
void dac_escribir(uint8_t codigo);

#endif /* DAC_H */
