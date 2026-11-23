/**
 * @file main.c
 * @brief CalCamino: contador de pasos con la IMU y pantalla OLED.
 *
 * Flujo polling + interrupciones:
 *   - ISR del pin INT (200 Hz): el chip tiene una muestra, se pide la lectura.
 *   - ISR del I2C de la IMU: la transferencia termino, guarda los bytes.
 *   - Timer cada PANTALLA_PERIODO_MS: marca que toca refrescar la pantalla.
 *   - ISR del I2C de la pantalla: envia el cuadro y avisa cuando termina.
 *   - main: recoge la lectura, la pasa al contador de pasos, suma las calorias
 *     de los pasos nuevos, dibuja la pantalla cuando toca y duerme cuando no
 *     queda nada por atender.
 *
 * El main no escribe por el serial: para ver los pasos y ajustar el detector
 * se usa pruebas/prueba_pasos.c. stdio_init_all() solo deja el USB listo para
 * cargar programas.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "config.h"
#include "mpu6050.h"
#include "pasos.h"
#include "calorias.h"
#include "pantalla.h"

static pasos_t pasos;
static calorias_t calorias;

static repeating_timer_t timer_pantalla;
static volatile bool toca_pantalla = false;

/** Callback del timer (corre en interrupcion): solo marca la bandera. */
static bool marcar_pantalla(repeating_timer_t *t) {
    (void)t;
    toca_pantalla = true;
    return true;
}

/** Inicializacion: aqui si se puede esperar. Sin IMU o sin pantalla se reintenta cada segundo. */
static void iniciar(void) {
    stdio_init_all();

    while (!pantalla_iniciar(PANTALLA_I2C, PANTALLA_PIN_SDA, PANTALLA_PIN_SCL, PANTALLA_I2C_HZ)) {
        sleep_ms(1000);
    }
    irq_set_priority(I2C0_IRQ + i2c_get_index(PANTALLA_I2C), PRIORIDAD_IRQ_PANTALLA);

    while (!mpu6050_iniciar()) {
        sleep_ms(1000);
    }
    while (!mpu6050_muestrear_con_int()) {
        sleep_ms(1000);
    }
    pasos_iniciar(&pasos);
    calorias_iniciar(&calorias, USUARIO_PESO_KG, USUARIO_PASO_M);

    add_repeating_timer_ms(-(int32_t)PANTALLA_PERIODO_MS, marcar_pantalla, NULL, &timer_pantalla);
}

/** Pasa la muestra al contador de pasos y suma la energia de los pasos nuevos. */
static void procesar_muestra(const mpu6050_datos_t *d) {
    const uint32_t nuevos = pasos_agregar(&pasos, d);
    if (nuevos > 0) {
        calorias_sumar(&calorias, nuevos, pasos.duracion_s);
    }
}

/** Dibuja pasos, calorias y bateria en el bufer y empieza a enviarlo. */
static void actualizar_pantalla(void) {
    const uint8_t bateria_pct = 100;    /* fijo hasta tener la lectura de la bateria */
    char texto[20];

    pantalla_limpiar(0);
    pantalla_texto("Pasos", 0, 0, 1);
    pantalla_dibujar_bateria(70, 0, bateria_pct);
    snprintf(texto, sizeof(texto), "%3u%%", (unsigned)bateria_pct);
    pantalla_texto(texto, 96, 1, 1);

    snprintf(texto, sizeof(texto), "%5lu", (unsigned long)pasos.total);
    pantalla_texto(texto, 4, 14, 3);
    pantalla_hline(0, 41, PANTALLA_ANCHO, 1);

    snprintf(texto, sizeof(texto), "%.1f kcal", calorias.kcal);
    pantalla_texto(texto, 0, 46, 1);
    snprintf(texto, sizeof(texto), "%.0fkg", calorias.peso_kg);
    pantalla_texto(texto, 96, 56, 1);

    pantalla_mostrar();
}

/** Con 3 lecturas fallidas seguidas el bus quedo colgado: se libera. */
static void vigilar_bus(void) {
    if (mpu6050_fallos() >= 3) {
        mpu6050_recuperar_bus();
    }
}

/** true si hay algo que el ciclo principal pueda atender ya. */
static bool hay_trabajo(void) {
    return mpu6050_hay_lectura() || (toca_pantalla && !pantalla_ocupada());
}

/**
 * Duerme hasta la proxima interrupcion si no hay nada por atender. La consulta
 * y el wfi van con las interrupciones enmascaradas: si un evento llega justo
 * entre los dos, queda pendiente y el wfi retorna enseguida.
 */
static void dormir(void) {
    const uint32_t estado = save_and_disable_interrupts();
    if (!hay_trabajo()) {
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
        if (toca_pantalla && !pantalla_ocupada()) {
            toca_pantalla = false;
            actualizar_pantalla();
        }
        vigilar_bus();
        dormir();
    }
}
