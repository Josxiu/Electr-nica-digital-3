/**
 * @file prueba_pasos.c
 * @brief Prueba del contador de pasos por el monitor serie.
 *
 * Es una herramienta para ajustar y comprobar el detector, no parte del
 * programa final: printf puede esperar si el PC no lee el puerto, y el programa
 * final no usa el serial.
 *
 * El flujo es el mismo del programa final (interrupciones del INT y del I2C,
 * lectura en el ciclo principal, dormir al terminar).
 *
 * Teclas por el monitor serie:
 *   r  empieza una sesion nueva (pasos y kcal a cero)
 *   g  activa o desactiva el envio de datos crudos (CSV) para ajustar el umbral
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "config.h"
#include "mpu6050.h"
#include "pasos.h"
#include "calorias.h"
#include "estado.h"

static pasos_t pasos;
static calorias_t calorias;
static estado_disp_t dispositivo;
static bool enviar_crudos = false;

/** Empieza una sesion: pasos y calorias en cero. */
static void iniciar_sesion(void) {
    pasos_iniciar(&pasos);
    calorias_iniciar(&calorias, (float)USUARIO_PESO_KG,
                     calorias_paso_por_estatura((float)USUARIO_ESTATURA_CM / 100.0f));
    estado_iniciar(&dispositivo, time_us_32());
}

static void atender_muestra(const mpu6050_datos_t *d) {
    const uint32_t ultimo_paso_us = dispositivo.t_ultimo_paso_us;
    const uint32_t nuevos = pasos_agregar(&pasos, d);

    if (nuevos > 0) {
        calorias_sumar(&calorias, nuevos, pasos.duracion_s);
        printf("Pasos: %lu (+%lu)  %.3f kcal  perdidas: %lu\n", (unsigned long)pasos.total,
               (unsigned long)nuevos, calorias.kcal, (unsigned long)mpu6050_perdidas());
    }
    if (estado_actualizar(&dispositivo, d->t_us, nuevos)) {
        if (dispositivo.actual == ESTADO_REPOSO) {
            printf("# Reposo: %.3f s despues del ultimo paso\n",
                   (d->t_us - ultimo_paso_us) * 1e-6f);
        } else {
            printf("# Activo: marcha confirmada\n");
        }
    }
    if (enviar_crudos) {
        printf("%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%lu\n", (unsigned long)(d->t_us / 1000),
               d->ax, d->ay, d->az, pasos.mag, pasos.filtrada, (unsigned long)pasos.total);
    }
}

static void atender_teclas(void) {
    const int tecla = getchar_timeout_us(0);
    if (tecla == 'r') {
        iniciar_sesion();
        printf("Sesion nueva: pasos y kcal en cero\n");
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
    iniciar_sesion();
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
