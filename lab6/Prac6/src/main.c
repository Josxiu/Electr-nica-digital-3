/**
 * @file main.c
 * @brief Contador de pasos con la IMU, para probar por el monitor serie.
 *
 * Flujo polling + interrupciones:
 *   - ISR del pin INT (200 Hz): el chip tiene una muestra, se pide la lectura.
 *   - ISR del I2C: la transferencia termino, guarda los bytes.
 *   - main: recoge la lectura, la pasa al contador de pasos, imprime y atiende
 *     las teclas. Sin trabajo pendiente, duerme.
 *
 * Teclas por el monitor serie:
 *   r  empieza una sesion nueva (pasos a cero)
 *   g  activa o desactiva el envio de datos crudos (CSV) para ajustar el umbral
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "config.h"
#include "mpu6050.h"
#include "pasos.h"

static pasos_t pasos;
static bool enviar_crudos = false;

static void atender_muestra(const mpu6050_datos_t *d) {
    const uint32_t nuevos = pasos_agregar(&pasos, d);

    if (nuevos > 0) {
        printf("Pasos: %lu (+%lu)\n", (unsigned long)pasos.total, (unsigned long)nuevos);
    }
    if (enviar_crudos) {
        printf("%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%lu\n", (unsigned long)(d->t_us / 1000),
               d->ax, d->ay, d->az, pasos.mag, pasos.filtrada, (unsigned long)pasos.total);
    }
}

static void atender_teclas(void) {
    const int tecla = getchar_timeout_us(0);
    if (tecla == 'r') {
        pasos_iniciar(&pasos);
        printf("Sesion nueva: pasos = 0\n");
    } else if (tecla == 'g') {
        enviar_crudos = !enviar_crudos;
        if (enviar_crudos) {
            printf("t_ms,ax,ay,az,mag,filtrada,pasos\n");
        }
    }
}

/** Con 3 lecturas fallidas seguidas el bus quedo colgado: se libera. */
static void vigilar_bus(void) {
    if (mpu6050_fallos() >= 3) {
        mpu6050_recuperar_bus();
        printf("# I2C sin respuesta, bus reiniciado\n");
    }
}

/**
 * Duerme hasta la proxima interrupcion si no hay nada por atender. La consulta
 * y el wfi van con las interrupciones enmascaradas: si la lectura llega justo
 * entre los dos, queda pendiente y el wfi retorna enseguida.
 */
static void dormir(void) {
    const uint32_t estado = save_and_disable_interrupts();
    if (!mpu6050_hay_lectura()) {
        __wfi();
    }
    restore_interrupts(estado);
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                 /* tiempo para abrir el monitor serie */

    if (!mpu6050_iniciar() || !mpu6050_muestrear_con_int()) {
        while (true) {
            printf("ERROR: la IMU no responde. Revisa SDA, SCL, VCC, GND e INT.\n");
            sleep_ms(1000);
        }
    }
    pasos_iniciar(&pasos);
    printf("Contador de pasos listo. r = sesion nueva, g = datos crudos\n");

    while (true) {
        mpu6050_datos_t d;
        if (mpu6050_tomar_lectura(&d)) {
            atender_muestra(&d);
        }
        atender_teclas();
        vigilar_bus();
        dormir();
    }
}
