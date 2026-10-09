/**
 * @file entradas.c
 * @brief Implementacion del antirrebote de los pulsadores.
 */
#include "entradas.h"
#include "hardware/gpio.h"

void boton_init(boton_t *b, uint8_t pin)
{
    b->bit = 1u << pin;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);          /* equivale a pinMode(pin, INPUT_PULLUP) */

    b->lectura  = false;
    b->estable  = false;
    b->flanco   = false;
    b->t_cambio = 0;
}

uint32_t entradas_muestrear(void)
{
    /* Lectura conjunta: los cinco pulsadores quedan capturados en el mismo
       instante, en lugar de en cinco lecturas separadas. */
    return gpio_get_all();
}

void boton_leer(boton_t *b, uint32_t entradas, uint32_t ahora)
{
    bool lect = ((entradas & b->bit) == 0u);   /* pull-up: nivel bajo = presionado */

    if (lect != b->lectura) {          /* cambio la lectura cruda: reinicia la ventana */
        b->lectura  = lect;
        b->t_cambio = ahora;
    }

    if ((ahora - b->t_cambio) >= ENT_T_ANTIRREBOTE && lect != b->estable) {
        b->estable = lect;
        if (b->estable) {
            b->flanco = true;          /* flanco de bajada: se acaba de presionar */
        }
    }
}

bool boton_presionado(boton_t *b)
{
    if (!b->flanco) {
        return false;
    }
    b->flanco = false;
    return true;
}

bool boton_esta_presionado(const boton_t *b)
{
    return b->estable;
}

bool boton_sostenido(const boton_t *b, uint32_t ahora, uint32_t ms)
{
    return b->estable && (ahora - b->t_cambio) >= ms;
}

void boton_descartar(boton_t *b)
{
    b->flanco = false;
}
