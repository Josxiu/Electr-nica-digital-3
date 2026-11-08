// Puente Wi-Fi del CleanBot: Pico (UART) <-> PC (UDP).
//
// La Pico H no tiene Wi-Fi, así que la ESP32-S3 hace de "cable inalámbrico":
//   - Lo que la Pico escribe por su UART (líneas de texto: D,..., P,..., avisos)
//     se reenvía al PC en paquetes UDP.
//   - Lo que el PC manda por UDP (las teclas) se escribe tal cual en el UART de la Pico.
// La ESP no interpreta nada: así el protocolo es el mismo que por el cable USB y,
// cuando haya una Pico W, el PC le hablará igual (mismo puerto, mismo texto).
//
// Conexiones (las dos placas son de 3,3 V, no hace falta conversor de nivel):
//   Pico GP4 (UART1 TX, pin 6)  ->  ESP GPIO17 (RX)
//   Pico GP5 (UART1 RX, pin 7)  <-  ESP GPIO18 (TX)
//   Pico GND (pin 8)            --  ESP GND
// (En la ESP32-S3 el UART se puede sacar por cualquier pin; si se cruzan los cables,
//  basta con intercambiar PIN_RX_PICO y PIN_TX_PICO aquí abajo.)
//
// Red: la ESP se anuncia como cleanbot.local y escucha UDP en el puerto 8600.
// El PC le manda paquetes y la ESP le contesta a la IP y puerto del último que habló.
//
// LED de la placa: rojo = sin Wi-Fi, azul = Wi-Fi pero sin PC, verde = PC conectado.
//
// Placa: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash Size: 16MB.
// La primera vez copia secrets.h.example como secrets.h y pon el nombre y la clave
// de la red de 2,4 GHz (secrets.h no se sube a git).

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

#include "secrets.h"
#include "conexion_wifi.h"
#include "led_rgb.h"

// ---------- Lo que más probablemente vas a querer cambiar ----------

const char* MI_NOMBRE = "cleanbot";        // en la red: cleanbot.local
const uint16_t PUERTO_UDP = 8600;
const int PIN_RX_PICO = 17;                // llega del GP4 (TX) de la Pico, pin 6
const int PIN_TX_PICO = 18;                // va al GP5 (RX) de la Pico, pin 7
const uint32_t BAUDIOS_PICO = 115200;

const unsigned long PC_VIGENTE_MS = 3000;  // si el PC no habla en 3 s, se deja de enviarle
const unsigned long LOTE_MS = 20;          // se juntan líneas hasta 20 ms antes de enviar
const size_t LOTE_MAX = 1200;              // bytes por paquete (menos que los 1500 de Wi-Fi)

// -------------------------------------------------------------------

WiFiUDP udp;
LedRgb led(48, 30);
bool udpListo = false;

IPAddress ipPC;               // a quién se le mandan los datos: el último que habló
uint16_t puertoPC = 0;
unsigned long ultimoPC = 0;

char lote[LOTE_MAX];          // líneas completas que esperan salir en un paquete
size_t nLote = 0;
unsigned long inicioLote = 0;

char linea[256];              // línea que va llegando de la Pico
size_t nLinea = 0;
bool lineaLarga = false;      // se pasó de 256 bytes: se descarta hasta el próximo '\n'

// Contadores para el reporte por USB
uint32_t paquetesDelPC = 0, bytesAlaPico = 0, paquetesAlPC = 0, lineasDeLaPico = 0;

bool pcActivo() {
  return puertoPC != 0 && millis() - ultimoPC < PC_VIGENTE_MS;
}

void enviarLote() {
  if (nLote == 0) return;
  if (pcActivo() && wifiConectado()) {
    udp.beginPacket(ipPC, puertoPC);
    udp.write((const uint8_t*)lote, nLote);
    udp.endPacket();
    paquetesAlPC++;
  }
  nLote = 0;
}

void agregarLinea(const char* texto, size_t n) {
  lineasDeLaPico++;
  // Sin PC, lo que no son datos (mensajes de la Pico) se muestra por USB para depurar
  if (!pcActivo() && !(n > 2 && (texto[0] == 'D' || texto[0] == 'P') && texto[1] == ',')) {
    Serial.print("Pico: ");
    Serial.write((const uint8_t*)texto, n);
  }
  if (nLote + n > LOTE_MAX) enviarLote();
  if (nLote == 0) inicioLote = millis();
  memcpy(lote + nLote, texto, n);
  nLote += n;
}

void setup() {
  Serial.begin(115200);
  // Sin Monitor Serie abierto, los print no deben frenar el puente
  Serial.setTxTimeoutMs(0);
  delay(500);

  // Búfer grande: si el Wi-Fi se demora un momento, las líneas de la Pico esperan aquí
  Serial1.setRxBufferSize(4096);
  Serial1.begin(BAUDIOS_PICO, SERIAL_8N1, PIN_RX_PICO, PIN_TX_PICO);

  led.poner(255, 0, 0);
  wifiIniciar(WIFI_SSID, WIFI_PASSWORD, MI_NOMBRE);
  // Sin ahorro de energía: con el modo de ahorro el ESP "duerme" entre avisos del router
  // y los paquetes llegan con cientos de ms de retraso (la Pico pararía por el latido).
  WiFi.setSleep(false);
}

void loop() {
  if (wifiRevisar()) {
    udp.begin(PUERTO_UDP);
    udpListo = true;
    MDNS.addService("cleanbot", "udp", PUERTO_UDP);
    Serial.printf("Puente listo: el PC me habla a %s.local (o %s), UDP %u\n",
                  MI_NOMBRE, WiFi.localIP().toString().c_str(), PUERTO_UDP);
  }

  // 1) PC -> Pico: cada paquete se escribe tal cual en el UART
  if (udpListo) {
    int n = udp.parsePacket();
    while (n > 0) {
      ipPC = udp.remoteIP();
      puertoPC = udp.remotePort();
      ultimoPC = millis();
      paquetesDelPC++;
      uint8_t buf[256];
      int leidos = udp.read(buf, sizeof(buf));
      if (leidos > 0) {
        Serial1.write(buf, leidos);
        bytesAlaPico += leidos;
      }
      n = udp.parsePacket();
    }
  }

  // 2) Pico -> PC: se arman líneas completas y se juntan en paquetes
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      if (!lineaLarga && nLinea < sizeof(linea)) {
        linea[nLinea++] = '\n';
        agregarLinea(linea, nLinea);
      }
      nLinea = 0;
      lineaLarga = false;
    } else if (nLinea < sizeof(linea) - 1) {
      linea[nLinea++] = c;
    } else {
      lineaLarga = true;
    }
  }
  if (nLote > 0 && millis() - inicioLote >= LOTE_MS) enviarLote();

  // 3) LED de estado y reporte por USB
  static unsigned long ultimoLed = 0, ultimoReporte = 0;
  if (millis() - ultimoLed > 200) {
    ultimoLed = millis();
    if (!wifiConectado()) led.poner(255, 0, 0);
    else if (!pcActivo()) led.poner(0, 0, 255);
    else led.poner(0, 255, 0);
  }
  if (millis() - ultimoReporte > 5000) {
    ultimoReporte = millis();
    Serial.printf("[puente] PC %s | paquetes del PC %lu (%lu bytes a la Pico) | "
                  "lineas de la Pico %lu | paquetes al PC %lu\n",
                  pcActivo() ? ipPC.toString().c_str() : "sin conectar",
                  paquetesDelPC, bytesAlaPico, lineasDeLaPico, paquetesAlPC);
  }
  delay(1);
}
