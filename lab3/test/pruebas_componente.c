/**
 * @file pruebas_componente.c
 * @brief Banco de pruebas de componente (paso 3.2.5 de la guia).
 *
 * Programa independiente del juego. Verifica por separado cada modulo de
 * acceso al hardware y deja constancia por el monitor serie, de modo que las
 * incidencias de montaje no se confundan con errores de la logica del juego.
 *
 * Se compila como un segundo ejecutable (pruebas_componente.uf2). Para
 * usarlo se carga en el Pico igual que el juego; volviendo a cargar
 * pulso_memoria.uf2 se recupera el juego.
 *
 * Secuencia de pruebas, en bucle:
 *   P1  LEDs uno por uno (verifica cableado y orden de PIN_LED).
 *   P2  LED de tiempo.
 *   P3  Destello colectivo de los cinco LEDs con una sola escritura.
 *   P4  Cada digito del display por separado, mostrando 0..9.
 *   P5  Los cuatro digitos a la vez con el barrido (prueba de multiplexado).
 *   P6  Pulsadores: informa por serie cada flanco y cada pulsacion sostenida.
 *   P7  Base de tiempo: mide la desviacion de un intervalo de 1000 ms.
 */

#include <stdio.h>
#include "pico/stdlib.h"

#include "pines.h"
#include "tiempo.h"
#include "tablero.h"
#include "luces.h"
#include "entradas.h"

static tablero_t tablero;
static luces_t   luces;
static boton_t   btn[4];
static boton_t   btn_inicio;

/**
 * @brief Espera activa que mantiene el barrido del display.
 * @param ms milisegundos a esperar.
 *
 * No se usa sleep_ms() porque detendria la multiplexacion y el display
 * parpadearia durante la prueba.
 */
static void esperar_refrescando(uint32_t ms)
{
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        tablero_refrescar(&tablero, millis());
    }
}

int main(void)
{
    stdio_init_all();

    tablero_init(&tablero, PIN_SEGMENTO, PIN_COMUN);
    luces_init(&luces, PIN_LED, PIN_LED_TIEMPO);
    for (int i = 0; i < 4; i++) {
        boton_init(&btn[i], PIN_BOTON[i]);
    }
    boton_init(&btn_inicio, PIN_BOTON_INICIO);

    printf("\n=== Pruebas de componente - Pulso de memoria (C) ===\n");

    /* --- P7: base de tiempo (se mide primero, antes de cargar el sistema) --- */
    uint32_t t0 = millis();
    esperar_refrescando(1000);
    printf("P7 base de tiempo: intervalo nominal 1000 ms, medido %u ms\n",
           (unsigned)(millis() - t0));

    while (true) {

        /* --- P1: LEDs de secuencia uno por uno --- */
        printf("P1 LEDs 1..4 uno por uno\n");
        for (int i = 0; i < 4; i++) {
            luces_apagar_todo(&luces);
            luces_encender(&luces, i);
            esperar_refrescando(400);
        }
        luces_apagar_todo(&luces);

        /* --- P2: LED de tiempo --- */
        printf("P2 LED de tiempo\n");
        luces_tiempo(&luces, true);
        esperar_refrescando(600);
        luces_tiempo(&luces, false);

        /* --- P3: destello colectivo con una sola escritura --- */
        printf("P3 los cinco LEDs a la vez (escritura enmascarada)\n");
        for (int k = 0; k < 4; k++) {
            luces_mascara(&luces, M_TODO, true);
            esperar_refrescando(200);
            luces_mascara(&luces, M_TODO, false);
            esperar_refrescando(200);
        }

        /* --- P4: cada digito por separado --- */
        printf("P4 digitos por separado, 0 a 9\n");
        for (int d = 0; d <= 9; d++) {
            /* Se pone el mismo valor en los cuatro para comprobar que los
               siete segmentos responden en todas las posiciones. */
            tablero_mostrar(&tablero, d, d, d * 11);
            esperar_refrescando(400);
        }

        /* --- P5: multiplexado con cuatro valores distintos --- */
        printf("P5 multiplexado: debe leerse 9 3 47 estable y sin fantasmas\n");
        tablero_mostrar(&tablero, 9, 3, 47);
        esperar_refrescando(3000);

        /* --- P6: pulsadores durante 8 s --- */
        printf("P6 pulsadores: pulsa cada uno; INICIO 2 s para la prueba larga\n");
        uint32_t t_ini = millis();
        bool aviso_largo = false;
        while (millis() - t_ini < 8000u) {
            uint32_t ahora = millis();
            tablero_refrescar(&tablero, ahora);

            uint32_t entradas = entradas_muestrear();
            for (int i = 0; i < 4; i++) {
                boton_leer(&btn[i], entradas, ahora);
            }
            boton_leer(&btn_inicio, entradas, ahora);

            for (int i = 0; i < 4; i++) {
                if (boton_presionado(&btn[i])) {
                    printf("   flanco en pulsador %d\n", i + 1);
                    luces_eco(&luces, i, ahora);
                }
            }
            if (boton_presionado(&btn_inicio)) {
                printf("   flanco en INICIO\n");
            }
            if (!aviso_largo && boton_sostenido(&btn_inicio, ahora, 2000u)) {
                aviso_largo = true;
                printf("   INICIO sostenido 2 s detectado\n");
            }
            if (!boton_esta_presionado(&btn_inicio)) {
                aviso_largo = false;
            }
            luces_actualizar(&luces, ahora);
        }

        printf("--- ciclo de pruebas completo, repitiendo ---\n\n");
    }
}
