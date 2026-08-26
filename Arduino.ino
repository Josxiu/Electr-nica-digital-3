// Etapa 0: ver la secuencia en los LEDs + probar el display TM1637.
// Muestra nivel 1, espera, agrega un elemento, muestra nivel 2, y repite.

#include <TM1637Display.h>

const int LED[4] = {2, 3, 4, 5};   // <-- tus GPIO de los LEDs 1 a 4

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

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(LED[i], OUTPUT);
    digitalWrite(LED[i], LOW);
  }

  display.setBrightness(3);        // 0 = mas tenue, 7 = mas brillante

  randomSeed(micros());            // para que no salga siempre la misma
  secuencia[0] = random(0, 4);     // primer elemento del nivel 1
}

void loop() {
  // --- frecuencia del nivel: 1-2 -> 1.0 Hz, 3-4 -> 1.5 Hz, etc. ---
  float f = 1.0 + 0.5 * ((nivel - 1) / 2);
  int periodo = 1000 / f;          // duracion de cada elemento, en ms
  int encendido = periodo * 0.7;   // 70% prendido
  int apagado   = periodo - encendido;  // 30% apagado: separa dos elementos iguales

  mostrarTablero(nivel, vidas, 0);      // se refresca al entrar al nivel

  Serial.print("Nivel ");
  Serial.print(nivel);
  Serial.print(" -> secuencia: ");

  // --- mostrar la secuencia ---
  for (int i = 0; i < nivel; i++) {
    Serial.print(secuencia[i] + 1);
    Serial.print(" ");

    digitalWrite(LED[secuencia[i]], HIGH);
    delay(encendido);
    digitalWrite(LED[secuencia[i]], LOW);
    delay(apagado);
  }
  Serial.println();

  delay(2000);   // pausa para ver donde termina una secuencia y empieza la otra

  // --- subir de nivel: se CONSERVA lo anterior y se agrega uno al final ---
  if (nivel < NIVEL_MAX) {
    secuencia[nivel] = random(0, 4);
    nivel++;
  } else {
    nivel = 1;                     // vuelve a empezar
    secuencia[0] = random(0, 4);
  }
}
