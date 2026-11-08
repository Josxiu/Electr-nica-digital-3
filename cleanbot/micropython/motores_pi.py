# Control de velocidad PI de los dos motores N20 (CleanBot)
# Raspberry Pi Pico (MicroPython) + TB6612FNG + encoders CH-N20-3
#
# Conexiones (GPIO -> pin físico):
#   Driver (lado izquierdo):  PWMA GP15 (20), AIN2 GP14 (19), AIN1 GP13 (17),
#                             STBY GP12 (16), BIN1 GP11 (15), BIN2 GP10 (14),
#                             PWMB GP9 (12)
#   Encoder A (derecha):      C1 amarillo GP19 (25), C2 verde GP20 (26)
#   Encoder B (izquierda):    C1 amarillo GP6 (9),   C2 verde GP7 (10)
#   Encoders: negro -> 3V3, azul -> GND.  VM del driver -> cargador de 5 V.

from machine import Pin, PWM
import time

# --- Parámetros medidos ---
PULSOS_POR_VUELTA = 848   # cuadratura completa (A y B, subida y bajada): 4 x 7 x 30,3
KP = 0.12                 # % de PWM por RPM de error
KI = 1.5                  # % de PWM por (RPM x s) de error acumulado
DT_MS = 100               # periodo del lazo
PWM_MAX = 100.0           # límite de la salida en %
INTEGRAL_MAX = 120.0      # anti-windup

# Velocidades deseadas en RPM (positivo = adelante, negativo = reversa)
META_A = 200.0
META_B = 200.0

# Tabla de cuadratura: (estado_anterior << 2 | estado_actual) -> +1, -1 o 0
TABLA = (0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0)


class Motor:
    def __init__(self, pin_c1, pin_c2, pin_in1, pin_in2, pin_pwm, signo=-1):
        self.c1 = Pin(pin_c1, Pin.IN, Pin.PULL_UP)
        self.c2 = Pin(pin_c2, Pin.IN, Pin.PULL_UP)
        self.in1 = Pin(pin_in1, Pin.OUT, value=0)
        self.in2 = Pin(pin_in2, Pin.OUT, value=0)
        self.pwm = PWM(Pin(pin_pwm))
        self.pwm.freq(1000)
        self.pwm.duty_u16(0)
        # Con IN1=1 ("adelante") el encoder cuenta hacia abajo: signo = -1
        # hace que adelante dé RPM positivas.
        self.signo = signo
        self.pulsos = 0
        self.estado = (self.c1.value() << 1) | self.c2.value()
        self.c1.irq(self._encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING, hard=True)
        self.c2.irq(self._encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING, hard=True)
        self.pulsos_ant = 0
        self.integral = 0.0

    def _encoder(self, pin):
        nuevo = (self.c1.value() << 1) | self.c2.value()
        self.pulsos += TABLA[(self.estado << 2) | nuevo]
        self.estado = nuevo

    def aplicar(self, porcentaje):
        porcentaje = max(-PWM_MAX, min(PWM_MAX, porcentaje))
        self.in1.value(1 if porcentaje > 0 else 0)
        self.in2.value(1 if porcentaje < 0 else 0)
        self.pwm.duty_u16(int(abs(porcentaje) / 100 * 65535))

    def parar(self):
        self.in1.value(0)
        self.in2.value(0)
        self.pwm.duty_u16(0)

    def rpm(self, dt):
        p = self.pulsos
        delta = (p - self.pulsos_ant) * self.signo
        self.pulsos_ant = p
        return delta / PULSOS_POR_VUELTA / dt * 60

    def control(self, meta, dt):
        actual = self.rpm(dt)
        error = meta - actual
        self.integral = max(-INTEGRAL_MAX, min(INTEGRAL_MAX, self.integral + error * dt))
        salida = KP * error + KI * self.integral
        self.aplicar(salida)
        return actual, max(-PWM_MAX, min(PWM_MAX, salida))


stby = Pin(12, Pin.OUT, value=0)
motor_a = Motor(19, 20, 13, 14, 15)
motor_b = Motor(6, 7, 11, 10, 9)
stby.value(1)   # activa el driver

print("PI -> meta A: %.0f RPM | meta B: %.0f RPM (Ctrl+C para parar)" % (META_A, META_B))
ultimo = time.ticks_ms()
try:
    while True:
        ahora = time.ticks_ms()
        if time.ticks_diff(ahora, ultimo) >= DT_MS:
            dt = time.ticks_diff(ahora, ultimo) / 1000
            ultimo = ahora
            rpm_a, pwm_a = motor_a.control(META_A, dt)
            rpm_b, pwm_b = motor_b.control(META_B, dt)
            print("A: %6.1f RPM (PWM %5.1f%%) | B: %6.1f RPM (PWM %5.1f%%)" % (rpm_a, pwm_a, rpm_b, pwm_b))
        time.sleep_ms(2)
except KeyboardInterrupt:
    pass
finally:
    motor_a.parar()
    motor_b.parar()
    stby.value(0)
    print("Motores detenidos.")
