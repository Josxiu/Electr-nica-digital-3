/**
 * @file botones.h
 * @brief Botones MODO, MAS y MENOS con antirrebote por interrupciones.
 *
 * Cada boton va del pin a GND con el pull-up interno (sin pulsar = 1).
 *
 * Flujo:
 *   - ISR del GPIO (flanco de bajada o de subida): apaga la interrupcion de
 *     ese pin para ignorar los rebotes y programa una alarma de
 *     BOTON_ANTIRREBOTE_MS.
 *   - Alarma: lee el nivel ya estable. Al presionar guarda el instante; al
 *     soltar deja un evento corto o largo (BOTON_LARGO_MS) y vuelve a
 *     activar la interrupcion del pin.
 *   - main: consulta los eventos con botones_tomar_evento().
 *
 * Basado en el antirrebote de la Practica 4 (estado estable y pulsacion que
 * se consume una vez), pasado de polling a interrupciones para que el main
 * pueda dormir.
 */
#ifndef BOTONES_H
#define BOTONES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOTON_MODO = 0,     /**< Cambia de pantalla; sostenido inicia una sesion. */
    BOTON_MAS,          /**< Sube el valor que se esta configurando. */
    BOTON_MENOS,        /**< Baja el valor que se esta configurando. */
    BOTONES_N
} boton_id_t;

typedef struct {
    boton_id_t boton;   /**< Boton que se solto. */
    bool largo;         /**< Se sostuvo al menos BOTON_LARGO_MS. */
} boton_evento_t;

/**
 * @brief Configura los pines y sus interrupciones.
 *
 * Comparte la interrupcion del banco de GPIO con el pin INT de la IMU (cada
 * uno registra su manejador para sus pines).
 */
void botones_iniciar(void);

/** @return true si hay algun evento sin tomar. */
bool botones_hay_evento(void);

/**
 * @brief Toma el evento pendiente de un boton, si hay.
 * @param ev donde se copia el evento.
 * @return true si habia un evento.
 */
bool botones_tomar_evento(boton_evento_t *ev);

#endif /* BOTONES_H */
