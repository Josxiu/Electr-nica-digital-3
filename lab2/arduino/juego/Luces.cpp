/**
 * @file Luces.cpp
 * @brief Implementacion del control de los cinco LEDs.
 */
#include "Luces.h"

Luces::Luces(const int pinesLed[4], int pinLedTiempo) {
  for (int i = 0; i < 4; i++) _led[i] = pinesLed[i];
  _ledTiempo = pinLedTiempo;
  _ecoActivo = -1;
  _tEco = 0;
  _palpitoOn = false;
  _tPalpito = 0;
}

void Luces::iniciar() {
  for (int i = 0; i < 4; i++) {
    pinMode(_led[i], OUTPUT);
    digitalWrite(_led[i], LOW);
  }
  pinMode(_ledTiempo, OUTPUT);
  digitalWrite(_ledTiempo, LOW);
}

void Luces::encender(int i) {
  if (i < 0 || i > 3) return;
  digitalWrite(_led[i], HIGH);
}

void Luces::apagar(int i) {
  if (i < 0 || i > 3) return;
  digitalWrite(_led[i], LOW);
}

void Luces::apagarTodo() {
  for (int i = 0; i < 4; i++) digitalWrite(_led[i], LOW);
  digitalWrite(_ledTiempo, LOW);
  _ecoActivo = -1;
}

void Luces::tiempo(bool encendido) {
  digitalWrite(_ledTiempo, encendido ? HIGH : LOW);
}

void Luces::eco(int i) {
  if (i < 0 || i > 3) return;
  if (_ecoActivo >= 0) digitalWrite(_led[_ecoActivo], LOW);
  _ecoActivo = i;
  _tEco = millis();
  digitalWrite(_led[i], HIGH);
}

void Luces::actualizar() {
  if (_ecoActivo >= 0 && (millis() - _tEco) >= T_ECO) {
    digitalWrite(_led[_ecoActivo], LOW);
    _ecoActivo = -1;
  }
}

void Luces::palpitoReiniciar() {
  _palpitoOn = false;
  _tPalpito = millis();
  digitalWrite(_ledTiempo, LOW);
}

void Luces::palpito(int periodoMs) {
  unsigned long ahora = millis();
  // Medio periodo encendido, medio apagado.
  if (ahora - _tPalpito < (unsigned long)(periodoMs / 2)) return;
  _tPalpito = ahora;
  _palpitoOn = !_palpitoOn;
  digitalWrite(_ledTiempo, _palpitoOn ? HIGH : LOW);
}
