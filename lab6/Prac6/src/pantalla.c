/**
 * @file pantalla.c
 * @brief Implementación no bloqueante del driver LCD HD44780. Ver pantalla.h.
 */
#include "pantalla.h"
#include "config.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

/* Comandos estándar HD44780 */
#define CMD_CLEAR_DISPLAY   0x01u
#define CMD_RETURN_HOME     0x02u
#define CMD_ENTRY_MODE      0x06u   /* Incrementar cursor, sin desplazamiento */
#define CMD_DISPLAY_ON      0x0Cu   /* Display ON, cursor OFF, parpadeo OFF */
#define CMD_FUNCTION_SET    0x28u   /* Bus 4 bits, 2 líneas, fuente 5x8 */
#define CMD_SET_DDRAM       0x80u

/* Tiempos mínimos según hoja de datos (en microsegundos) */
#define T_PULSO_E_US        2u
#define T_CMD_NORMAL_US     45u
#define T_CMD_LARGO_US      1650u   /* Clear Display y Return Home tardan ~1.52 ms */

/* Cola circular de operaciones (64 entradas alcanzan para refrescar toda la LCD) */
#define COLA_CAPACIDAD      64u

typedef struct {
    uint8_t byte;
    bool es_dato;           /* true = carácter (RS=1), false = comando (RS=0) */
} item_lcd_t;

typedef enum {
    ESTADO_INACTIVO = 0,
    ESTADO_NIBBLE_ALTO_E1,
    ESTADO_NIBBLE_ALTO_E0,
    ESTADO_NIBBLE_BAJO_E1,
    ESTADO_NIBBLE_BAJO_E0
} estado_fsm_t;

static item_lcd_t cola[COLA_CAPACIDAD];
static uint8_t cola_cabeza = 0;
static uint8_t cola_cola = 0;
static uint8_t cola_cantidad = 0;

static estado_fsm_t estado = ESTADO_INACTIVO;
static item_lcd_t actual;
static uint32_t t_inicio_us = 0;
static uint32_t t_espera_us = 0;

static void poner_nibble(uint8_t nibble, bool rs) {
    gpio_put(LCD_PIN_RS, rs);
    gpio_put(LCD_PIN_D4, (nibble >> 0) & 0x01u);
    gpio_put(LCD_PIN_D5, (nibble >> 1) & 0x01u);
    gpio_put(LCD_PIN_D6, (nibble >> 2) & 0x01u);
    gpio_put(LCD_PIN_D7, (nibble >> 3) & 0x01u);
}

/* Envío bloqueante usado únicamente durante pantalla_iniciar() */
static void enviar_nibble_bloqueante(uint8_t nibble, bool rs) {
    poner_nibble(nibble, rs);
    gpio_put(LCD_PIN_E, 1);
    sleep_us(T_PULSO_E_US);
    gpio_put(LCD_PIN_E, 0);
    sleep_us(T_CMD_NORMAL_US);
}

static void enviar_byte_bloqueante(uint8_t byte, bool rs) {
    enviar_nibble_bloqueante(byte >> 4, rs);
    enviar_nibble_bloqueante(byte & 0x0Fu, rs);
}

static bool encolar(uint8_t byte, bool es_dato) {
    if (cola_cantidad >= COLA_CAPACIDAD) {
        return false;
    }
    cola[cola_cabeza].byte = byte;
    cola[cola_cabeza].es_dato = es_dato;
    cola_cabeza = (uint8_t)((cola_cabeza + 1u) % COLA_CAPACIDAD);
    cola_cantidad++;
    return true;
}

static bool desencolar(item_lcd_t *out) {
    if (cola_cantidad == 0) {
        return false;
    }
    *out = cola[cola_cola];
    cola_cola = (uint8_t)((cola_cola + 1u) % COLA_CAPACIDAD);
    cola_cantidad--;
    return true;
}

void pantalla_iniciar(void) {
    const uint8_t pines[] = {
        LCD_PIN_RS, LCD_PIN_E,
        LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7
    };
    for (size_t i = 0; i < sizeof(pines); i++) {
        gpio_init(pines[i]);
        gpio_set_dir(pines[i], GPIO_OUT);
        gpio_put(pines[i], 0);
    }

    /* Secuencia oficial de reset por software del HD44780 */
    sleep_ms(50);                       /* Espera estabilización de Vcc */
    enviar_nibble_bloqueante(0x03, false);
    sleep_ms(5);
    enviar_nibble_bloqueante(0x03, false);
    sleep_us(150);
    enviar_nibble_bloqueante(0x03, false);
    enviar_nibble_bloqueante(0x02, false); /* Cambia a modo 4 bits */

    /* Configuración inicial */
    enviar_byte_bloqueante(CMD_FUNCTION_SET, false);
    enviar_byte_bloqueante(CMD_DISPLAY_ON, false);
    enviar_byte_bloqueante(CMD_CLEAR_DISPLAY, false);
    sleep_us(T_CMD_LARGO_US);
    enviar_byte_bloqueante(CMD_ENTRY_MODE, false);

    cola_cabeza = 0;
    cola_cola = 0;
    cola_cantidad = 0;
    estado = ESTADO_INACTIVO;
}

void pantalla_tarea(void) {
    const uint32_t ahora = time_us_32();

    switch (estado) {
        case ESTADO_INACTIVO:
            if (!desencolar(&actual)) {
                return;
            }
            /* Inicia nibble alto con E = 1 */
            poner_nibble(actual.byte >> 4, actual.es_dato);
            gpio_put(LCD_PIN_E, 1);
            t_inicio_us = ahora;
            t_espera_us = T_PULSO_E_US;
            estado = ESTADO_NIBBLE_ALTO_E1;
            break;

        case ESTADO_NIBBLE_ALTO_E1:
            if ((ahora - t_inicio_us) >= t_espera_us) {
                gpio_put(LCD_PIN_E, 0);
                t_inicio_us = ahora;
                t_espera_us = T_PULSO_E_US;
                estado = ESTADO_NIBBLE_ALTO_E0;
            }
            break;

        case ESTADO_NIBBLE_ALTO_E0:
            if ((ahora - t_inicio_us) >= t_espera_us) {
                /* Inicia nibble bajo con E = 1 */
                poner_nibble(actual.byte & 0x0Fu, actual.es_dato);
                gpio_put(LCD_PIN_E, 1);
                t_inicio_us = ahora;
                t_espera_us = T_PULSO_E_US;
                estado = ESTADO_NIBBLE_BAJO_E1;
            }
            break;

        case ESTADO_NIBBLE_BAJO_E1:
            if ((ahora - t_inicio_us) >= t_espera_us) {
                gpio_put(LCD_PIN_E, 0);
                t_inicio_us = ahora;
                /* Si es Clear o Home necesita > 1.52 ms; el resto ~45 us */
                if (!actual.es_dato &&
                    (actual.byte == CMD_CLEAR_DISPLAY || actual.byte == CMD_RETURN_HOME)) {
                    t_espera_us = T_CMD_LARGO_US;
                } else {
                    t_espera_us = T_CMD_NORMAL_US;
                }
                estado = ESTADO_NIBBLE_BAJO_E0;
            }
            break;

        case ESTADO_NIBBLE_BAJO_E0:
            if ((ahora - t_inicio_us) >= t_espera_us) {
                estado = ESTADO_INACTIVO;
            }
            break;
    }
}

bool pantalla_limpiar(void) {
    return encolar(CMD_CLEAR_DISPLAY, false);
}

bool pantalla_cursor(uint8_t col, uint8_t fila) {
    if (col >= LCD_COLUMNAS || fila >= LCD_FILAS) {
        return false;
    }
    static const uint8_t offset_fila[2] = { 0x00u, 0x40u };
    return encolar((uint8_t)(CMD_SET_DDRAM | (col + offset_fila[fila])), false);
}

bool pantalla_escribir_char(char c) {
    return encolar((uint8_t)c, true);
}

bool pantalla_escribir(const char *texto) {
    if (!texto) {
        return false;
    }
    const size_t len = strlen(texto);
    if ((COLA_CAPACIDAD - cola_cantidad) < len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        encolar((uint8_t)texto[i], true);
    }
    return true;
}

bool pantalla_imprimir_fila(uint8_t fila, const char *texto) {
    if (fila >= LCD_FILAS || !texto) {
        return false;
    }
    /* Necesitamos 1 comando de cursor + 16 caracteres = 17 espacios en cola */
    if ((COLA_CAPACIDAD - cola_cantidad) < (LCD_COLUMNAS + 1u)) {
        return false;
    }
    pantalla_cursor(0, fila);
    size_t i = 0;
    for (; i < LCD_COLUMNAS && texto[i] != '\0'; i++) {
        encolar((uint8_t)texto[i], true);
    }
    for (; i < LCD_COLUMNAS; i++) {
        encolar((uint8_t)' ', true);
    }
    return true;
}

bool pantalla_ocupada(void) {
    return (estado != ESTADO_INACTIVO) || (cola_cantidad > 0);
}