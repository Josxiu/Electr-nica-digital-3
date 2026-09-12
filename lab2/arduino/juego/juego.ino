/**
 * @file juego.ino
 * @brief Juego de memoria (Practica 2) - version Arduino sobre Raspberry Pi Pico.
 *
 * Este archivo contiene la LOGICA DEL JUEGO y la COORDINACION general.
 * El acceso a los dispositivos vive en modulos aparte:
 *
 *   Tablero.h / .cpp   -> los cuatro digitos de 7 segmentos multiplexados
 *   Luces.h   / .cpp   -> los cinco LEDs, el eco y el palpito
 *   Entradas.h / .cpp  -> los pulsadores con antirrebote
 *
 * Nada bloquea: no hay un solo delay(). Todo se resuelve comparando marcas de
 * tiempo en cada vuelta del loop. Eso es lo que permite atender los pulsadores
 * en cualquier instante y, sobre todo, sostener el barrido de los displays:
 * con multiplexacion por software, una espera de mas de ~8 ms se ve como
 * parpadeo.
 */

#include "Tablero.h"
#include "Luces.h"
#include "Entradas.h"

// =====================================================================
// 1. ASIGNACION DE PINES
// =====================================================================

const int PIN_LED[4]       = { 2, 3, 4, 5 };                 // LEDs 1 a 4
const int PIN_LED_TIEMPO   = 10;                             // LED 5
const int PIN_BOTON[4]     = { 6, 7, 8, 9 };                 // pulsadores 1 a 4
const int PIN_BOTON_INICIO = 11;                             // inicio / reinicio
const int PIN_SEGMENTO[7]  = { 12, 13, 14, 15, 16, 17, 18 }; // segmentos a..g
const int PIN_COMUN[4]     = { 19, 20, 21, 22 };             // comunes digitos 1 a 4

// =====================================================================
// 2. DISPOSITIVOS
// =====================================================================

Tablero tablero(PIN_SEGMENTO, PIN_COMUN);
Luces   luces(PIN_LED, PIN_LED_TIEMPO);
Boton   btn[4];        // pulsadores del jugador
Boton   btnInicio;     // pulsador de inicio / reinicio

// =====================================================================
// 3. PARAMETROS DEL JUEGO
// =====================================================================

const int   NIVEL_MAX          = 9;
const int   VIDAS_INICIALES    = 3;
const float FACTOR_RESPUESTA   = 1.25;   // Tmaximo = 1,25 x Tpresentacion
const float DUTY               = 0.7;    // fraccion del periodo con el LED encendido
const unsigned long T_RESET    = 2000;   // pulsacion sostenida que reinicia
const unsigned long T_PAUSA    = 1500;   // pausa entre intentos
const int   PALPITO_LENTO      = 400;    // periodo del LED 5 al empezar a responder
const int   PALPITO_RAPIDO     = 90;     // periodo del LED 5 al vencerse el tiempo

// =====================================================================
// 4. ESTADO DE LA PARTIDA
// =====================================================================

/** Estados de la maquina principal. */
enum Estado {
  ESPERA,        ///< esperando el pulsador de inicio
  PRESENTACION,  ///< mostrando la secuencia (pulsadores 1-4 desactivados)
  INGRESO,       ///< el jugador responde, con el LED 5 palpitando
  PAUSA,         ///< intervalo entre intentos
  FIN            ///< partida terminada; el tablero queda congelado
};

Estado estado = ESPERA;

int secuencia[NIVEL_MAX];      // elementos de la secuencia (indices de LED 0..3)
int nivel = 1;
int vidas = VIDAS_INICIALES;

// --- Presentacion ---
int  periodo = 1000;           // duracion de cada elemento, en ms
int  encendido = 700;          // parte del periodo con el LED prendido
int  paso = 0;                 // cual elemento se esta mostrando
bool ledEncendido = false;
unsigned long tPaso = 0;       // instante en que empezo el elemento o la pausa

// --- Ingreso ---
int indiceJugador = 0;                // cuantos elementos correctos lleva
unsigned long tMaximo = 0;            // ms permitidos para este intento
unsigned long tInicioIngreso = 0;     // instante en que empezo el turno
unsigned long tiempoAcumMs = 0;       // suma de TODOS los intentos

// --- Control de flujo ---
bool subirDespues = false;     // al terminar la pausa: ¿subir de nivel o repetir?
bool irAFin = false;           // la partida termino
bool resetYaHecho = false;     // evita reiniciar en bucle mientras se sostiene INICIO

// --- Modos de prueba (se cambian por el monitor serie) ---
bool modoTiempo = true;        // false = sin limite de tiempo
bool modoVidas  = true;        // false = vidas infinitas

// =====================================================================
// 5. LOGICA DEL JUEGO
// =====================================================================

/**
 * @brief Frecuencia de presentacion del nivel n, en Hz.
 *
 * Arranca en 1,0 Hz y sube 0,5 Hz cada dos niveles:
 * 1-2 -> 1,0 | 3-4 -> 1,5 | 5-6 -> 2,0 | 7-8 -> 2,5 | 9 -> 3,0.
 * La division entera (n-1)/2 produce el escalon.
 */
float frecuencia(int n) {
  return 1.0 + 0.5 * ((n - 1) / 2);
}

/** @brief Suma el tiempo de un intento al acumulado. Satura en 99 s. */
void sumarTiempo(unsigned long ms) {
  tiempoAcumMs += ms;
  if (tiempoAcumMs > 99000UL) tiempoAcumMs = 99000UL;
}

/** @brief Refleja el estado de la partida en los cuatro digitos. */
void actualizarTablero() {
  tablero.mostrar(nivel, vidas, tiempoAcumMs / 1000);
}

/** @brief Prepara el nivel actual y arranca la presentacion de su secuencia. */
void iniciarNivel() {
  float f   = frecuencia(nivel);
  periodo   = (int)(1000.0 / f);
  encendido = (int)(periodo * DUTY);
  tMaximo   = (unsigned long)(nivel * periodo * FACTOR_RESPUESTA);

  actualizarTablero();

  Serial.print("Nivel ");
  Serial.print(nivel);
  Serial.print(" -> secuencia: ");
  for (int i = 0; i < nivel; i++) {
    Serial.print(secuencia[i] + 1);
    Serial.print(" ");
  }
  Serial.print(" | Tpres ");
  Serial.print(nivel * periodo);
  Serial.print(" ms | Tmax ");
  Serial.print(tMaximo);
  Serial.println(" ms");

  luces.apagarTodo();            // incluye cancelar un eco pendiente

  paso = 0;
  ledEncendido = true;
  tPaso = millis();
  luces.encender(secuencia[0]);
  estado = PRESENTACION;
}

/** @brief Sube de nivel conservando la secuencia y anadiendo un elemento al final. */
void subirNivel() {
  if (nivel < NIVEL_MAX) {
    secuencia[nivel] = random(0, 4);
    nivel++;
  } else {
    nivel = 1;
    secuencia[0] = random(0, 4);
  }
}

/** @brief Deja el juego en espera con los valores iniciales. */
void reiniciarPartida() {
  nivel = 1;
  vidas = VIDAS_INICIALES;
  tiempoAcumMs = 0;
  irAFin = false;
  subirDespues = false;

  luces.apagarTodo();
  actualizarTablero();
  estado = ESPERA;

  Serial.println("\n-- En espera. Pulsa INICIO para comenzar. --");
}

/**
 * @brief Comienza una partida nueva.
 *
 * La semilla se toma AQUI, en el instante en que el jugador pulso INICIO. Ese
 * momento es impredecible, asi que la secuencia cambia en cada partida. Si se
 * sembrara en setup(), micros() valdria casi lo mismo siempre.
 */
void empezarPartida() {
  randomSeed(micros());
  nivel = 1;
  vidas = VIDAS_INICIALES;
  tiempoAcumMs = 0;
  irAFin = false;
  subirDespues = false;
  secuencia[0] = random(0, 4);

  Serial.println("\n=== PARTIDA NUEVA ===");
  iniciarNivel();
}

/** @brief El jugador reprodujo toda la secuencia correctamente. */
void acierto(unsigned long ahora) {
  sumarTiempo(ahora - tInicioIngreso);
  luces.tiempo(false);

  Serial.print("  == NIVEL SUPERADO | intento ");
  Serial.print(ahora - tInicioIngreso);
  Serial.print(" ms | acumulado ");
  Serial.print(tiempoAcumMs / 1000);
  Serial.println(" s ==");

  actualizarTablero();

  if (nivel >= NIVEL_MAX) {
    Serial.println("  *** NIVEL 9 COMPLETADO: PARTIDA GANADA ***");
    irAFin = true;
  } else {
    subirDespues = true;
  }

  tPaso = ahora;
  estado = PAUSA;
}

/**
 * @brief El jugador se equivoco o se le acabo el tiempo.
 * @param porTiempo true si vencio el plazo; false si pulso el LED equivocado.
 *
 * Si vencio el tiempo se suma el maximo completo; si fallo antes, lo que
 * alcanzo a usar. El enunciado incluye los intentos vencidos en el acumulado.
 */
void fallo(unsigned long ahora, bool porTiempo) {
  sumarTiempo(porTiempo ? tMaximo : (ahora - tInicioIngreso));
  luces.tiempo(false);

  if (modoVidas) {
    vidas--;
    Serial.print("  == FALLASTE | vidas restantes: ");
    Serial.print(vidas);
  } else {
    Serial.print("  == FALLASTE (vidas infinitas)");
  }
  Serial.print(" | acumulado ");
  Serial.print(tiempoAcumMs / 1000);
  Serial.println(" s ==");

  if (modoVidas && vidas <= 0) {
    Serial.println("  *** SIN VIDAS: FIN DE PARTIDA ***");
    irAFin = true;
  }

  actualizarTablero();
  subirDespues = false;          // se repite el mismo nivel con la misma secuencia
  tPaso = ahora;
  estado = PAUSA;
}

/**
 * @brief Comandos de prueba por el monitor serie.
 *
 * Permiten verificar los requisitos por separado, sin que el limite de tiempo
 * o las vidas interrumpan la prueba de otra cosa.
 *   t -> activa/desactiva el limite de tiempo
 *   v -> activa/desactiva las vidas
 *   r -> reinicia la partida
 *   e -> imprime el estado actual
 */
void leerComandos() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == 't') {
      modoTiempo = !modoTiempo;
      Serial.print("\n*** limite de tiempo: ");
      Serial.println(modoTiempo ? "ACTIVADO ***" : "DESACTIVADO ***");
      if (!modoTiempo) luces.tiempo(false);
    }
    else if (c == 'v') {
      modoVidas = !modoVidas;
      Serial.print("\n*** vidas: ");
      Serial.println(modoVidas ? "ACTIVADAS ***" : "INFINITAS ***");
    }
    else if (c == 'r') {
      Serial.println("\n*** reinicio manual ***");
      reiniciarPartida();
    }
    else if (c == 'e') {
      Serial.print("\n*** nivel ");      Serial.print(nivel);
      Serial.print(" | vidas ");         Serial.print(vidas);
      Serial.print(" | acumulado ");     Serial.print(tiempoAcumMs / 1000);
      Serial.print(" s | modo tiempo "); Serial.print(modoTiempo ? "ON" : "OFF");
      Serial.print(" | modo vidas ");    Serial.print(modoVidas ? "ON" : "OFF");
      Serial.println(" ***");
    }
  }
}

// =====================================================================
// 6. COORDINACION
// =====================================================================

void setup() {
  Serial.begin(115200);

  tablero.iniciar();
  luces.iniciar();
  for (int i = 0; i < 4; i++) btn[i].iniciar(PIN_BOTON[i]);
  btnInicio.iniciar(PIN_BOTON_INICIO);

  Serial.println("\nJuego de memoria - Practica 2 (version Arduino)");
  Serial.println("Comandos: t=limite tiempo  v=vidas  r=reiniciar  e=estado");

  reiniciarPartida();
}

void loop() {
  // El barrido de los displays va PRIMERO y sin condiciones: sostiene la
  // ilusion de los cuatro digitos encendidos. Va antes de cualquier return.
  tablero.refrescar();

  unsigned long ahora = millis();   // una sola marca de tiempo por vuelta

  // --- Entradas y comandos: siempre, en todos los estados ---
  leerComandos();
  for (int i = 0; i < 4; i++) btn[i].leer(ahora);
  btnInicio.leer(ahora);
  luces.actualizar();               // apaga el eco cuando le toca

  // --- Reinicio por pulsacion sostenida: vale en cualquier estado ---
  if (btnInicio.sostenido(ahora, T_RESET) && !resetYaHecho) {
    resetYaHecho = true;
    btnInicio.descartar();          // que no cuente ademas como pulsacion corta
    Serial.println("\n-- Reinicio por pulsacion sostenida --");
    reiniciarPartida();
    return;
  }
  if (!btnInicio.estaPresionado()) resetYaHecho = false;   // listo para otro reset

  // --- Maquina de estados ---
  if (estado == ESPERA) {
    if (btnInicio.presionado()) empezarPartida();
  }

  else if (estado == PRESENTACION) {
    // Los pulsadores 1-4 no generan entradas aqui: simplemente no se consultan.
    unsigned long transcurrido = ahora - tPaso;

    if (ledEncendido && transcurrido >= (unsigned long)encendido) {
      luces.apagar(secuencia[paso]);      // el apagado separa elementos iguales
      ledEncendido = false;
    }
    else if (!ledEncendido && transcurrido >= (unsigned long)periodo) {
      paso++;
      if (paso >= nivel) {                // termino la presentacion: turno del jugador
        indiceJugador = 0;
        tInicioIngreso = ahora;           // arranca el cronometro de respuesta
        luces.palpitoReiniciar();

        // Descarta pulsaciones hechas DURANTE la presentacion.
        for (int i = 0; i < 4; i++) btn[i].descartar();

        Serial.println("  tu turno...");
        estado = INGRESO;
      } else {
        tPaso = ahora;
        ledEncendido = true;
        luces.encender(secuencia[paso]);
      }
    }
  }

  else if (estado == INGRESO) {
    unsigned long t = ahora - tInicioIngreso;

    if (modoTiempo) {
      // El palpito se acelera de forma lineal conforme se agota el plazo.
      float avance = (float)t / (float)tMaximo;
      if (avance > 1.0) avance = 1.0;
      int perPalpito = PALPITO_LENTO - (int)((PALPITO_LENTO - PALPITO_RAPIDO) * avance);
      luces.palpito(perPalpito);

      if (t >= tMaximo) {
        Serial.println("  TIEMPO AGOTADO");
        fallo(ahora, true);
        return;
      }
    }

    for (int i = 0; i < 4; i++) {
      if (btn[i].presionado()) {
        luces.eco(i);                     // confirma la entrada reconocida

        Serial.print("  pulsaste ");
        Serial.print(i + 1);

        if (i == secuencia[indiceJugador]) {
          indiceJugador++;
          Serial.print("  ok (");
          Serial.print(indiceJugador);
          Serial.print("/");
          Serial.print(nivel);
          Serial.println(")");

          if (indiceJugador >= nivel) acierto(ahora);
        }
        else {
          Serial.print("  MAL, era el ");
          Serial.println(secuencia[indiceJugador] + 1);
          fallo(ahora, false);
        }
        break;                            // solo un boton por vuelta
      }
    }
  }

  else if (estado == PAUSA) {
    if (ahora - tPaso >= T_PAUSA) {
      if (irAFin) {
        irAFin = false;
        estado = FIN;
        Serial.println("-- Fin de partida. Manten INICIO 2 s para reiniciar. --");
      } else {
        if (subirDespues) subirNivel();   // acierto -> nivel nuevo + elemento nuevo
        iniciarNivel();                   // fallo   -> mismo nivel, misma secuencia
      }
    }
  }

  else if (estado == FIN) {
    // El tablero conserva nivel, vidas y tiempo alcanzados.
    // Solo se sale con la pulsacion sostenida de 2 s.
    btnInicio.descartar();
  }
}
