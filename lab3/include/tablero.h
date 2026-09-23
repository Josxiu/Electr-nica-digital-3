/**
 * @file tablero.h
 * @brief Cuatro digitos de 7 segmentos multiplexados por software.
 *
 * Los cuatro digitos comparten fisicamente las mismas siete lineas de
 * segmento, asi que solo puede haber uno encendido a la vez. El modulo rota
 * entre ellos lo bastante rapido para que el ojo los perciba encendidos
 * simultaneamente (persistencia retiniana).
 *
 * Hardware: display de catodo comun de 12 pines, una resistencia de 220 ohm
 * por segmento y el comun de cada digito conmutado desde un GPIO.
 *
 * Reparto de los digitos:
 *   Digito 1 = nivel (1-9)
 *   Digito 2 = vidas (0-3)
 *   Digitos 3 y 4 = tiempo acumulado en segundos (00-99)
 *
 * Diferencia con la version Arduino: alli cada refresco hacia nueve
 * escrituras individuales (apagar el comun, siete segmentos, encender el
 * comun nuevo). Aqui los once GPIO se actualizan con una sola operacion
 * enmascarada, de modo que segmentos y comun conmutan en el mismo ciclo.
 */
#ifndef TABLERO_H
#define TABLERO_H

#include <stdint.h>

/** Numero de segmentos por digito. */
#define TAB_NUM_SEG 7

/** Numero de digitos del display. */
#define TAB_NUM_DIG 4

/**
 * Polaridad del display. Son las UNICAS dos lineas que hay que cambiar si se
 * reemplaza el display por uno de otro tipo:
 *
 *   Catodo comun, conexion directa (el de Juan):  SEG 1, COM 0
 *   Anodo comun, conexion directa (el de Camilo): SEG 0, COM 1
 *   Anodo comun con transistor PNP:               SEG 0, COM 0
 *
 * La tabla de digitos NO cambia: dice cuales segmentos van encendidos, no con
 * que nivel logico se encienden. Internamente la polaridad se aplica como un
 * XOR sobre la palabra de salida completa, asi que no cuesta tiempo adicional.
 */
#define TAB_SEG_ACTIVO_ALTO 1
#define TAB_COM_ACTIVO_ALTO 0

/**
 * @brief Estado del display multiplexado.
 *
 * Los campos son privados por convencion: fuera de tablero.c no deben
 * leerse ni escribirse.
 */
typedef struct {
    uint32_t seg_mask;               /**< bits de los siete segmentos */
    uint32_t com_mask;               /**< bits de los cuatro comunes */
    uint32_t invertir;               /**< bits de los pines activos en bajo */
    uint32_t seg_word[10];           /**< palabra de GPIO de cada digito 0-9 */
    uint32_t com_word[TAB_NUM_DIG];  /**< palabra de GPIO de cada comun */
    uint32_t buffer[TAB_NUM_DIG];    /**< palabra que le toca a cada digito */
    uint8_t  activo;                 /**< digito encendido en este momento */
    uint32_t t_refresco;             /**< ms del ultimo paso del barrido */
} tablero_t;

/**
 * @brief Configura los once GPIO como salida y deja el display en blanco.
 * @param t              estructura a inicializar (no puede ser NULL).
 * @param pines_segmento GPIO de los segmentos a, b, c, d, e, f, g en ese orden.
 * @param pines_comun    GPIO de los comunes de los digitos 1 a 4.
 *
 * Precalcula la tabla de palabras de GPIO de los diez digitos, de forma que
 * el refresco solo tenga que combinar dos valores ya resueltos.
 */
void tablero_init(tablero_t *t,
                  const uint8_t pines_segmento[TAB_NUM_SEG],
                  const uint8_t pines_comun[TAB_NUM_DIG]);

/**
 * @brief Define QUE se muestra.
 * @param t      display.
 * @param nivel  valor del digito 1; se recorta a 0-9.
 * @param vidas  valor del digito 2; se recorta a 0-9.
 * @param tiempo segundos acumulados; se recorta a 0-99.
 *
 * No toca el hardware: solo llena el buffer interno, asi que es instantaneo
 * y se puede llamar cuando sea.
 */
void tablero_mostrar(tablero_t *t, int nivel, int vidas, int tiempo);

/**
 * @brief Avanza el barrido un digito, si ya paso su turno.
 * @param t     display.
 * @param ahora marca de tiempo comun de esta vuelta del bucle, en ms.
 *
 * DEBE llamarse continuamente, en cada vuelta del bucle principal. No es una
 * funcion que "escribe el numero": es la que sostiene la ilusion. Si pasan
 * mas de unos 8 ms entre llamadas, el parpadeo se vuelve visible.
 */
void tablero_refrescar(tablero_t *t, uint32_t ahora);

/**
 * @brief Apaga los cuatro digitos sin borrar el contenido del buffer.
 * @param t display.
 *
 * Se usa en las pruebas de componente; el juego no la necesita.
 */
void tablero_apagar(tablero_t *t);

#endif /* TABLERO_H */
