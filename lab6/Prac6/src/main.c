/**
 * @file prueba_mpu6050.c
 * @brief Prueba del driver de la IMU en la placa.
 *
 * El chip avisa cada muestra por el pin INT y el driver la lee por
 * interrupciones. Una vez por segundo imprime cuántas lecturas llegaron
 * (deben ser MPU_FRECUENCIA_HZ), los fallos y la última lectura.
 *
 * Con la placa quieta y plana se espera az cerca de 1 g, ax y ay cerca de 0
 * y el giroscopio cerca de 0 (unas décimas de sesgo son normales).
 * Si no llega ninguna lectura, casi siempre es el cable del pin INT.
 */
#include <stdio.h>
#include "pico/stdlib.h"

#include "config.h"
#include "mpu6050.h"

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                 /* tiempo para abrir el monitor serie */

    printf("Prueba de la IMU: SDA GP%u, SCL GP%u, INT GP%u\n",
           PIN_I2C_SDA, PIN_I2C_SCL, MPU_PIN_INT);
    if (!mpu6050_iniciar() || !mpu6050_muestrear_con_int()) {
        while (true) {
            printf("ERROR: la IMU no responde. Revisa SDA, SCL, VCC y GND.\n");
            sleep_ms(1000);
        }
    }
    printf("lecturas/s  fallos |  ax[g]  ay[g]  az[g] | gx gy gz [grados/s]\n");

    mpu6050_datos_t d = { 0 };
    uint32_t lecturas = 0, fallos = 0;
    absolute_time_t informe = make_timeout_time_ms(1000);

    while (true) {
        if (mpu6050_tomar_lectura(&d)) {
            lecturas++;
        }
        if (mpu6050_fallos() >= 3) {
            fallos += mpu6050_fallos();
            mpu6050_recuperar_bus();
        }
        if (time_reached(informe)) {
            informe = delayed_by_ms(informe, 1000);
            printf("%10lu  %6lu | %6.3f %6.3f %6.3f | %7.2f %7.2f %7.2f\n",
                   (unsigned long)lecturas, (unsigned long)fallos,
                   d.ax, d.ay, d.az, d.gx, d.gy, d.gz);
            lecturas = 0;
        }
    }
}
