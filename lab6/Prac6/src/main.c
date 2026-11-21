/**
 * @file main.c
 * @brief CalCamino: contador de pasos con la IMU.
 *
 * Flujo polling + interrupciones:
 *   - ISR del pin INT (200 Hz): el chip tiene una muestra, se pide la lectura.
 *   - ISR del I2C: la transferencia termino, guarda los bytes.
 *   - main: recoge la lectura, la pasa al contador de pasos, suma las calorias
 *     de los pasos nuevos y duerme cuando no queda nada por atender.
 *
 * El main no escribe por el serial: para ver los pasos y ajustar el detector
 * se usa pruebas/prueba_pasos.c. stdio_init_all() solo deja el USB listo para
 * cargar programas.
 */
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "config.h"
#include "mpu6050.h"
#include "pasos.h"
#include "calorias.h"

static pasos_t pasos;
static calorias_t calorias;

/** Inicializacion: aqui si se puede esperar. Sin IMU se reintenta cada segundo. */
static void iniciar(void) {
    stdio_init_all();

    while (!mpu6050_iniciar()) {
        sleep_ms(1000);
    }
    while (!mpu6050_muestrear_con_int()) {
        sleep_ms(1000);
    }
    pasos_iniciar(&pasos);
    calorias_iniciar(&calorias, USUARIO_PESO_KG, USUARIO_PASO_M);
}

/** Pasa la muestra al contador de pasos y suma la energia de los pasos nuevos. */
static void procesar_muestra(const mpu6050_datos_t *d) {
    const uint32_t nuevos = pasos_agregar(&pasos, d);
    if (nuevos > 0) {
        calorias_sumar(&calorias, nuevos, pasos.duracion_s);
    }
}

/** Con 3 lecturas fallidas seguidas el bus quedo colgado: se libera. */
static void vigilar_bus(void) {
    if (mpu6050_fallos() >= 3) {
        mpu6050_recuperar_bus();
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
    iniciar();

    while (true) {
        mpu6050_datos_t d;
        if (mpu6050_tomar_lectura(&d)) {
            procesar_muestra(&d);
        }
        vigilar_bus();
        dormir();
    }
}
