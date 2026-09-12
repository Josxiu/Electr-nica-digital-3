/**
 * @file Tablero.cpp
 * @brief Implementacion del multiplexado por software de los cuatro digitos.
 */
#include "Tablero.h"
// #include <hardware/gpio.h>   // solo si se usa gpio_set_drive_strength()

/**
 * Codificacion de los digitos 0 a 9 en siete segmentos.
 * Bit 0 = a, bit 1 = b, bit 2 = c, bit 3 = d, bit 4 = e, bit 5 = f, bit 6 = g.
 */
static const byte DIGITO[10] = {
  0b0111111,  // 0 -> a b c d e f
  0b0000110,  // 1 -> b c
  0b1011011,  // 2 -> a b d e g
  0b1001111,  // 3 -> a b c d g
  0b1100110,  // 4 -> b c f g
  0b1101101,  // 5 -> a c d f g
  0b1111101,  // 6 -> a c d e f g
  0b0000111,  // 7 -> a b c
  0b1111111,  // 8 -> todos
  0b1101111   // 9 -> a b c d f g
};

Tablero::Tablero(const int pinesSegmento[7], const int pinesComun[4]) {
  for (int i = 0; i < 7; i++) _seg[i] = pinesSegmento[i];
  for (int i = 0; i < 4; i++) _com[i] = pinesComun[i];
  for (int i = 0; i < 4; i++) _buffer[i] = 0;
  _activo = 0;
  _tRefresco = 0;
}

void Tablero::iniciar() {
  for (int i = 0; i < 7; i++) {
    pinMode(_seg[i], OUTPUT);
    // Si los digitos quedan tenues, descomentar la linea siguiente (y el
    // include de arriba). Sube la fuerza de salida del pin de 4 mA a 12 mA:
    // no cambia los 3,3 V, reduce la caida de voltaje interna del GPIO.
    // gpio_set_drive_strength(_seg[i], GPIO_DRIVE_STRENGTH_12MA);
    digitalWrite(_seg[i], !SEG_ON);      // segmentos apagados
  }
  for (int i = 0; i < 4; i++) {
    pinMode(_com[i], OUTPUT);
    // gpio_set_drive_strength(_com[i], GPIO_DRIVE_STRENGTH_12MA);
    digitalWrite(_com[i], !COM_ON);      // ningun digito encendido
  }
}

void Tablero::mostrar(int nivel, int vidas, int tiempo) {
  // Se recortan los valores para no salirse de la tabla si llega algo raro.
  if (nivel  < 0) nivel  = 0;
  if (vidas  < 0) vidas  = 0;
  if (tiempo < 0) tiempo = 0;
  if (tiempo > 99) tiempo = 99;

  _buffer[0] = DIGITO[nivel % 10];
  _buffer[1] = DIGITO[vidas % 10];
  _buffer[2] = DIGITO[(tiempo / 10) % 10];   // decenas
  _buffer[3] = DIGITO[tiempo % 10];          // unidades
}

void Tablero::refrescar() {
  unsigned long ahora = millis();
  if (ahora - _tRefresco < MS_POR_DIGITO) return;
  _tRefresco = ahora;

  // 1. Apagar el digito actual ANTES de tocar los segmentos.
  //    Si no se hace, durante un instante el digito viejo muestra los
  //    segmentos del nuevo: es el efecto "fantasma" del multiplexado.
  digitalWrite(_com[_activo], !COM_ON);

  // 2. Pasar al siguiente digito y poner sus segmentos.
  _activo = (_activo + 1) % 4;
  byte codigo = _buffer[_activo];
  for (int s = 0; s < 7; s++) {
    bool encendido = (codigo >> s) & 1;
    digitalWrite(_seg[s], encendido ? SEG_ON : !SEG_ON);
  }

  // 3. Encender el digito nuevo.
  digitalWrite(_com[_activo], COM_ON);
}
