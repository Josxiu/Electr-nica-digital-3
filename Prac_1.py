"""
Etapa 0 - version MicroPython: ver la secuencia en los LEDs + display TM1637.
Muestra nivel 1, agrega un elemento, muestra nivel 2, y asi hasta el 9.

No usa librerias externas: el driver del TM1637 esta incluido abajo.
La logica del juego no bloquea: todo se resuelve comparando marcas de tiempo
con ticks_ms(), para poder agregar despues los pulsadores sin perder pulsaciones.
"""

from machine import Pin
from time import ticks_ms, ticks_us, ticks_diff, sleep_us
import random

# =====================================================================
# CONFIGURACION
# =====================================================================

PINES_LED = (2, 3, 4, 5)    # GPIO de los LEDs 1 a 4
PIN_CLK   = 15              # GPIO del TM1637
PIN_DIO   = 14

NIVEL_MAX     = 9
VIDAS_INICIAL = 3
PAUSA_MS      = 2000        # pausa entre una secuencia y la siguiente
DUTY          = 0.7         # 70% encendido; el 30% apagado separa elementos iguales


# =====================================================================
# CAPA DE DISPOSITIVO: driver del TM1637
# =====================================================================

# Codigos de 7 segmentos para los digitos 0-9.
# Cada bit es un segmento:  a=0x01  b=0x02  c=0x04  d=0x08  e=0x10  f=0x20  g=0x40
SEGMENTOS = (0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F)

_RETARDO_US = 10            # separacion entre flancos del bus (ver hoja de datos)


class DriverTM1637:
    """
    Driver minimo para el TM1637: solo lo necesario para escribir cuatro digitos.

    El bus es de dos hilos (CLK y DIO), parecido a I2C pero no compatible.
    Los bits se envian con el bit menos significativo primero.
    """

    CMD_DATOS = 0x40    # escritura con autoincremento de direccion
    CMD_DIR   = 0xC0    # direccion del primer digito
    CMD_CTRL  = 0x88    # display encendido; los 3 bits bajos son el brillo

    def __init__(self, clk, dio, brillo=3):
        self.clk = Pin(clk, Pin.OUT, value=0)
        self.dio = Pin(dio, Pin.OUT, value=0)
        self.brillo = brillo & 0x07
        self._comando(self.CMD_CTRL | self.brillo)   # enciende el display

    def _inicio(self):
        """Condicion de arranque de una trama."""
        self.dio(0); sleep_us(_RETARDO_US)
        self.clk(0); sleep_us(_RETARDO_US)

    def _fin(self):
        """Condicion de cierre de una trama."""
        self.dio(0); sleep_us(_RETARDO_US)
        self.clk(1); sleep_us(_RETARDO_US)
        self.dio(1)

    def _byte(self, b):
        """Envia un byte, bit menos significativo primero, y el ciclo de ACK."""
        for i in range(8):
            self.dio((b >> i) & 1)
            sleep_us(_RETARDO_US)
            self.clk(1)
            sleep_us(_RETARDO_US)
            self.clk(0)
            sleep_us(_RETARDO_US)
        # ciclo de reconocimiento del chip
        self.clk(0); sleep_us(_RETARDO_US)
        self.clk(1); sleep_us(_RETARDO_US)
        self.clk(0); sleep_us(_RETARDO_US)

    def _comando(self, b):
        self._inicio()
        self._byte(b)
        self._fin()

    def escribir(self, segmentos):
        """Escribe la lista de segmentos empezando por el primer digito."""
        self._comando(self.CMD_DATOS)
        self._inicio()
        self._byte(self.CMD_DIR)
        for s in segmentos:
            self._byte(s)
        self._fin()
        self._comando(self.CMD_CTRL | self.brillo)


# =====================================================================
# CAPA DE DISPOSITIVO: LEDs y tablero
# =====================================================================

class BancoLeds:
    """Los cuatro LEDs de la secuencia."""

    def __init__(self, pines):
        self.leds = [Pin(p, Pin.OUT) for p in pines]
        self.apagar_todos()

    def encender(self, i):
        self.leds[i].value(1)

    def apagar(self, i):
        self.leds[i].value(0)

    def apagar_todos(self):
        for led in self.leds:
            led.value(0)


class Tablero:
    """
    Los cuatro digitos del juego.
    Digito 1 = nivel | Digito 2 = vidas | Digitos 3-4 = tiempo acumulado (00-99).

    Guarda el ultimo valor escrito y no reescribe si nada cambio: cada escritura
    real tarda varios milisegundos y no tiene sentido gastarlos en vano.
    """

    def __init__(self, clk, dio, brillo=3):
        self.driver = DriverTM1637(clk, dio, brillo)
        self._ultimo = None

    def mostrar(self, nivel, vidas, tiempo):
        valores = (nivel, vidas, tiempo)
        if valores == self._ultimo:
            return
        self._ultimo = valores

        self.driver.escribir([
            SEGMENTOS[nivel % 10],
            SEGMENTOS[vidas % 10],
            SEGMENTOS[(tiempo // 10) % 10],   # decenas
            SEGMENTOS[tiempo % 10],           # unidades
        ])


# =====================================================================
# LOGICA DEL JUEGO
# =====================================================================

class JuegoEtapa0:
    """
    Maquina de estados minima: presenta la secuencia, hace una pausa,
    sube de nivel y vuelve a presentar.

    Estados:
      PRESENTACION - recorriendo los elementos de la secuencia
      PAUSA        - intervalo entre una secuencia y la siguiente
    """

    def __init__(self, leds, tablero):
        self.leds = leds
        self.tablero = tablero
        self.nivel = 1
        self.vidas = VIDAS_INICIAL
        self.secuencia = [random.getrandbits(2)]   # 2 bits -> valores 0 a 3
        self.estado = "PRESENTACION"

    def frecuencia(self):
        """Hz del nivel actual: 1-2 -> 1,0; 3-4 -> 1,5; 5-6 -> 2,0; 7-8 -> 2,5; 9 -> 3,0."""
        return 1.0 + 0.5 * ((self.nivel - 1) // 2)

    def iniciar_nivel(self, ahora):
        """Calcula los tiempos del nivel y arranca la presentacion."""
        self.periodo_ms    = int(1000 / self.frecuencia())   # duracion de cada elemento
        self.t_encendido   = int(self.periodo_ms * DUTY)
        self.paso          = 0
        self.led_encendido = True
        self.t_paso        = ahora

        # El display se escribe AQUI, antes de que empiecen los tiempos criticos.
        self.tablero.mostrar(self.nivel, self.vidas, 0)

        self.leds.encender(self.secuencia[0])
        print("Nivel {} | f = {} Hz | Tpres = {} ms | secuencia: {}".format(
            self.nivel,
            self.frecuencia(),
            self.nivel * self.periodo_ms,
            [x + 1 for x in self.secuencia]))

    def siguiente_nivel(self):
        """Se CONSERVA la secuencia anterior y se agrega un elemento al final."""
        if self.nivel < NIVEL_MAX:
            self.secuencia.append(random.getrandbits(2))
            self.nivel += 1
        else:
            self.nivel = 1
            self.secuencia = [random.getrandbits(2)]

    def actualizar(self, ahora):
        """Se llama en cada vuelta del bucle principal. Nunca espera."""

        if self.estado == "PRESENTACION":
            transcurrido = ticks_diff(ahora, self.t_paso)

            if self.led_encendido and transcurrido >= self.t_encendido:
                # apagado que hace distinguibles dos elementos iguales seguidos
                self.leds.apagar(self.secuencia[self.paso])
                self.led_encendido = False

            elif not self.led_encendido and transcurrido >= self.periodo_ms:
                self.paso += 1
                if self.paso >= self.nivel:
                    self.estado = "PAUSA"
                    self.t_paso = ahora
                else:
                    self.t_paso = ahora
                    self.led_encendido = True
                    self.leds.encender(self.secuencia[self.paso])

        elif self.estado == "PAUSA":
            if ticks_diff(ahora, self.t_paso) >= PAUSA_MS:
                self.siguiente_nivel()
                self.estado = "PRESENTACION"
                self.iniciar_nivel(ahora)


# =====================================================================
# COORDINACION
# =====================================================================

def main():
    # La semilla depende del instante de arranque; sin esto la secuencia
    # podria repetirse igual en cada encendido.
    random.seed(ticks_us())

    leds = BancoLeds(PINES_LED)
    tablero = Tablero(PIN_CLK, PIN_DIO)
    juego = JuegoEtapa0(leds, tablero)

    print("\nJuego de memoria - etapa 0 (MicroPython)")
    juego.iniciar_nivel(ticks_ms())

    while True:
        juego.actualizar(ticks_ms())


main()
