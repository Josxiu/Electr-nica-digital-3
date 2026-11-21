#include <stdio.h>
#include "pico/stdlib.h"
#include "config.h"
#include "pantalla.h"

int main(void) {
    stdio_init_all();
    pantalla_iniciar();

    pantalla_imprimir_fila(0, "Hola Prueba 1");
    pantalla_imprimir_fila(1, "LCD No Bloqueante");

    uint32_t contador = 0;
    char linea[17];
    absolute_time_t proximo_refresco = make_timeout_time_ms(250);

    while (true) {
        /* 1. Llamar siempre al polling de la pantalla (tarda < 1 us) */
        pantalla_tarea();

        /* 2. Aquí va tu lógica de la IMU sin interrupciones ni bloqueos */
        // mpu6050_tomar_lectura(&d);

        /* 3. Actualizar texto solo cada 250 ms y si la pantalla está libre */
        if (time_reached(proximo_refresco) && !pantalla_ocupada()) {
            proximo_refresco = delayed_by_ms(proximo_refresco, 250);
            snprintf(linea, sizeof(linea), "Ciclos: %lu", (unsigned long)contador++);
            pantalla_imprimir_fila(1, linea);
        }
    }
}