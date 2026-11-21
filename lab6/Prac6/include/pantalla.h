/**
 * @file pantalla.h
 * @brief Driver no bloqueante (por polling) para pantalla LCD 16x2 (HD44780)
 *        en interfaz paralela de 4 bits.
 *
 * La secuencia de encendido e inicialización a 4 bits se hace una vez al
 * arrancar de forma bloqueante (~50 ms). A partir de ahí, todas las escrituras
 * se encolan en un buffer circular y se envían paso a paso llamando a
 * pantalla_tarea() desde el ciclo principal, sin detener el procesador.
 */
#ifndef PANTALLA_H
#define PANTALLA_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configura los pines GPIO e inicializa el controlador HD44780 en
 *        modo de 4 bits, 2 líneas, cursor apagado.
 *
 * Bloquea unos 50 ms. Solo se llama durante la inicialización.
 */
void pantalla_iniciar(void);

/**
 * @brief Avanza la máquina de estados de envío hacia la LCD.
 *
 * Debe llamarse frecuentemente dentro del bucle principal (while(true)).
 * No bloquea: si aún no se cumple el tiempo de espera del controlador,
 * retorna inmediatamente.
 */
void pantalla_tarea(void);

/**
 * @brief Encola el comando para borrar toda la pantalla y volver al inicio.
 * @return false si la cola interna está llena.
 */
bool pantalla_limpiar(void);

/**
 * @brief Encola el posicionamiento del cursor.
 * @param col  Columna (0 a 15).
 * @param fila Fila (0 o 1).
 * @return false si la cola interna está llena o coordenadas inválidas.
 */
bool pantalla_cursor(uint8_t col, uint8_t fila);

/**
 * @brief Encola un carácter para mostrar en la posición actual del cursor.
 * @return false si la cola interna está llena.
 */
bool pantalla_escribir_char(char c);

/**
 * @brief Encola una cadena de texto terminada en '\0'.
 * @return false si no hay espacio suficiente en la cola para toda la cadena.
 */
bool pantalla_escribir(const char *texto);

/**
 * @brief Función de conveniencia: posiciona el cursor al inicio de una fila
 *        y escribe el texto rellenando con espacios hasta el final de la línea
 *        (evita caracteres residuales sin necesidad de usar pantalla_limpiar()).
 * @param fila  Fila (0 o 1).
 * @param texto Cadena a mostrar (se trunca a 16 caracteres).
 * @return false si no hay espacio suficiente en la cola.
 */
bool pantalla_imprimir_fila(uint8_t fila, const char *texto);

/** @return true si hay datos pendientes por enviar en la cola o en curso. */
bool pantalla_ocupada(void);

#endif /* PANTALLA_H */