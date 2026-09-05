from machine import Pin
from time import ticks_ms, ticks_us, ticks_diff, sleep_us
import random

from machine import Timer

# =====================================================================
# CONFIGURACIÓN DE PINES
# =====================================================================

LEDS_P      = (2, 3, 4, 5)     # Pines de los LEDS
BOTONES     = (6, 7, 8, 9)     # Pines de los botones
BTN_CONTROL = 10               # Botón para iniciar o reiniciar el juego

# Ánodos (D1, D2, D3, D4)
DIGITOS_PINES   = (11, 12, 13, 14)

# Cátodos (A, B, C, D, E, F, G, DP)
SEGMENTOS_PINES = (15, 16, 17, 18, 19, 20, 21, 22)

# CONSTANTES DEL JUEGO
NIVEL_MAX = 9
VIDAS     = 3
PAUSA_MS  = 1500  # Tiempo entre secuencias o nivel
DUTY      = 0.7   # 70% del periodo encendido y 30% apagado
DEBOUNCE  = 50    # Evita leer múltiples veces una sola presión en los botones
REINICIO  = 2000  # Tiempo necesario al presionar el botón de control para reiniciar
TURNO     = 5000  # Tiempo inicial para que el jugador responda.

NUMEROS = {
    0: [0, 0, 0, 0, 0, 0, 1, 1],
    1: [1, 0, 0, 1, 1, 1, 1, 1],
    2: [0, 0, 1, 0, 0, 1, 0, 1],
    3: [0, 0, 0, 0, 1, 1, 0, 1],
    4: [1, 0, 0, 1, 1, 0, 0, 1],
    5: [0, 1, 0, 0, 1, 0, 0, 1],
    6: [0, 1, 0, 0, 0, 0, 0, 1],
    7: [0, 0, 0, 1, 1, 1, 1, 1],
    8: [0, 0, 0, 0, 0, 0, 0, 1],
    9: [0, 0, 0, 0, 1, 0, 0, 1],
    ' ': [1, 1, 1, 1, 1, 1, 1, 1],  # Apagado
    '-': [1, 1, 1, 1, 1, 1, 0, 1],  # Guión
}


# =====================================================================
# CLASES DE HARDWARE
# =====================================================================

class Segmentos:
    def __init__(self, DIGITOS_PINES, SEGMENTOS_PINES):
        self.digitos = [Pin(p, Pin.OUT, value=0) for p in DIGITOS_PINES]
        self.segmentos = [Pin(p, Pin.OUT, value=1) for p in SEGMENTOS_PINES]
        self.buffer = [' ', ' ', ' ', ' ']
        self.digito_actual = 0

    def fijar_valores(self, d1, d2, d3, d4):
        self.buffer[0] = d1
        self.buffer[1] = d2
        self.buffer[2] = d3
        self.buffer[3] = d4

    def refrescar(self):
        """Conmuta rápidamente al siguiente dígito en cada llamada."""
        # 1. Apagar dígito anterior (Ánodo LOW) para evitar fantasmas (ghosting)
        self.digitos[self.digito_actual].value(0)

        # 2. Seleccionar el siguiente dígito
        self.digito_actual = (self.digito_actual + 1) % 4
        val = self.buffer[self.digito_actual]
        patron = NUMEROS.get(val, NUMEROS[' '])

        # 3. Aplicar patrón a los Cátodos (0=Encendido, 1=Apagado)
        for i in range(8):
            self.segmentos[i].value(patron[i])

        # 4. Encender el dígito activo (Ánodo HIGH)
        self.digitos[self.digito_actual].value(1)


class LEDS:
    def __init__(self, pines):
        self.leds = [Pin(p, Pin.OUT, value=0) for p in pines]

    def encender(self, i):
        self.leds[i].value(1)

    def apagar(self, i):
        self.leds[i].value(0)

    def apagar_todos(self):
        for led in self.leds:
            led.value(0)

    def encender_todos(self):
        for led in self.leds:
            led.value(1)


class Botones:
    def __init__(self, pines_juego, pin_control):
        self.botones_juego = [Pin(p, Pin.IN, Pin.PULL_UP) for p in pines_juego]
        self.btn_control = Pin(pin_control, Pin.IN, Pin.PULL_UP)

        self.estado_juego = [0] * len(pines_juego)
        self.tiempo_juego = [0] * len(pines_juego)

        self.estado_ctrl = 1
        self.t_cambio_ctrl = 0
        self.t_presionado_ctrl = 0
        self.procesado_largo_ctrl = False

    def leer_juego(self, ahora):
        for i, boton in enumerate(self.botones_juego):
            lectura = boton.value()
            if lectura != self.estado_juego[i]:
                self.tiempo_juego[i] = ahora
                self.estado_juego[i] = lectura

            if ticks_diff(ahora, self.tiempo_juego[i]) > DEBOUNCE:
                if lectura == 0 and not hasattr(self, f"_proc_{i}"):
                    setattr(self, f"_proc_{i}", True)
                    return i
                elif lectura == 1 and hasattr(self, f"_proc_{i}"):
                    delattr(self, f"_proc_{i}")
        return None

    def leer_control(self, ahora):
        lectura = self.btn_control.value()

        if lectura != self.estado_ctrl:
            self.t_cambio_ctrl = ahora
            self.estado_ctrl = lectura
            if lectura == 0:
                self.t_presionado_ctrl = ahora
                self.procesado_largo_ctrl = False

        if ticks_diff(ahora, self.t_cambio_ctrl) > DEBOUNCE:
            if self.estado_ctrl == 0:
                if not self.procesado_largo_ctrl and ticks_diff(ahora, self.t_presionado_ctrl) >= REINICIO:
                    self.procesado_largo_ctrl = True
                    return "LARGO"
            else:
                if hasattr(self, "_esperando_soltar_ctrl"):
                    delattr(self, "_esperando_soltar_ctrl")
                    if not self.procesado_largo_ctrl:
                        return "CORTO"

            if self.estado_ctrl == 0 and not self.procesado_largo_ctrl:
                setattr(self, "_esperando_soltar_ctrl", True)

        return None


class Tablero:
    """Interfaz entre la lógica del juego y el display 3461BS."""
    def __init__(self, DIGITOS_PINES, SEGMENTOS_PINES):
        self.driver = Segmentos(DIGITOS_PINES, SEGMENTOS_PINES)

    def mostrar(self, nivel, vidas, tiempo_seg):
        t_limitado = min(99, max(0, tiempo_seg))
        d1 = nivel % 10
        d2 = vidas % 10
        d3 = (t_limitado // 10) % 10
        d4 = t_limitado % 10
        self.driver.fijar_valores(d1, d2, d3, d4)

    def mostrar_guiones(self):
        self.driver.fijar_valores('-', '-', '-', '-')

    def refrescar(self):
        """Llama al multiplexado del driver."""
        self.driver.refrescar()


# =====================================================================
# LÓGICA DEL JUEGO SIMON SAYS
# =====================================================================

class JuegoSimon:
    def __init__(self, leds, panel, tablero):
        self.leds = leds
        self.panel = panel
        self.tablero = tablero

        self.estado = "ESPERANDO_INICIO"
        self.tablero.mostrar_guiones()

    def iniciar_juego_nuevo(self, ahora):
        self.nivel = 1
        self.vidas = VIDAS
        self.secuencia = [random.getrandbits(2)]

        #self.turno = turno
        #self.ultimo_turno = ahora

        self.t_inicio_partida = ahora
        self.tiempo_acumulado_s = 0
        self.cronometro_activo = True

        self.iniciar_presentacion(ahora)

    def obtener_tiempo_actual(self, ahora):
        if self.cronometro_activo:
            self.tiempo_acumulado_s = ticks_diff(ahora, self.t_inicio_partida) // 1000
        return self.tiempo_acumulado_s

    def frecuencia(self):
        return 1.0 + 0.5 * ((self.nivel - 1) // 2)

    def iniciar_presentacion(self, ahora):
        self.estado = "PRESENTACION"
        self.periodo_ms = int(1000 / self.frecuencia())
        self.t_encendido = int(self.periodo_ms * DUTY)
        self.paso = 0
        self.led_encendido = True
        self.t_paso = ahora

        self.tablero.mostrar(self.nivel, self.vidas, self.obtener_tiempo_actual(ahora))
        self.leds.encender(self.secuencia[0])

    def iniciar_turno_jugador(self, ahora):
        self.estado = "TURNO_JUGADOR"
        self.paso_jugador = 0
        self.t_retroalimentacion = 0
        self.led_presionado = None

    def procesar_error(self, ahora):
        self.vidas -= 1
        self.leds.apagar_todos()

        if self.vidas <= 0:
            print("--- GAME OVER ---")
            self.cronometro_activo = False
            self.estado = "FIN_JUEGO"
        else:
            print(f"¡Incorrecto! Vidas restantes: {self.vidas}")
            self.estado = "PAUSA"
            self.t_paso = ahora

    def actualizar(self, ahora):
        evt_ctrl = self.panel.leer_control(ahora)

        if evt_ctrl == "LARGO":
            print("\n--> Reinicio Manual (> 2s)")
            self.leds.apagar_todos()
            self.iniciar_juego_nuevo(ahora)
            return

        elif evt_ctrl == "CORTO" and self.estado == "ESPERANDO_INICIO":
            print("\n--> ¡Juego Iniciado!")
            self.iniciar_juego_nuevo(ahora)
            return

        if self.estado == "ESPERANDO_INICIO":
            return

        t_actual = self.obtener_tiempo_actual(ahora)
        self.tablero.mostrar(self.nivel, self.vidas, t_actual)

        # 1. PRESENTACIÓN DE LA SECUENCIA
        if self.estado == "PRESENTACION":
            transcurrido = ticks_diff(ahora, self.t_paso)

            if self.led_encendido and transcurrido >= self.t_encendido:
                self.leds.apagar(self.secuencia[self.paso])
                self.led_encendido = False

            elif not self.led_encendido and transcurrido >= self.periodo_ms:
                self.paso += 1
                if self.paso >= self.nivel:
                    self.iniciar_turno_jugador(ahora)
                else:
                    self.t_paso = ahora
                    self.led_encendido = True
                    self.leds.encender(self.secuencia[self.paso])

        # 2. TURNO DEL JUGADOR
        elif self.estado == "TURNO_JUGADOR":
            
            #while (self.turno > 0):
            
            if self.led_presionado is not None and ticks_diff(ahora, self.t_retroalimentacion) >= 150:
                self.leds.apagar(self.led_presionado)
                self.led_presionado = None

            btn = self.panel.leer_juego(ahora)
            if btn is not None:
                self.leds.encender(btn)
                self.led_presionado = btn
                self.t_retroalimentacion = ahora

                if btn == self.secuencia[self.paso_jugador]:
                    self.paso_jugador += 1

                    if self.paso_jugador >= self.nivel:
                        if self.nivel < NIVEL_MAX:
                            print(f"¡Nivel {self.nivel} superado!")
                            self.nivel += 1
                            self.secuencia.append(random.getrandbits(2))
                            self.estado = "PAUSA"
                            self.t_paso = ahora
                        else:
                            print("¡VICTORIA TOTAL!")
                            self.cronometro_activo = False
                            self.estado = "VICTORIA"
                else:
                    self.procesar_error(ahora)
            
                




        # 3. PAUSA ENTRE NIVELES
        elif self.estado == "PAUSA":
            if ticks_diff(ahora, self.t_paso) >= PAUSA_MS:
                self.iniciar_presentacion(ahora)

        # 4. GAME OVER O VICTORIA
        elif self.estado in ("FIN_JUEGO", "VICTORIA"):
            pass


# =====================================================================
# BUCLE PRINCIPAL
# =====================================================================

def main():
    random.seed(ticks_us())

    leds = LEDS(LEDS_P)
    panel = Botones(BOTONES, BTN_CONTROL)
    tablero = Tablero(DIGITOS_PINES, SEGMENTOS_PINES)
    juego = JuegoSimon(leds, panel, tablero)

    print("Sistema Iniciado - Presiona el botón de control para comenzar.")

    while True:
        ahora = ticks_ms()
        juego.actualizar(ahora)
        tablero.refrescar()  # Refresco continuo del display 3461BS

main()




# =====================================================================
# BUCLE PRINCIPAL DE PRUEBA (SOLO DISPLAY DE 7 SEGMENTOS)
# =====================================================================

# def main():
#     tablero = Tablero(DIGITOS_PINES, SEGMENTOS_PINES)
# 
#     # Timer para refrescar el display continuamente en segundo plano
#     # Frecuencia de 200Hz = 5ms entre digitos (sin parpadeo perceptible)
#     timer_display = Timer(-1)  # Timer virtual en MicroPython
#     timer_display.init(
#         period=5, mode=Timer.PERIODIC, callback=lambda t: tablero.refrescar()
#     )
# 
#     print("\n--- PRUEBA DE DISPLAY 7 SEGMENTOS ---")
#     print("Soportados: Números (0-9), espacios ' ' y guiones '-'")
#     print("Para salir presiona Ctrl+C\n")
# 
#     try:
#         while True:
#             # Ahora input() funciona de forma nativa sin congelar el display
#             entrada = input("Ingresa valor (4 caracteres) > ")
# 
#             # Rellenar con espacios si el usuario ingresa menos de 4 caracteres
#             cadena = entrada
# 
#             # Convertir a digitos válidos para el diccionario NUMEROS
#             d1 = int(cadena[0]) if cadena[0].isdigit() else cadena[0]
#             d2 = int(cadena[1]) if cadena[1].isdigit() else cadena[1]
#             d3 = int(cadena[2]) if cadena[2].isdigit() else cadena[2]
#             d4 = int(cadena[3]) if cadena[3].isdigit() else cadena[3]
# 
#             tablero.driver.fijar_valores(d1, d2, d3, d4)
#             print(f"-> Mostrando en display: '{cadena}'\n")
# 
#     except KeyboardInterrupt:
#         # Detener el temporizador al salir del programa
#         timer_display.deinit()
#         print("\nPrueba finalizada.")
# 
# 
# main()
