/**
 * @file bateria_adc.h
 * @brief Lectura del voltaje de la bateria con el ADC por interrupciones.
 *
 * Flujo:
 *   - Timer cada BATERIA_PERIODO_MS: arranca una conversion (escribe un
 *     registro y sale).
 *   - ISR de la FIFO del ADC: unos 2 us despues saca el dato, lo guarda y
 *     marca que hay muestra.
 *   - main: toma la muestra con bateria_adc_tomar().
 *
 * No se usa adc_read(), que espera a que termine la conversion. La primera
 * conversion despues de iniciar se descarta; la primera muestra util llega a
 * los BATERIA_PERIODO_MS.
 */
#ifndef BATERIA_ADC_H
#define BATERIA_ADC_H

#include <stdbool.h>
#include <stdint.h>

/** Configura el ADC en BATERIA_PIN, su interrupcion y el timer; pide la primera muestra. */
void bateria_adc_iniciar(void);

/** @return true si hay una muestra sin tomar. */
bool bateria_adc_hay_muestra(void);

/**
 * @brief Toma la ultima muestra, si hay.
 * @param crudo cuentas del ADC (0 a 4095).
 * @return true si habia una muestra nueva.
 */
bool bateria_adc_tomar(uint16_t *crudo);

#endif /* BATERIA_ADC_H */
