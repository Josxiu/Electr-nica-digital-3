"""

@file juego_prac2.py
@brief Implementacion de un juego de memoria con leds y botones usando una RaspBerry pi pico.
@details El sistema implementa la logica del juego utilizando:
            - Multiplexacion no bloqueante para un display de 7 segmentos de 4 ditgitos (3461BS).
            - Antirrebote (debounce) por seofware para lectura de botones de juego y control.
            - Maquinas de estados finitos (FSM) no bloqueantes basada en temporizadores de tiempo transcurrido (ticks_ms).
            - Indicador de pulso progresivo (parpadeo) mediante LED de tiempo restante.

@author Camilo Andres Medina Herrera, Juan José Vásquez Urrutia
@date 2026-09-14

"""


from machine import Pin         # Controloa los pines de entrada/salida (GPIO)
from time import ticks_ms, ticks_us, ticks_diff     # Medir el tiempo transcurrido
import random

from machine import Timer


# CONFIGURACIÓN DE PINES


LEDS_P      = (2, 3, 4, 5)							##< Pines del GPIO asignados a los leds del juego.
LED_TIEMPO 	=  1									##< Pin para el led del parpadeo.
BOTONES     = (6, 7, 8, 9)     						##< Pines del GPIO asignados a los botones del juego.
BTN_CONTROL = 10                					##< Pin para el boton de control.

DIGITOS_PINES   = (11, 12, 13, 14)					##< Pines asignados a los anodos de los digitos, en el siguiente orden (D1, D2, D3, D4).

SEGMENTOS_PINES = (15, 16, 17, 18, 19, 20, 21, 22)	##< Pines para los catodos, con el siguiente orden (A, B, C, D, E, F, G, DP).


NIVEL_MAX = 9										##< Cantidad maxima de niveles que tiene el juego.
VIDAS     = 3										##< Numero de vidas que tiene el jugador.
PAUSA_MS  = 1500    								##< Tiempo de espera para presentar la siguiente secuencia al pasar de nivel.
DUTY      = 0.7     								##< Ciclo de trabajo configurado para el encendido de los leds en la secuancia, 70% encendido y 30% apagado, con el fin de tener un pequeño tiempo entre el apagadu de un led y el encendido del siguient.
DEBOUNCE  = 50      								##< Tiempo para filtrado de antirrebote.
REINICIO  = 2000    								##< Tiempo necesario para forzar el reinicio del juego al mantener precionado el boton de control.

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
    ' ': [1, 1, 1, 1, 1, 1, 1, 1],  
    '-': [1, 1, 1, 1, 1, 1, 0, 1],  
}    												##< Diccionario con la confoguracion de los catodos para cada numero.


# =====================================================================
# CLASES DE HARDWARE
# =====================================================================

class Segmentos:
    
    """
    @brief Controlador de bajo nivel para multiplexado de display de 7 segmentos.
    @details Gestiona la comunicacion rapida de digitos y actualizacion de segmentos evitando el fantasma visual (ghosting).
    """
    
    def __init__(self, DIGITOS_PINES, SEGMENTOS_PINES):
        """
        @brief Contructor de la clase Segmentos.
        @param DIGITOS_PINES Tupla con los pines de habilitacion de digitos (Anodos)
        @param SEGMENTOS_PINES Tupla con los pines de controls de segmentos (Catodos)
        """
        self.digitos = [Pin(p, Pin.OUT, value=0) for p in DIGITOS_PINES]
        self.segmentos = [Pin(p, Pin.OUT, value=1) for p in SEGMENTOS_PINES]
        
        self.buffer = [' ', ' ', ' ', ' ']
        self.digito_actual = 0

    def fijar_valores(self, d1, d2, d3, d4):
        """
        @brief Actualiza el bufer de caracteres a desplegar en la pantalla.
        @param d1 Caracter para el digito 1 (Nivel).
        @param d2 Caracter para el digito 2 (Vidas).
        @param d3 Caracter para el digito 3 (Decenas del tiempo).
        @param d4 Caracter para el digito 4 (unidades del tiempo).
        """
        self.buffer[0] = d1
        self.buffer[1] = d2
        self.buffer[2] = d3
        self.buffer[3] = d4

    def refrescar(self):
        """
        @brief Conmuta rapidamente al siguiente digito en cada llamada para mantener el multiplexado.
        @note Debe ser invocada continuamente dentro del bucle principal o por temporizador.
        """
        # Apagar dígito anterior (Ánodo LOW) para evitar fantasmas (ghosting)
        self.digitos[self.digito_actual].value(0)

        # Seleccionar el siguiente dígito
        self.digito_actual = (self.digito_actual + 1) % 4
        val = self.buffer[self.digito_actual]
        patron = NUMEROS.get(val, NUMEROS[' '])

        # Aplicar patrón a los Cátodos (0=Encendido, 1=Apagado)
        for i in range(8):
            self.segmentos[i].value(patron[i])
        self.segmentos[7].value(1)
        
        # Encender el dígito activo (Ánodo HIGH)
        self.digitos[self.digito_actual].value(1)


class LEDS:
    """
    @brief Controlador para la gestion de los LEDs del juego y LED ded tiempo.
    """
    def __init__(self, pines, pin_tiempo):
        """
        @brief Contructor de la clase LEDs.
        @param pines Tuplas con los numeros de GPIO asignados a los LEDs.
        @param pin_tiempo Numero de GPIO asignado aal LED indicador de tiempo.
        """
        self.leds = [Pin(p, Pin.OUT, value=0) for p in pines]
        self.led_tiempo = Pin(pin_tiempo, Pin.OUT, value=0)

    def encender(self, i):
        """
        @brief Encender un LED de juego especifico.
        @param i Indice del LED (0 a 3).
        """
        self.leds[i].value(1)

    def apagar(self, i):
        """
        @brief Apaga un LED de juego especifico.
        @param i Indice del LED (0 a 3).
        """
        self.leds[i].value(0)

    def apagar_todos(self):
        """
        @brief Apaga todos los LEDs de secuencia y el LED indicador ed tiempo.
        """
        for led in self.leds:
            led.value(0)
        self.led_tiempo.value(0)

    def encender_todos(self):
        """
        @brief Enciende todos los LEDs de secuencia simultaneamente.
        """
        for led in self.leds:
            led.value(1)
            
    def led_tiempo(self, valor):
        """
        @brief Controla el estado digital del LED de tiempo (parpadeo).
        @param valor estadp digital (1 para encender, 0 para apagar).
        """
        self.led_tiempo.value(valor)


class Botones:
    """
    @brief Controlador no bloqueante para la lectura e interpetacion de pulsadores.
    @details Aplica logica de antirrebote por software y discrimina entre pulsacion corta y larga.
    """
    def __init__(self, pines_juego, pin_control):
        """
        @brief Contructor de la clase Botones.
        @param pines_juego Tupla de GPIOs asignados a los botones del juego.
        @param pin_control GPIO asignado al boton de control.
        """
        self.botones_juego = [Pin(p, Pin.IN, Pin.PULL_UP) for p in pines_juego]
        self.btn_control = Pin(pin_control, Pin.IN, Pin.PULL_UP)

        self.estado_juego = [0] * len(pines_juego)
        self.tiempo_juego = [0] * len(pines_juego)

        self.estado_ctrl = 1
        self.t_cambio_ctrl = 0
        self.t_presionado_ctrl = 0
        self.procesado_largo_ctrl = False

    def leer_juego(self, ahora):
        """
        @brief Lee los botones de juego detectando eventos de presion tras filtrar el rebote.
        @param ahora Marca de tiempo actual en milisegundos ('ticks_ms').
        @return int Indice del boton presionado (0 a 3), o 'None' si no hay evento.
        """
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
        """
        @brief Evalua el estado del boton de control clasificando la accion realizada.
        @param ahora Marca de tiempo actual en milisegundos ('ticks_ms').
        @return str "CORTO" si fue una pulsacion rapida, "LARGO" si supero el umbral de reinicio, 0 'None'.
        """
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
    """
    @brief Interfaz entre la logica del juego y los display de 7 segmentos.
    """
    def __init__(self, DIGITOS_PINES, SEGMENTOS_PINES):
        """
        @brief Contructor de la clase Tablero.
        @param DIGITOS_PINES Tupla con GPIOs de los digitos, los 'd'.
        @param SEGMENTOS_PINES Tupla con GPIOs de los segmentos, desde 'a' hasta 'g'.
        """
        self.driver = Segmentos(DIGITOS_PINES, SEGMENTOS_PINES)

    def mostrar(self, nivel, vidas, tiempo_seg):
        """
        @brief Muestra en el display los datos del juego.
        @param nivel Nivel actual (Digito 1).
        @param vidas Vidas restantes (Digito 2).
        @param tiempo_seg Tiempo acumulado de juego (Digitos 3 y 4).
        """
        t_limitado = min(99, max(0, tiempo_seg))
        d1 = nivel % 10
        d2 = vidas % 10
        d3 = (t_limitado // 10) % 10
        d4 = t_limitado % 10
        self.driver.fijar_valores(d1, d2, d3, d4)

    def mostrar_guiones(self):
        """
        @brief Muestra giones en los display cuando se esperan para inicial el juego.
        """
        self.driver.fijar_valores('-', '-', '-', '-')

    def refrescar(self):
        """
        @brief Llama al multiplexado del driver de segmentos.
        """
        self.driver.refrescar()


# =====================================================================
# LÓGICA DEL JUEGO SIMON SAYS
# =====================================================================

class Juego:
    """
    @brief Maquina de Estados Finitos que controla la logica y flujo del juego.
    """
    def __init__(self, leds, panel, tablero):
        """
        @brief Contructor de la clase.
        @param leds Instancia del controlador de LEDs.
        @param panel Instanci del controlador de Borones.
        @param tablero Instancia del controlador de Tablero (Display).
        """
        self.leds = leds
        self.panel = panel
        self.tablero = tablero
        
        self.led_presionado = None
        self.t_retroalimentacion = 0
        
        self.estado = "ESPERANDO_INICIO"
        self.tablero.mostrar_guiones()

    def iniciar_juego_nuevo(self, ahora):
        """
        @brief Inicializa las varibles de estado para comenzar una nueva partida.
        @param ahora Marca de timepo actualo ('ticks_ms').
        """
        self.nivel = 1
        self.vidas = VIDAS
        self.secuencia = [random.getrandbits(2)]

        self.t_inicio_partida = ahora
        self.tiempo_acumulado_s = 0
        self.cronometro_activo = True

        self.iniciar_presentacion(ahora)

    def obtener_tiempo_actual(self, ahora):
        """
        @brief Calcula el tiempo transcurrido en segundos desde el inicio de la partida.
        @param ahora Marca de tiempo actual.
        @return int Tiempo acumulado en segundos.
        """
        if self.cronometro_activo:
            self.tiempo_acumulado_s = ticks_diff(ahora, self.t_inicio_partida) // 1000
        return self.tiempo_acumulado_s

    def frecuencia(self):
        """
        @brief Calcula la frecuencia de presentacion de la secuencia basada en el nivel.
        @return float Frecuencia en Hz.
        """
        return 1.0 + 0.5 * ((self.nivel - 1) // 2)

    def iniciar_presentacion(self, ahora):
        """
        @brief Prepara e inicia la reproduccion visual de la secuancia de LEDs.
        @param ahora Marca de tiempo de entrada al estado.
        """
        self.estado = "PRESENTACION"
        self.periodo_ms = int(1000 / self.frecuencia())
        self.t_encendido = int(self.periodo_ms * DUTY)
        self.paso = 0
        self.led_encendido = True
        self.t_paso = ahora
        
        self.t_inicio_presentacion = ahora

        self.tablero.mostrar(self.nivel, self.vidas, self.obtener_tiempo_actual(ahora))
        self.leds.encender(self.secuencia[0])

    def iniciar_turno_jugador(self, ahora):
        """
        @brief Prepra el estado para la entrada de secuancia por parte del jugador.
        @param ahora Marca de timempo de inicio del turno.
        """
        self.estado = "TURNO_JUGADOR"
        self.paso_jugador = 0
        self.t_retroalimentacion = 0
        self.led_presionado = None
        
        # Calcular  tiempo de presentacion y tiempo de turno
        self.t_presentacion = ticks_diff(ahora, self.t_inicio_presentacion)
        self.t_maximo = int(1.25*self.t_presentacion)
        self.t_inicio_turno = ahora
        
        self.t_palpito = ahora
        self.estado_palpito = False

    def gestionar_palpito(self, ahora):
        """
        @brief Modula la frecuencia de parpadeo del LED de tiempo de forma dinamica segun el avance del turno.
        @param ahora Marca de tiempo actual.
        """
        t_transcurrido = ticks_diff(ahora, self.t_inicio_turno)
        progreso = min(1.0, max(0.0, t_transcurrido/self.t_maximo))
        
        frec_actual = 2.0 + (10 * progreso)
        semi_periodo = int(1000 / (2*frec_actual))
        
        if ticks_diff(ahora, self.t_palpito) >= semi_periodo:
            self.t_palpito = ahora
            self.estado_palpito = not self.estado_palpito
            self.leds.led_tiempo(1 if self.estado_palpito else 0)

    def procesar_error(self, ahora):
        """
        @brief Administra la perdida de vidas o la transicion a Game Over.
        @param ahora Marca de tiempo actual.
        """
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

    def obtener_tiempo_limite(self):
        """
        @brief Retonar el tiempo limite del turno del jugador (1.25 veces el tiempo de  presentacion de la secuencia).
        @return ins Tiempo maximo.
        """
        if hasattr(self, 't_maximo'):
            return self.t_maximo
        return 5000

    def actualizar(self, ahora):
        """
        @brief Metodo de actualizacion principal de la maquina de estados y perifericos.
        @details Controla de manera no bloqueante los eventos temporizados, las entradas de control y la FSM.
        @param ahora Marca de tiempo actual.
        """
        if self.led_presionado is not None and ticks_diff(ahora, self.t_retroalimentacion) >= 150:
            self.leds.apagar(self.led_presionado)
            self.led_presionado = None
        
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
            
            if ticks_diff(ahora, self.t_inicio_turno) >= self.obtener_tiempo_limite():
                print("¡Tiempo Agotado!")
                self.procesar_error(ahora)
                return
            
            self.gestionar_palpito(ahora)
            
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
                        #self.leds.apagar_todos()
                        #self.led_presionado = None
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
    """
    @brief Punto de entrada principal del programa.
    @details Inicializa la semiila aleatoria, instancia los objetos de perfericos y ejecuta el buqle infinito.
    """
    random.seed(ticks_us())

    leds = LEDS(LEDS_P, LED_TIEMPO)
    panel = Botones(BOTONES, BTN_CONTROL)
    tablero = Tablero(DIGITOS_PINES, SEGMENTOS_PINES)
    juego = Juego(leds, panel, tablero)

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
