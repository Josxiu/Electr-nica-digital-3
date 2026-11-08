// Conexión WiFi como estación (cliente de un router), con nombre mDNS propio.
//
// Reutilizable: copia conexion_wifi.h y conexion_wifi.cpp a la carpeta de
// otro sketch. Uso mínimo:
//
//   void setup() { wifiIniciar(WIFI_SSID, WIFI_PASSWORD, "mi-aparato"); }
//   void loop()  { if (wifiRevisar()) { /* recién conectado */ } ... }
//
// Lo que aporta frente a llamar WiFi.begin() a secas: avisa por Serial
// cuándo conecta, cuándo se cae y, mientras no conecte, POR QUÉ (el código
// de motivo de 802.11), que es lo que de verdad ayuda a diagnosticar.
#pragma once

#include <Arduino.h>

// Arranca el intento de conexión en segundo plano; no espera a que conecte.
// nombreMdns: el aparato quedará como <nombreMdns>.local en la red (o nullptr
// si no se quiere). Si el SSID todavía es el de secrets.h.example, no intenta.
//
// Devuelve true si arrancó la red. OJO: cualquier cosa que use TCP/IP (un
// WebServer, por ejemplo) hay que iniciarla DESPUÉS de esto: la pila de red
// (lwIP) la crea WiFi.mode(), y sin ella el chip aborta y se reinicia en bucle.
bool wifiIniciar(const char* ssid, const char* clave, const char* nombreMdns);

// Llamarla en cada vuelta del loop. Devuelve true solo en la vuelta en que
// se acaba de conectar: es el momento de arrancar lo que necesite red
// (por ejemplo, anunciar servicios por mDNS).
bool wifiRevisar();

bool wifiConectado();
