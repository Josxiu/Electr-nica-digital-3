/**
 * @file mpu6050.h
 * @brief Driver de la IMU del módulo GY-521 (MPU-6050 o MPU-6500) por I2C.
 *
 * La configuración del chip se hace una vez, al arrancar, con las funciones
 * bloqueantes del SDK. De ahí en adelante las lecturas van por interrupciones
 * y ninguna función espera al bus:
 *
 *   1. mpu6050_pedir_lectura() carga la transferencia completa en la FIFO del
 *      I2C y retorna. Se puede llamar desde una ISR (un temporizador o el pin
 *      INT del chip).
 *   2. Cuando el bus termina, la ISR del I2C guarda los 14 bytes.
 *   3. El ciclo principal recoge la lectura con mpu6050_tomar_lectura().
 *
 * Este archivo no incluye nada del SDK, así que imu.c se puede compilar en el PC.
 *
 * Configuración del chip: giroscopio a +-500 grados/s, acelerómetro a +-2 g,
 * filtro pasa-bajos de ~44 Hz y muestreo a MPU_FRECUENCIA_HZ.
 * Pines, dirección y velocidad del bus están en config.h.
 */
#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>

/** Una lectura de la IMU en unidades físicas. */
typedef struct {
    float ax, ay, az;     /**< Aceleración en g. */
    float gx, gy, gz;     /**< Velocidad angular en grados/s. */
    uint32_t t_us;        /**< Instante en que se pidió la lectura (us). */
} mpu6050_datos_t;

/**
 * @brief Inicia el bus I2C, reinicia el chip y lo configura.
 * @return false si el chip no responde o WHO_AM_I no es de un MPU-6050/6500.
 *
 * Bloquea unos 110 ms. Solo se llama durante la inicialización.
 */
bool mpu6050_iniciar(void);

/**
 * @brief Hace que el chip pida sus propias lecturas: cada vez que tiene una
 *        muestra nueva sube el pin INT y la ISR de ese pin llama a
 *        mpu6050_pedir_lectura().
 * @return false si no se pudo escribir en el chip.
 *
 * Bloquea (una escritura por I2C). Solo durante la inicialización.
 */
bool mpu6050_muestrear_con_int(void);

/**
 * @brief Empieza a leer aceleración y giroscopio. No espera el resultado.
 *
 * Si la lectura anterior todavía no terminó, no hace nada y la cuenta como
 * fallo. Contexto: ciclo principal o ISR.
 */
void mpu6050_pedir_lectura(void);

/** @return true si hay una lectura que el programa no ha recogido. */
bool mpu6050_hay_lectura(void);

/**
 * @brief Recoge la última lectura, ya en g y grados/s.
 * @param[out] d  la lectura.
 * @return false si no había ninguna nueva (d no se modifica).
 */
bool mpu6050_tomar_lectura(mpu6050_datos_t *d);

/** @return lecturas fallidas seguidas (vuelve a 0 con la primera que sale bien). */
uint32_t mpu6050_fallos(void);

/** @return lecturas perdidas desde el arranque: llego una nueva sin que se tomara la anterior. */
uint32_t mpu6050_perdidas(void);

/**
 * @brief Libera un bus I2C colgado: hasta 9 pulsos de SCL para que el chip
 *        suelte SDA, un STOP, y vuelve a iniciar el bus. No reconfigura el chip.
 *
 * Tarda unos 150 us. Contexto: ciclo principal.
 */
void mpu6050_recuperar_bus(void);

#endif /* MPU6050_H */
