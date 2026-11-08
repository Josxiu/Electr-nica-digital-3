#include "conexion_wifi.h"

#include <WiFi.h>
#include <ESPmDNS.h>

// "static" en un .cpp = solo visible en este archivo. Así estas variables
// no chocan con otras del mismo nombre en el resto del programa.
static const char* ssidActual = nullptr;
static const char* nombreMdnsActual = nullptr;
static bool estabaConectado = false;

// Cada vez que el router nos tumba, el chip dispara un evento con el
// "reason code" de 802.11. Lo guardamos para imprimirlo en el reporte.
// Los que más salen: 15 o 204 = falló el saludo de 4 pasos (casi siempre la
// contraseña), 201 = no aparece la red, 202 = autenticación rechazada.
static volatile uint8_t ultimoMotivo = 0;

static void alDesconectar(WiFiEvent_t evento, WiFiEventInfo_t info) {
  uint8_t motivo = info.wifi_sta_disconnected.reason;
  // Tras cada intento fallido el ESP manda un segundo aviso, el 36
  // (STA_LEAVING: "me retiro"), que es solo limpieza. Si lo guardáramos
  // taparía el motivo verdadero, que llega justo antes.
  if (motivo != WIFI_REASON_STA_LEAVING) ultimoMotivo = motivo;
}

static const char* describirEstado(wl_status_t s) {
  switch (s) {
    case WL_NO_SSID_AVAIL:   return "no encuentro esa red (¿nombre mal escrito o es la de 5 GHz?)";
    case WL_CONNECT_FAILED:  return "la red rechazó la conexión (¿contraseña?)";
    case WL_DISCONNECTED:    return "desconectado, intentando...";
    case WL_IDLE_STATUS:     return "intentando...";
    case WL_CONNECTION_LOST: return "se perdió la conexión";
    default:                 return "estado desconocido";
  }
}

bool wifiIniciar(const char* ssid, const char* clave, const char* nombreMdns) {
  if (String(ssid).startsWith("nombre_de_tu_red")) {
    Serial.println("secrets.h todavía tiene los datos de ejemplo: sigo sin WiFi.");
    return false;
  }
  ssidActual = ssid;
  nombreMdnsActual = nombreMdns;

  Serial.printf("Conectando a \"%s\"...\n", ssid);
  WiFi.onEvent(alDesconectar, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);   // estación = cliente de un router (no punto de acceso)
  // begin() no espera: el intento sigue en segundo plano y, si el router
  // se cae, el ESP se reconecta solo.
  WiFi.begin(ssid, clave);
  return true;
}

bool wifiConectado() {
  return WiFi.status() == WL_CONNECTED;
}

bool wifiRevisar() {
  if (ssidActual == nullptr) return false;   // nunca se intentó conectar

  // Solo informamos cuando el estado cambia, para que el Monitor Serie no
  // se llene de lo mismo.
  bool conectado = wifiConectado();
  bool recienConectado = conectado && !estabaConectado;

  if (recienConectado) {
    Serial.printf("WiFi conectado. Mi IP es %s (señal %d dBm)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    // mDNS hay que arrancarlo con red: sirve para que nos encuentren por
    // nombre y también para que nosotros preguntemos por el de otros.
    if (nombreMdnsActual != nullptr && MDNS.begin(nombreMdnsActual)) {
      Serial.printf("Mi nombre en la red: %s.local\n", nombreMdnsActual);
    }
  } else if (!conectado && estabaConectado) {
    Serial.println("Se perdió el WiFi; reintentando...");
  }
  estabaConectado = conectado;

  // Mientras no conecte, cada 5 s decimos por qué. No es lo mismo
  // "no veo la red" que "clave mala".
  static unsigned long ultimoReporte = 0;
  if (!conectado && millis() - ultimoReporte > 5000) {
    ultimoReporte = millis();
    if (ultimoMotivo == WIFI_REASON_NO_AP_FOUND) {
      Serial.printf("WiFi sin conectar: no aparece la red '%s' (motivo 201). "
                    "Revisa el nombre exacto, espacios incluidos, con prueba_led.\n", ssidActual);
    } else {
      Serial.printf("WiFi sin conectar: %s (motivo 802.11: %u)\n",
                    describirEstado(WiFi.status()), ultimoMotivo);
    }
  }
  return recienConectado;
}
