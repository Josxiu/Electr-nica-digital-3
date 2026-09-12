/**
 * @file Luces.h
 * @brief Los cinco LEDs del juego: los cuatro de la secuencia y el de tiempo.
 *
 * Encapsula tres cosas que antes estaban sueltas en el programa principal:
 * el encendido y apagado de los LEDs, el eco luminoso que confirma una
 * pulsacion, y el palpito del LED de tiempo.
 *
 * El eco y el palpito no bloquean: la clase recuerda cuando empezaron y el
 * programa principal la deja avanzar llamando a actualizar() en cada vuelta.
 */
#ifndef LUCES_H
#define LUCES_H

#include <Arduino.h>

class Luces {
public:
  /**
   * @param pinesLed     GPIO de los LEDs 1 a 4, en el mismo orden que los pulsadores.
   * @param pinLedTiempo GPIO del LED 5, el indicador de tiempo disponible.
   */
  Luces(const int pinesLed[4], int pinLedTiempo);

  /** @brief Configura los cinco pines como salida. Llamar desde setup(). */
  void iniciar();

  /** @brief Enciende el LED indicado (0 a 3). */
  void encender(int i);

  /** @brief Apaga el LED indicado (0 a 3). */
  void apagar(int i);

  /** @brief Apaga los cinco LEDs y cancela cualquier eco pendiente. */
  void apagarTodo();

  /** @brief Enciende o apaga el LED 5 (el de tiempo). */
  void tiempo(bool encendido);

  /**
   * @brief Enciende brevemente un LED como confirmacion de una pulsacion.
   *
   * Dura T_ECO milisegundos sin importar cuanto se sostenga el pulsador, que
   * es lo que pide el enunciado: una pulsacion equivale a una sola entrada.
   */
  void eco(int i);

  /** @brief Apaga el eco cuando se cumple su tiempo. Llamar en cada vuelta. */
  void actualizar();

  /** @brief Reinicia el palpito para que arranque apagado. */
  void palpitoReiniciar();

  /**
   * @brief Hace palpitar el LED de tiempo con el periodo indicado.
   * @param periodoMs duracion de un ciclo completo; mas corto = mas rapido.
   *
   * El periodo lo calcula la logica del juego, porque depende del tiempo que
   * queda. Aqui solo se ejecuta el parpadeo.
   */
  void palpito(int periodoMs);

private:
  static const unsigned long T_ECO = 150;   ///< ms que dura el eco luminoso

  int  _led[4];
  int  _ledTiempo;

  int  _ecoActivo;            ///< LED encendido como eco (-1 = ninguno)
  unsigned long _tEco;        ///< instante en que arranco el eco

  bool _palpitoOn;
  unsigned long _tPalpito;
};

#endif
