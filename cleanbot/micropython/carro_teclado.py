# CleanBot: manejo con el teclado + PI de velocidad + rumbo con el giroscopio
# Raspberry Pi Pico (MicroPython) + TB6612FNG + encoders CH-N20-3 + MPU-6500
#
# Se puede usar de dos formas:
#   1) Con control_pc.py en el PC (flechas del teclado). Recomendado.
#   2) Desde Thonny: escribir la letra y Enter (w, s, a, d, espacio, q, e, +, -).
#
# Comandos (una letra cada uno):
#   w adelante   s atrás   a girar izquierda   d girar derecha   espacio parar
#   q girar 90° a la izquierda   e girar 90° a la derecha
#   + más velocidad   - menos velocidad   r reiniciar trayectoria
#   . latido del PC (seguridad)
#   '@' a 'O': flechas mantenidas (las manda control_pc.py; se mueve solo mientras
#              la tecla está oprimida)
#
# Conexiones (GPIO -> pin físico):
#   Driver:     PWMA GP15 (20), AIN2 GP14 (19), AIN1 GP13 (17), STBY GP12 (16),
#               BIN1 GP11 (15), BIN2 GP10 (14), PWMB GP9 (12)
#   Encoder A:  C1 GP19 (25), C2 GP20 (26)     Encoder B: C1 GP6 (9), C2 GP7 (10)
#   IMU (i2c1): SDA GP26 (31), SCL GP27 (32), INT GP22 (29, no se usa aquí)

from machine import Pin, PWM, I2C
import math, select, struct, sys, time

# --- Motores ---
PULSOS_POR_VUELTA = 848
KP, KI = 0.12, 1.5
K_FF = 0.2            # % de PWM por RPM pedida (arranque más rápido)
INTEGRAL_MAX = 120.0
DT_MS = 50

# --- Trayectoria (odometría: encoders para la distancia, giroscopio para el rumbo) ---
RUEDA_DIAMETRO_MM = 43.0   # MEDIR la llanta y corregir este valor
MM_POR_PULSO = math.pi * RUEDA_DIAMETRO_MM / PULSOS_POR_VUELTA
DATOS_MS = 100             # cada cuánto se mandan los datos al PC

# --- Manejo ---
VEL_INICIAL = 60      # RPM de las ruedas al avanzar
VEL_MAX = 150
PASO_VEL = 20
VEL_GIRO = 40         # RPM de cada rueda al girar en el sitio
K_RUMBO = 1.5         # RPM de corrección por grado de desvío al ir recto
CORR_MAX = 25         # máxima corrección de rumbo en RPM
K_GIRO90 = 1.0        # RPM por grado que falta en el giro de 90°
GIRO90_MIN, GIRO90_MAX = 25, 50
TIEMPO_LATIDO_MS = 500  # sin noticias del PC en este tiempo -> parar

TABLA = (0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0)


class Motor:
    def __init__(self, c1, c2, in1, in2, pwm):
        self.c1 = Pin(c1, Pin.IN, Pin.PULL_UP)
        self.c2 = Pin(c2, Pin.IN, Pin.PULL_UP)
        self.in1 = Pin(in1, Pin.OUT, value=0)
        self.in2 = Pin(in2, Pin.OUT, value=0)
        self.pwm = PWM(Pin(pwm))
        self.pwm.freq(1000)
        self.pwm.duty_u16(0)
        self.pulsos = 0
        self.pulsos_ant = 0
        self.integral = 0.0
        self.salida = 0.0
        self.delta = 0
        self.estado = (self.c1.value() << 1) | self.c2.value()
        self.c1.irq(self._encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING, hard=True)
        self.c2.irq(self._encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING, hard=True)

    def _encoder(self, pin):
        nuevo = (self.c1.value() << 1) | self.c2.value()
        self.pulsos += TABLA[(self.estado << 2) | nuevo]
        self.estado = nuevo

    def aplicar(self, porcentaje):
        porcentaje = max(-100.0, min(100.0, porcentaje))
        self.in1.value(1 if porcentaje > 0 else 0)
        self.in2.value(1 if porcentaje < 0 else 0)
        self.pwm.duty_u16(int(abs(porcentaje) * 655.35))
        self.salida = porcentaje

    def frenar(self):
        self.in1.value(1)          # IN1 = IN2 = 1: freno corto del TB6612
        self.in2.value(1)
        self.pwm.duty_u16(0)
        self.integral = 0.0
        self.salida = 0.0

    def rpm(self, dt):
        p = self.pulsos
        delta = -(p - self.pulsos_ant)   # adelante (IN1=1) cuenta hacia abajo
        self.pulsos_ant = p
        self.delta = delta
        return delta / PULSOS_POR_VUELTA / dt * 60

    def control(self, meta, dt):
        actual = self.rpm(dt)
        if meta == 0:
            self.frenar()
            return actual
        error = meta - actual
        self.integral = max(-INTEGRAL_MAX, min(INTEGRAL_MAX, self.integral + error * dt))
        self.aplicar(K_FF * meta + KP * error + KI * self.integral)
        return actual


# --- IMU (solo el giro alrededor de Z) ---
i2c = I2C(1, sda=Pin(26), scl=Pin(27), freq=100000)
IMU = 0x68
i2c.writeto_mem(IMU, 0x6B, b'\x00')   # despertar
time.sleep_ms(100)
i2c.writeto_mem(IMU, 0x1B, b'\x08')   # giroscopio ±500 °/s -> 65.5 por °/s
i2c.writeto_mem(IMU, 0x1A, b'\x03')   # filtro pasa bajas ~41 Hz


def giro_z():
    return struct.unpack('>h', i2c.readfrom_mem(IMU, 0x47, 2))[0] / 65.5


def acel_xy():
    ax, ay = struct.unpack('>hh', i2c.readfrom_mem(IMU, 0x3B, 4))
    return ax / 16384, ay / 16384      # en g (escala ±2 g)


def calibrar():
    while True:
        s = 0.0
        for _ in range(300):
            s += giro_z()
            time.sleep_ms(3)
        b = s / 300
        if abs(b) < 3:
            return b
        print("El carro se movio durante la calibracion, repitiendo...")


stby = Pin(12, Pin.OUT, value=0)
motor_a = Motor(19, 20, 13, 14, 15)
motor_b = Motor(6, 7, 11, 10, 9)

print("Calibrando giroscopio, no muevas el carro...")
sesgo = calibrar()
print("Listo (sesgo %.2f grados/s)" % sesgo)
print("Datos: D,t_ms,x_mm,y_mm,rumbo_grados,gz_grados_s,ax_g,ay_g,rpm_a,rpm_b,pwm_a,pwm_b,vel")

entrada = select.poll()
entrada.register(sys.stdin, select.POLLIN)

vel = VEL_INICIAL
v = 0              # velocidad hacia adelante (RPM)
w = 0              # giro en el sitio (RPM, positivo = izquierda)
rumbo = 0.0        # ángulo acumulado del carro en grados (positivo = izquierda)
rumbo_ref = None   # rumbo a mantener al ir recto
meta_giro = None   # rumbo objetivo del giro de 90°
con_pc = False     # True cuando llega el latido de control_pc.py
ultimo_rx = time.ticks_ms()
t_giro = time.ticks_us()
t_ctrl = time.ticks_ms()
t_print = t_ctrl
rpm_a = rpm_b = 0.0
gz = 0.0           # velocidad de giro actual (°/s)
x = y = 0.0        # posición estimada en mm (x = adelante al arrancar, y = izquierda)
t_inicio = time.ticks_ms()


def mover(nv, nw):
    global v, w, rumbo_ref, meta_giro
    v, w, meta_giro = nv, nw, None
    rumbo_ref = rumbo if (nv != 0 and nw == 0) else None


teclas = 0         # flechas mantenidas (bits), ver mover_teclas()


def mover_teclas():
    adelante = (teclas & 1) - ((teclas >> 1) & 1)      # +1, 0 o -1
    izquierda = ((teclas >> 2) & 1) - ((teclas >> 3) & 1)
    if adelante and izquierda:
        # Flecha arriba/abajo + lado: curva (una rueda más rápida que la otra)
        mover(adelante * vel, izquierda * adelante * vel // 3)
    else:
        mover(adelante * vel, izquierda * VEL_GIRO)


stby.value(1)
try:
    while True:
        # 1) Leer comandos sin bloquear
        while entrada.poll(0):
            c = sys.stdin.read(1)
            ultimo_rx = time.ticks_ms()
            if c == '.':
                con_pc = True
            elif '@' <= c <= 'O':
                # Estado de las teclas mantenidas, lo manda control_pc.py cada 50 ms:
                # bit 0 arriba, bit 1 abajo, bit 2 izquierda, bit 3 derecha
                con_pc = True
                bits = ord(c) & 0x0F
                if bits != teclas:
                    teclas = bits
                    if bits or meta_giro is None:     # soltar todo no cancela un giro de 90°
                        mover_teclas()
            elif c == 'w':
                mover(vel, 0)
            elif c == 's':
                mover(-vel, 0)
            elif c == 'a':
                mover(0, VEL_GIRO)
            elif c == 'd':
                mover(0, -VEL_GIRO)
            elif c == ' ':
                mover(0, 0)
            elif c in 'qe':
                mover(0, 0)
                meta_giro = rumbo + (90 if c == 'q' else -90)
            elif c == 'r':                         # reiniciar trayectoria y rumbo
                x = y = rumbo = 0.0
                meta_giro = None
                if rumbo_ref is not None:
                    rumbo_ref = 0.0
            elif c in '+-':
                vel = max(PASO_VEL, min(VEL_MAX, vel + (PASO_VEL if c == '+' else -PASO_VEL)))
                if teclas:
                    mover_teclas()
                elif v != 0:
                    mover(vel if v > 0 else -vel, 0)

        # 2) Seguridad: si el PC deja de mandar el latido, parar
        if con_pc and time.ticks_diff(time.ticks_ms(), ultimo_rx) > TIEMPO_LATIDO_MS:
            mover(0, 0)

        # 3) Integrar el giroscopio para saber hacia dónde mira el carro
        ahora = time.ticks_us()
        gz = giro_z() - sesgo
        rumbo += gz * time.ticks_diff(ahora, t_giro) / 1e6
        t_giro = ahora

        # 4) Lazo de control cada DT_MS
        if time.ticks_diff(time.ticks_ms(), t_ctrl) >= DT_MS:
            dt = time.ticks_diff(time.ticks_ms(), t_ctrl) / 1000
            t_ctrl = time.ticks_ms()
            giro = w
            if rumbo_ref is not None:                 # ir recto con el giroscopio
                giro = max(-CORR_MAX, min(CORR_MAX, K_RUMBO * (rumbo_ref - rumbo)))
            if meta_giro is not None:                 # giro de 90°
                falta = meta_giro - rumbo
                if abs(falta) < 2:
                    meta_giro = None
                    giro = 0
                else:
                    giro = max(GIRO90_MIN, min(GIRO90_MAX, K_GIRO90 * abs(falta)))
                    giro = giro if falta > 0 else -giro
            # Los motores están en espejo: para avanzar, A gira hacia atrás y B hacia adelante
            rpm_a = motor_a.control(-v + giro, dt)
            rpm_b = motor_b.control(v + giro, dt)
            # Odometría: cuánto avanzó el centro del carro y en qué dirección
            d = (-motor_a.delta + motor_b.delta) / 2 * MM_POR_PULSO
            th = math.radians(rumbo)
            x += d * math.cos(th)
            y += d * math.sin(th)

        # 5) Mandar los datos (control_pc.py los grafica y los guarda)
        if time.ticks_diff(time.ticks_ms(), t_print) >= DATOS_MS:
            t_print = time.ticks_ms()
            ax, ay = acel_xy()
            print("D,%d,%.1f,%.1f,%.2f,%.2f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%d" %
                  (time.ticks_diff(t_print, t_inicio), x, y, rumbo, gz, ax, ay,
                   rpm_a, rpm_b, motor_a.salida, motor_b.salida, vel))
        time.sleep_ms(2)
except KeyboardInterrupt:
    pass
finally:
    motor_a.frenar()
    motor_b.frenar()
    time.sleep_ms(150)
    stby.value(0)
    print("Motores detenidos.")
