/**
 * @file Entradas.cpp
 * @brief Implementacion del antirrebote de los pulsadores.
 */
#include "Entradas.h"

void Boton::iniciar(int pin) {
  _pin = pin;
  pinMode(pin, INPUT_PULLUP);
  _lectura = false;
  _estable = false;
  _flanco  = false;
  _tCambio = 0;
}

void Boton::leer(unsigned long ahora) {
  bool lect = (digitalRead(_pin) == LOW);   // pull-up: LOW = presionado

  if (lect != _lectura) {        // cambio la lectura cruda: reinicia la ventana
    _lectura = lect;
    _tCambio = ahora;
  }

  if ((ahora - _tCambio) >= T_ANTIRREBOTE && lect != _estable) {
    _estable = lect;
    if (_estable) _flanco = true;      // flanco de bajada: se acaba de presionar
  }
}

bool Boton::presionado() {
  if (!_flanco) return false;
  _flanco = false;
  return true;
}

bool Boton::estaPresionado() const {
  return _estable;
}

bool Boton::sostenido(unsigned long ahora, unsigned long ms) const {
  return _estable && (ahora - _tCambio) >= ms;
}

void Boton::descartar() {
  _flanco = false;
}
