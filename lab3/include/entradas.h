/**
 * @file entradas.h
 * @brief Pulsadores con antirrebote.
 *
 * Un contacto mecanico rebota unos milisegundos al cerrarse, asi que una sola
 * pulsacion se leeria como varias. Este modulo solo acepta un cambio de
 * estado si la lectura se mantuvo estable durante ENT_T_ANTIRREBOTE.
 *
 * Distingue dos cosas que no son lo mismo:
 *   - el INSTANTE de la pulsacion (el "flanco"), que es lo que cuenta como
 *     entrada del jugador y se consume una sola vez;
 *   - el ESTADO sostenido, que sirve para detectar la pulsacion larga de
 *     reinicio.
 *
 * Se asume que el pulsador va del GPIO a GND y se usa el pull-up interno:
 * sin pulsar = 1, pulsado = 0.
 *
 * Diferencia con la version Arduino: alli cada boton hacia su propio
 * digitalRead. Aqui el bucle principal toma UNA instantanea de los 30 GPIO
 * con gpio_get_all() y se la pasa a los cinco botones, de modo que todos
 * quedan muestreados en el mismo instante.
 */
#ifndef ENTRADAS_H
#define ENTRADAS_H

#include <stdbool.h>
#include <stdint.h>

/** Milisegundos de estabilidad exigidos para validar un cambio. */
#define ENT_T_ANTIRREBOTE 25u

/**
 * @brief Estado de un pulsador. Campos privados por convencion.
 */
typedef struct {
    uint32_t bit;        /**< mascara del GPIO dentro de la palabra de entradas */
    bool     lectura;    /**< ultima lectura cruda del pin */
    bool     estable;    /**< estado ya validado (true = presionado) */
    bool     flanco;     /**< true en el instante de la pulsacion */
    uint32_t t_cambio;   /**< ms del ultimo cambio en la lectura cruda */
} boton_t;

/**
 * @brief Configura el GPIO como entrada con pull-up y deja el estado limpio.
 * @param b   boton a inicializar.
 * @param pin numero de GPIO (0-29).
 */
void boton_init(boton_t *b, uint8_t pin);

/**
 * @brief Toma una instantanea de todas las entradas.
 * @return Palabra con el nivel de los 30 GPIO en el mismo instante.
 *
 * Llamar una sola vez por vuelta y pasar el resultado a todos los botones.
 */
uint32_t entradas_muestrear(void);

/**
 * @brief Valida el cambio de un boton si paso el tiempo de antirrebote.
 * @param b        boton.
 * @param entradas palabra devuelta por entradas_muestrear().
 * @param ahora    marca de tiempo comun de esta vuelta, en ms.
 *
 * Hay que llamarlo en CADA vuelta, en todos los estados del juego.
 */
void boton_leer(boton_t *b, uint32_t entradas, uint32_t ahora);

/**
 * @brief Consulta si hubo una pulsacion nueva y la consume.
 * @param b boton.
 * @return true una sola vez por pulsacion, sin importar cuanto se sostenga.
 *
 * Ojo: consume el flanco. Si dos partes del programa lo llaman, solo la
 * primera se entera de la pulsacion.
 */
bool boton_presionado(boton_t *b);

/**
 * @brief Indica si el pulsador esta presionado en este momento.
 * @param b boton.
 * @return true si el estado validado es "presionado".
 */
bool boton_esta_presionado(const boton_t *b);

/**
 * @brief Indica si lleva al menos 'ms' milisegundos presionado sin soltarse.
 * @param b     boton.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 * @param ms    duracion exigida, por ejemplo 2000 para el reinicio.
 * @return true si se cumple la condicion.
 */
bool boton_sostenido(const boton_t *b, uint32_t ahora, uint32_t ms);

/**
 * @brief Olvida un flanco pendiente, para que no cuente como entrada.
 * @param b boton.
 */
void boton_descartar(boton_t *b);

#endif /* ENTRADAS_H */
