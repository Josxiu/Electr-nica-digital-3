/**
 * @file luces.c
 * @brief Implementacion del control de los cinco LEDs.
 */
#include "luces.h"
#include "hardware/gpio.h"

void luces_init(luces_t *l, const uint8_t pines_led[4], uint8_t pin_tiempo)
{
    l->todos = 0;
    for (int i = 0; i < 4; i++) {
        l->bit[i] = 1u << pines_led[i];
        l->todos |= l->bit[i];
    }
    l->bit[4] = 1u << pin_tiempo;
    l->todos |= l->bit[4];

    l->eco_activo = -1;
    l->t_eco = 0;
    l->palpito_on = false;
    l->t_palpito = 0;

    gpio_init_mask(l->todos);
    gpio_set_dir_out_masked(l->todos);
    gpio_put_masked(l->todos, 0u);       /* los cinco apagados de una vez */
}

void luces_encender(luces_t *l, int i)
{
    if (i < 0 || i > 3) {
        return;
    }
    gpio_set_mask(l->bit[i]);
}

void luces_apagar(luces_t *l, int i)
{
    if (i < 0 || i > 3) {
        return;
    }
    gpio_clr_mask(l->bit[i]);
}

void luces_apagar_todo(luces_t *l)
{
    gpio_clr_mask(l->todos);             /* una escritura para los cinco */
    l->eco_activo = -1;
}

void luces_tiempo(luces_t *l, bool encendido)
{
    if (encendido) {
        gpio_set_mask(l->bit[4]);
    } else {
        gpio_clr_mask(l->bit[4]);
    }
}

void luces_mascara(luces_t *l, uint8_t m, bool encendido)
{
    /* ACTUALIZACION CONJUNTA DE GPIO (requisito 2.2 de la practica).
     *
     * La mascara logica que usa el juego (bits 0-4) se traduce a la mascara
     * fisica de GPIO y se escribe de un golpe. Los cuatro LEDs de una senal
     * de realimentacion encienden y apagan en el mismo instante, no en
     * cascada como ocurria con cuatro digitalWrite sucesivos. */
    uint32_t w = 0;
    for (int i = 0; i < LUZ_N; i++) {
        if (m & (1u << i)) {
            w |= l->bit[i];
        }
    }
    gpio_put_masked(w, encendido ? w : 0u);
}

void luces_eco(luces_t *l, int i, uint32_t ahora)
{
    if (i < 0 || i > 3) {
        return;
    }
    if (l->eco_activo >= 0) {
        gpio_clr_mask(l->bit[l->eco_activo]);
    }
    l->eco_activo = i;
    l->t_eco = ahora;
    gpio_set_mask(l->bit[i]);
}

void luces_actualizar(luces_t *l, uint32_t ahora)
{
    if (l->eco_activo >= 0 && (ahora - l->t_eco) >= LUZ_T_ECO) {
        gpio_clr_mask(l->bit[l->eco_activo]);
        l->eco_activo = -1;
    }
}

void luces_palpito_reiniciar(luces_t *l, uint32_t ahora)
{
    l->palpito_on = false;
    l->t_palpito = ahora;
    gpio_clr_mask(l->bit[4]);
}

void luces_palpito(luces_t *l, uint32_t periodo_ms, uint32_t ahora)
{
    /* Medio periodo encendido, medio apagado. */
    if (ahora - l->t_palpito < periodo_ms / 2u) {
        return;
    }
    l->t_palpito = ahora;
    l->palpito_on = !l->palpito_on;
    if (l->palpito_on) {
        gpio_set_mask(l->bit[4]);
    } else {
        gpio_clr_mask(l->bit[4]);
    }
}
