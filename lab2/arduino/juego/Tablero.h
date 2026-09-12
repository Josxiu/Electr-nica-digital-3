/**
 * @file Tablero.h
 * @brief Cuatro digitos de 7 segmentos multiplexados por software.
 *
 * Los cuatro digitos comparten fisicamente las mismas siete lineas de
 * segmento, asi que solo puede haber uno encendido a la vez. Esta clase rota
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
 */
#ifndef TABLERO_H
#define TABLERO_H

#include <Arduino.h>

/**
 * Polaridad del display. Son las UNICAS dos lineas que hay que cambiar si se
 * reemplaza el display por uno de otro tipo:
 *
 *   Catodo comun (el que usamos):    SEG_ON = HIGH, COM_ON = HIGH
 *   Anodo comun, conexion directa:   SEG_ON = LOW,  COM_ON = HIGH
 *   Anodo comun, con transistor PNP: SEG_ON = LOW,  COM_ON = LOW
 *
 * La tabla de digitos NO cambia: dice cuales segmentos van encendidos, no con
 * que nivel logico se encienden.
 */
const bool SEG_ON = HIGH;
const bool COM_ON = HIGH;

class Tablero {
public:
  /**
   * @param pinesSegmento GPIO de los segmentos a, b, c, d, e, f, g en ese orden.
   * @param pinesComun    GPIO de los comunes de los digitos 1 a 4.
   */
  Tablero(const int pinesSegmento[7], const int pinesComun[4]);

  /** @brief Configura los once pines como salida. Llamar desde setup(). */
  void iniciar();

  /**
   * @brief Define QUE se muestra. No toca el hardware: solo llena el buffer
   *        interno, asi que es instantaneo y se puede llamar cuando sea.
   */
  void mostrar(int nivel, int vidas, int tiempo);

  /**
   * @brief Avanza el barrido un digito, si ya paso su turno.
   *
   * DEBE llamarse continuamente, en cada vuelta del loop. No es una funcion
   * que "escribe el numero": es la que sostiene la ilusion. Si pasan mas de
   * unos 8 ms entre llamadas, el parpadeo se vuelve visible.
   */
  void refrescar();

private:
  static const unsigned long MS_POR_DIGITO = 2;   ///< 2 ms x 4 digitos = 125 Hz

  int  _seg[7];          ///< GPIO de cada segmento
  int  _com[4];          ///< GPIO del comun de cada digito
  byte _buffer[4];       ///< codigo de segmentos que le toca a cada digito
  int  _activo;          ///< digito encendido en este momento
  unsigned long _tRefresco;  ///< instante del ultimo paso del barrido
};

#endif
