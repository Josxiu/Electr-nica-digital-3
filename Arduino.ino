// Etapa 0: ver la secuencia en los LEDs + probar el display TM1637.
// Muestra nivel 1, espera, agrega un elemento, muestra nivel 2, y repite.

#include <TM1637Display.h>

const int LED[4] = {2, 3, 4, 5};   // <-- tus GPIO de los LEDs 1 a 4

const int BOTON[4] = {7, 8, 9, 10};   // <-- tus GPIO de los pulsadores 1 a 4

const unsigned long T_ANTIRREBOTE = 25;   // ms que debe sostenerse una lectura

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
enum Estado { PRESENTACION, PAUSA };
Estado estado = PRESENTACION;

int  periodo = 1000;        // duracion de cada elemento, en ms
int  encendido = 700;       // parte del periodo con el LED prendido
int  paso = 0;              // cual elemento se esta mostrando
bool ledEncendido = false;  // si el LED del paso actual esta prendido
unsigned long tPaso = 0;    // instante en que empezo el elemento actual


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

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(LED[i], OUTPUT);
    digitalWrite(LED[i], LOW);
  }

  display.setBrightness(3);        // 0 = mas tenue, 7 = mas brillante

  randomSeed(micros());            // para que no salga siempre la misma
  secuencia[0] = random(0, 4);     // primer elemento del nivel 1
  iniciarNivel();     // <-- arranca el primer nivel
}

void loop() {
  unsigned long ahora = millis();   // una sola marca de tiempo por vuelta

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
      if (paso >= nivel) {          // se acabo la secuencia
        estado = PAUSA;
        tPaso = ahora;
      } else {
        tPaso = ahora;
        ledEncendido = true;
        digitalWrite(LED[secuencia[paso]], HIGH);
      }
    }
  }

  else if (estado == PAUSA) {
    if (ahora - tPaso >= 2000) {
      subirNivel();
      iniciarNivel();
    }
  }
}
