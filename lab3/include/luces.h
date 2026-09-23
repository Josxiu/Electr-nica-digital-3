/**
 * @file luces.h
 * @brief Los cinco LEDs del juego: los cuatro de la secuencia y el de tiempo.
 *
 * Encapsula tres cosas: el encendido y apagado de los LEDs, el eco luminoso
 * que confirma una pulsacion, y el palpito del LED de tiempo.
 *
 * El eco y el palpito no bloquean: el modulo recuerda cuando empezaron y el
 * programa principal los deja avanzar llamando a luces_actualizar() y
 * luces_palpito() en cada vuelta.
 *
 * Los cinco LEDs son activos en alto y se actualizan como grupo: cualquier
 * cambio que afecte a mas de uno se resuelve con una sola escritura
 * enmascarada, de modo que un destello colectivo empieza y termina en el
 * mismo instante para todos.
 */
#ifndef LUCES_H
#define LUCES_H

#include <stdbool.h>
#include <stdint.h>

/** Cuantos LEDs administra el modulo: 4 de secuencia + el de tiempo. */
#define LUZ_N 5

/** Mascara logica de los cuatro LEDs de la secuencia. */
#define M_LEDS 0x0Fu

/** Mascara logica del LED 5 (indicador de tiempo). */
#define M_TIEMPO 0x10u

/** Mascara logica de los cinco LEDs. */
#define M_TODO 0x1Fu

/** Milisegundos que dura el eco luminoso de una pulsacion. */
#define LUZ_T_ECO 150u

/**
 * @brief Estado de los cinco LEDs. Campos privados por convencion.
 */
typedef struct {
    uint32_t bit[LUZ_N];    /**< mascara de GPIO de cada LED (indice 4 = tiempo) */
    uint32_t todos;         /**< mascara de los cinco */
    int      eco_activo;    /**< LED encendido como eco (-1 = ninguno) */
    uint32_t t_eco;         /**< ms en que arranco el eco */
    bool     palpito_on;    /**< estado actual del palpito */
    uint32_t t_palpito;     /**< ms del ultimo cambio del palpito */
} luces_t;

/**
 * @brief Configura los cinco GPIO como salida y los deja apagados.
 * @param l           estructura a inicializar.
 * @param pines_led   GPIO de los LEDs 1 a 4, en el mismo orden que los pulsadores.
 * @param pin_tiempo  GPIO del LED 5.
 */
void luces_init(luces_t *l, const uint8_t pines_led[4], uint8_t pin_tiempo);

/**
 * @brief Enciende el LED indicado.
 * @param l luces.
 * @param i indice 0 a 3; valores fuera de rango se ignoran.
 */
void luces_encender(luces_t *l, int i);

/**
 * @brief Apaga el LED indicado.
 * @param l luces.
 * @param i indice 0 a 3; valores fuera de rango se ignoran.
 */
void luces_apagar(luces_t *l, int i);

/**
 * @brief Apaga los cinco LEDs y cancela cualquier eco pendiente.
 * @param l luces.
 */
void luces_apagar_todo(luces_t *l);

/**
 * @brief Enciende o apaga el LED 5 (el de tiempo).
 * @param l         luces.
 * @param encendido true para encenderlo.
 */
void luces_tiempo(luces_t *l, bool encendido);

/**
 * @brief Enciende o apaga un conjunto de LEDs a la vez.
 * @param l         luces.
 * @param m         bits 0 a 3 = LEDs 1 a 4; bit 4 (0x10) = LED de tiempo.
 * @param encendido true para encender el conjunto.
 *
 * Lo usan las senales de realimentacion, que hacen parpadear grupos de LEDs.
 * Los LEDs del conjunto conmutan en una sola escritura; los que no estan en
 * la mascara no se tocan.
 */
void luces_mascara(luces_t *l, uint8_t m, bool encendido);

/**
 * @brief Enciende brevemente un LED como confirmacion de una pulsacion.
 * @param l     luces.
 * @param i     indice 0 a 3 del LED.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 *
 * Dura LUZ_T_ECO milisegundos sin importar cuanto se sostenga el pulsador,
 * que es lo que pide el enunciado: una pulsacion equivale a una sola entrada.
 */
void luces_eco(luces_t *l, int i, uint32_t ahora);

/**
 * @brief Apaga el eco cuando se cumple su tiempo. Llamar en cada vuelta.
 * @param l     luces.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 */
void luces_actualizar(luces_t *l, uint32_t ahora);

/**
 * @brief Reinicia el palpito para que arranque apagado.
 * @param l     luces.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 */
void luces_palpito_reiniciar(luces_t *l, uint32_t ahora);

/**
 * @brief Hace palpitar el LED de tiempo con el periodo indicado.
 * @param l          luces.
 * @param periodo_ms duracion de un ciclo completo; mas corto = mas rapido.
 * @param ahora      marca de tiempo de esta vuelta, en ms.
 *
 * El periodo lo calcula la logica del juego, porque depende del tiempo que
 * queda. Aqui solo se ejecuta el parpadeo.
 */
void luces_palpito(luces_t *l, uint32_t periodo_ms, uint32_t ahora);

#endif /* LUCES_H */
