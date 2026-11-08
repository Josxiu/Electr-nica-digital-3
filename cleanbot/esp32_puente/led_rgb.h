// LED RGB direccionable (WS2812 / "NeoPixel"), como el que traen integrado
// las ESP32-S3 DevKit.
//
// Reutilizable:
//
//   LedRgb led(48, 40);        // pin y brillo máximo (0..255)
//   led.poner(255, 0, 0);      // rojo, escalado al brillo
//
// Usa rgbLedWrite(), que viene con el núcleo ESP32 de Arduino: no hace falta
// instalar la librería Adafruit NeoPixel para un solo LED.
#pragma once

#include <Arduino.h>

class LedRgb {
public:
  // brillo: tope de 0 a 255. A 255 un WS2812 encandila; 40 se ve bien.
  LedRgb(int pin, uint8_t brillo = 40) : pin_(pin), brillo_(brillo) {}

  // Los colores se dan "a tope" (0..255) y aquí se escalan al brillo, para
  // no tener que retocar cada color si cambias el brillo.
  void poner(uint8_t r, uint8_t g, uint8_t b) {
    // rgbLedWrite ya sabe hablar el protocolo del WS2812, incluido que ese
    // LED espera los bytes en orden G, R, B.
    rgbLedWrite(pin_, r * brillo_ / 255, g * brillo_ / 255, b * brillo_ / 255);
  }

  void apagar() { rgbLedWrite(pin_, 0, 0, 0); }

private:
  int pin_;
  uint8_t brillo_;
};
