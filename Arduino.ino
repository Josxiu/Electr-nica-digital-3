// Etapa 0+1: secuencia en los LEDs, display TM1637 y pulsadores con antirrebote.
// Muestra nivel 1, espera, agrega un elemento, muestra nivel 2, y repite.
// Sin delay(): el loop nunca se queda esperando, por eso puede atender los
// pulsadores en cualquier instante.

#include <TM1637Display.h>

const int LED[4] = {2, 3, 4, 5};     // <-- tus GPIO de los LEDs 1 a 4

const int BOTON[4] = {6, 7, 8, 9};   // <-- tus GPIO de los pulsadores 1 a 4

const unsigned long T_ANTIRREBOTE = 25;   // ms que debe sostenerse una lectura

/**
 * Pulsador con antirrebote.
 *
 * Un contacto mecanico rebota unos milisegundos al cerrarse, asi que una sola
 * pulsacion se leeria como varias. Por eso un cambio en la lectura solo se
 * acepta si se mantuvo estable durante T_ANTIRREBOTE.
 *
 * NOTA: el struct va aqui arriba, antes de la primera funcion del archivo.
 * El IDE de Arduino inserta los prototipos justo antes de la primera funcion,
 * y esos prototipos ya mencionan el tipo Boton.
 */
struct Boton {
  int  pin;
  bool lectura;              // ultima lectura cruda del pin
  bool estable;              // estado ya validado (true = presionado)
  bool flanco;               // true en el instante de la pulsacion
  unsigned long tCambio;     // cuando cambio la lectura cruda
};

Boton btn[4];

// ---------- MODULO DISPLAY (TM1637, 4 digitos) ----------
const int PIN_CLK = 15;            // <-- tus GPIO del TM1637
const int PIN_DIO = 14;

// Se deja el bitDelay por defecto (100 us). Solo despues de verlo funcionar
// vale la pena bajarlo: TM1637Display display(PIN_CLK, PIN_DIO, 50);
TM1637Display display(PIN_CLK, PIN_DIO);

// Ultimo valor escrito en el display, para no reescribir lo mismo.
// Se inicializa en -1 para forzar la primera escritura.
int ultNivel = -1, ultVidas = -1, ultTiempo = -1;

/**
 * Escribe los cuatro digitos del tablero, pero solo si algo cambio.
 * Cada escritura real bloquea ~20 ms, asi que no conviene repetirla en vano.
 *
 * Digito 1 = nivel | Digito 2 = vidas | Digitos 3-4 = tiempo acumulado (00-99).
 */
void mostrarTablero(int nivel, int vidas, int tiempo) {
  if (nivel == ultNivel && vidas == ultVidas && tiempo == ultTiempo) return;
  ultNivel = nivel; ultVidas = vidas; ultTiempo = tiempo;

  uint8_t digitos[4];
  digitos[0] = display.encodeDigit(nivel % 10);
  digitos[1] = display.encodeDigit(vidas % 10);
  digitos[2] = display.encodeDigit((tiempo / 10) % 10);   // decenas
  digitos[3] = display.encodeDigit(tiempo % 10);          // unidades
  display.setSegments(digitos);
}
// --------------------------------------------------------

const int NIVEL_MAX = 9;
int secuencia[9];                  // aqui se guarda la secuencia
int nivel = 1;
int vidas = 3;
int tiempo = 0;

// --- Estado de la presentacion ---
enum Estado { PRESENTACION, INGRESO, PAUSA };
Estado estado = PRESENTACION;

int  periodo = 1000;        // duracion de cada elemento, en ms
int  encendido = 700;       // parte del periodo con el LED prendido
int  paso = 0;              // cual elemento se esta mostrando
bool ledEncendido = false;  // si el LED del paso actual esta prendido
unsigned long tPaso = 0;    // instante en que empezo el elemento actual
int  indiceJugador = 0;     // cuantos elementos correctos lleva el jugador
bool subirDespues = false;  // al terminar la pausa: ¿subir de nivel o repetir?

/**
 * Prepara el nivel actual y arranca la presentacion de su secuencia.
 */
void iniciarNivel() {
  float f   = 1.0 + 0.5 * ((nivel - 1) / 2);
  periodo   = 1000 / f;
  encendido = periodo * 0.7;

  mostrarTablero(nivel, vidas, 0);

  Serial.print("Nivel ");
  Serial.print(nivel);
  Serial.print(" -> secuencia: ");
  for (int i = 0; i < nivel; i++) {
    Serial.print(secuencia[i] + 1);
    Serial.print(" ");
  }
  Serial.println();

  paso = 0;
  ledEncendido = true;
  tPaso = millis();
  digitalWrite(LED[secuencia[0]], HIGH);
  estado = PRESENTACION;
}

/**
 * Sube de nivel conservando la secuencia y agregando un elemento al final.
 */
void subirNivel() {
  if (nivel < NIVEL_MAX) {
    secuencia[nivel] = random(0, 4);
    nivel++;
  } else {
    nivel = 1;
    secuencia[0] = random(0, 4);
  }
}

/** Configura el pin del pulsador y lo deja en estado conocido. */
void botonIniciar(Boton &b, int pin) {
  b.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  b.lectura = false;
  b.estable = false;
  b.flanco  = false;
  b.tCambio = 0;
}

/** Lee el pin y valida el cambio si ya paso el tiempo de antirrebote. */
void botonLeer(Boton &b, unsigned long ahora) {
  bool lect = (digitalRead(b.pin) == LOW);   // pull-up: LOW = presionado

  if (lect != b.lectura) {        // cambio la lectura: reinicia el conteo
    b.lectura = lect;
    b.tCambio = ahora;
  }

  if ((ahora - b.tCambio) >= T_ANTIRREBOTE && lect != b.estable) {
    b.estable = lect;
    if (b.estable) b.flanco = true;   // flanco: se acaba de presionar
  }
}

/**
 * Devuelve true UNA sola vez por pulsacion y la consume.
 * Sostener el boton no genera entradas repetidas.
 */
bool botonPresionado(Boton &b) {
  if (!b.flanco) return false;
  b.flanco = false;
  return true;
}

/**
 * El jugador reprodujo toda la secuencia correctamente.
 * Se programa una pausa y luego se sube de nivel.
 */
void acierto(unsigned long ahora) {
  Serial.println("  == NIVEL SUPERADO ==");
  subirDespues = true;
  tPaso = ahora;
  estado = PAUSA;
}

/**
 * El jugador se equivoco. Pierde una vida y repite el MISMO nivel con la
 * MISMA secuencia. Si se queda sin vidas, la partida vuelve a empezar.
 */
void fallo(unsigned long ahora) {
  vidas--;
  Serial.print("  == FALLASTE | vidas restantes: ");
  Serial.println(vidas);

  if (vidas <= 0) {
    Serial.println("  == SIN VIDAS: partida reiniciada ==");
    nivel = 1;
    vidas = 3;
    secuencia[0] = random(0, 4);   // secuencia nueva
  }

  subirDespues = false;   // repetir nivel (o empezar de cero si se reinicio)
  tPaso = ahora;
  estado = PAUSA;
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(LED[i], OUTPUT);
    digitalWrite(LED[i], LOW);
    botonIniciar(btn[i], BOTON[i]);
  }

  display.setBrightness(3);        // 0 = mas tenue, 7 = mas brillante

  randomSeed(micros());            // para que no salga siempre la misma
  secuencia[0] = random(0, 4);     // primer elemento del nivel 1
  iniciarNivel();                  // arranca el primer nivel
}

void loop() {
  unsigned long ahora = millis();   // una sola marca de tiempo por vuelta

  // --- Leer las entradas: SIEMPRE, en cada vuelta, pase lo que pase ---
  for (int i = 0; i < 4; i++) botonLeer(btn[i], ahora);



  if (estado == PRESENTACION) {
    unsigned long transcurrido = ahora - tPaso;

    // ¿ya toca apagar el LED de este elemento?
    if (ledEncendido && transcurrido >= encendido) {
      digitalWrite(LED[secuencia[paso]], LOW);
      ledEncendido = false;
    }
    // ¿ya se acabo el periodo completo? -> pasar al siguiente elemento
    else if (!ledEncendido && transcurrido >= periodo) {
      paso++;
      if (paso >= nivel) {          // se acabo la presentacion: turno del jugador
        indiceJugador = 0;
        // Descarta pulsaciones hechas DURANTE la presentacion: ahi los
        // pulsadores estan desactivados y no deben contar como entrada.
        for (int i = 0; i < 4; i++) botonPresionado(btn[i]);
        Serial.println("  tu turno...");
        estado = INGRESO;
      } else {
        tPaso = ahora;
        ledEncendido = true;
        digitalWrite(LED[secuencia[paso]], HIGH);
      }
    }
  }

    else if (estado == INGRESO) {
    for (int i = 0; i < 4; i++) {
      if (botonPresionado(btn[i])) {
        Serial.print("  pulsaste ");
        Serial.print(i + 1);

        if (i == secuencia[indiceJugador]) {      // acerto este elemento
          indiceJugador++;
          Serial.print("  ok (");
          Serial.print(indiceJugador);
          Serial.print("/");
          Serial.print(nivel);
          Serial.println(")");

          if (indiceJugador >= nivel) acierto(ahora);   // completo la secuencia
        }
        else {                                    // se equivoco
          Serial.print("  MAL, era el ");
          Serial.println(secuencia[indiceJugador] + 1);
          fallo(ahora);
        }
        break;   // solo un boton por vuelta
      }
    }
  }

  else if (estado == PAUSA) {
    if (ahora - tPaso >= 1500) {
      if (subirDespues) subirNivel();   // acierto -> nivel nuevo + elemento nuevo
      iniciarNivel();                   // fallo   -> mismo nivel, misma secuencia
    }
  }
}
