/**
 * @file Entradas.h
 * @brief Pulsadores con antirrebote.
 *
 * Un contacto mecanico rebota unos milisegundos al cerrarse, asi que una sola
 * pulsacion se leeria como varias. Esta clase solo acepta un cambio de estado
 * si la lectura se mantuvo estable durante T_ANTIRREBOTE.
 *
 * Distingue dos cosas que no son lo mismo:
 *   - el INSTANTE de la pulsacion (el "flanco"), que es lo que cuenta como
 *     entrada del jugador y se consume una sola vez;
 *   - el ESTADO sostenido, que sirve para detectar la pulsacion larga de
 *     reinicio.
 *
 * Se asume que el pulsador va del GPIO a GND y se usa el pull-up interno:
 * sin pulsar = HIGH, pulsado = LOW.
 */
#ifndef ENTRADAS_H
#define ENTRADAS_H

#include <Arduino.h>

class Boton {
public:
  /** @brief Configura el pin como entrada con pull-up y deja el estado limpio. */
  void iniciar(int pin);

  /**
   * @brief Lee el pin y valida el cambio si paso el tiempo de antirrebote.
   * @param ahora marca de tiempo comun de esta vuelta del loop.
   *
   * Hay que llamarlo en CADA vuelta, en todos los estados del juego.
   */
  void leer(unsigned long ahora);

  /**
   * @brief Consulta si hubo una pulsacion nueva y la consume.
   * @return true una sola vez por pulsacion, sin importar cuanto se sostenga.
   *
   * Ojo: consume el flanco. Si dos partes del programa lo llaman, solo la
   * primera se entera de la pulsacion.
   */
  bool presionado();

  /** @brief true si el pulsador esta presionado en este momento. */
  bool estaPresionado() const;

  /**
   * @brief true si lleva al menos 'ms' milisegundos presionado sin soltarse.
   * @param ahora marca de tiempo de esta vuelta.
   * @param ms    duracion exigida, por ejemplo 2000 para el reinicio.
   */
  bool sostenido(unsigned long ahora, unsigned long ms) const;

  /** @brief Olvida un flanco pendiente, para que no cuente como entrada. */
  void descartar();

private:
  static const unsigned long T_ANTIRREBOTE = 25;   ///< ms de estabilidad exigidos

  int  _pin;
  bool _lectura;            ///< ultima lectura cruda del pin
  bool _estable;            ///< estado ya validado (true = presionado)
  bool _flanco;             ///< true en el instante de la pulsacion
  unsigned long _tCambio;   ///< instante del ultimo cambio en la lectura cruda
};

#endif
