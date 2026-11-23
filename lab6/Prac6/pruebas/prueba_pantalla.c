/**
 * @file main.c
 * @brief Aplicación de demostración del podómetro CalCamino usando el driver pantalla.
 * @details Inicializa la pantalla OLED por polling y actualiza periódicamente los pasos,
 *          las calorías y el nivel de batería mediante envíos no bloqueantes por interrupciones.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "config.h"
#include "pantalla.h"

/**
 * @brief Punto de entrada principal de la aplicación.
 *
 * @return int Código de salida estándar (no retorna en operación normal).
 */
int main(void) {
    stdio_init_all();

    // Inicialización síncrona por polling
    if (!pantalla_iniciar(PANTALLA_I2C, PANTALLA_PIN_SDA, PANTALLA_PIN_SCL, PANTALLA_I2C_HZ)) {
        printf("No se encontro la pantalla: revisa SDA, SCL, VCC y GND\n");
        while (true) {
            tight_loop_contents();
        }
    }

    // Secuencia de prueba y bienvenida
    pantalla_limpiar(1);
    pantalla_mostrar();
    pantalla_esperar_lista();
    sleep_ms(500);

    pantalla_limpiar(0);
    pantalla_texto("Hola CalCamino", 8, 28, 1);
    pantalla_mostrar();
    pantalla_esperar_lista();
    sleep_ms(2000);

    uint32_t pasos = 0;
    uint8_t bateria_pct = 100;
    float calorias = 0.0f;
    absolute_time_t prox_refresco = get_absolute_time();

    while (true) {
        if (time_reached(prox_refresco) && !pantalla_ocupada()) {
            calorias = pasos * 0.035f;

            char texto[20];
            pantalla_limpiar(0);
            pantalla_texto("Pasos", 0, 0, 1);
            pantalla_dibujar_bateria(100, 0, bateria_pct);
            snprintf(texto, sizeof(texto), "%5lu", (unsigned long)pasos);
            pantalla_texto(texto, 4, 14, 3);
            pantalla_hline(0, 42, PANTALLA_ANCHO, 1);
            snprintf(texto, sizeof(texto), "%.1f kcal", calorias);
            pantalla_texto(texto, 0, 48, 1);

            if (pantalla_mostrar()) {
                pasos += 7;
                if ((pasos % 70 == 0) && (bateria_pct > 0)) {
                    bateria_pct--;
                }
                prox_refresco = make_timeout_time_ms(300);
            }
        }
    }

    return 0;
}