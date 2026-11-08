# CleanBot: manejo con el teclado + PI de velocidad + odometría + detección de patinaje
# Raspberry Pi Pico (MicroPython) + TB6612FNG + encoders CH-N20-3 + MPU-6500
#
# Se puede usar de dos formas:
#   1) Con control_pc.py en el PC (flechas del teclado y gráfica). Recomendado.
#   2) Desde Thonny: escribir la letra y Enter (w, s, a, d, espacio, q, e, +, -).
#
# Comandos (una letra cada uno):
#   w adelante   s atrás   a girar izquierda   d girar derecha   espacio parar
#   q girar 90° a la izquierda   e girar 90° a la derecha
#   + más velocidad   - menos velocidad   r reiniciar trayectoria
#   c recalibrar la IMU (carro quieto)   . latido del PC (seguridad)
#   '@' a 'O': flechas mantenidas (las manda control_pc.py; se mueve solo mientras
#              la tecla está oprimida)
#
# Conexiones (GPIO -> pin físico):
#   Driver:     PWMA GP15 (20), AIN2 GP14 (19), AIN1 GP13 (17), STBY GP12 (16),
#               BIN1 GP11 (15), BIN2 GP10 (14), PWMB GP9 (12)
#   Encoder A:  C1 GP19 (25), C2 GP20 (26)     Encoder B: C1 GP6 (9), C2 GP7 (10)
#   IMU (i2c1): SDA GP26 (31), SCL GP27 (32), INT GP22 (29, no se usa aquí)
#   ESP32 (puente Wi-Fi, UART1): GP4 TX (6) -> RX de la ESP, GP5 RX (7) <- TX de la ESP
#
# Ejes del carro: A es la rueda izquierda y B la derecha; el eje Y de la IMU apunta
# hacia adelante y el Z hacia arriba (rumbo positivo = giro a la izquierda).

from machine import Pin, PWM, I2C, UART
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


# === DETECTOR INICIO (este bloque no usa hardware: se puede probar en el PC) ===
# Bits del estado
SIN_AVANCE = 1     # las ruedas giran pero el acelerómetro no ve el cambio de velocidad (solo aviso)
PATINA_GIRO = 2    # las ruedas dicen que gira, el giroscopio dice que no tanto
BLOQUEO_A = 4      # rueda A frenada: PWM alto y casi sin velocidad
BLOQUEO_B = 8

ANCHO_MM = 190.0          # distancia efectiva entre ruedas (medida con los recorridos)
G_MM_S2 = 9806.65         # 1 g en mm/s²

BLOQ_PWM = 70.0           # % de PWM a partir del cual se sospecha bloqueo
BLOQ_FRACCION = 0.25      # ... si la rueda va a menos de esta fracción de la meta
BLOQ_RPM_MIN = 8.0        # (o a menos de estas RPM)
BLOQ_TICKS = 6            # sostenido 6 x 50 ms = 0,3 s

GIRO_VENTANA = 4          # ticks (4 x 50 ms = 0,2 s) para comparar giros
GIRO_FALTA_MIN = math.radians(3)  # el giroscopio debe quedarse corto en más de 3°...
GIRO_FALTA_REL = 0.5              # ... y en más del 50 % de lo que dicen las ruedas

AV_TAU = 0.7              # s: qué tan rápido se acerca la velocidad de la IMU a la de las ruedas
AV_V_MIN = 60.0           # mm/s: por debajo no se juzga el avance
AV_E_MIN = 50.0           # mm/s: diferencia mínima para sospechar
AV_E_REL = 0.6            # ... y relativa a la velocidad de las ruedas
AV_TICKS = 3              # sostenido 3 ticks (0,15 s)
AV_FUGA = 1.0             # s: con el aviso puesto, la velocidad de la IMU se lleva a 0
RUEDAS_QUIETAS_TICKS = 5  # 0,25 s sin pulsos -> el carro está quieto (velocidad IMU = 0)
PARADO_TICKS = 10         # 0,5 s quieto y sin motores -> se recalibra el acelerómetro


class Detector:
    def __init__(self):
        self.estado = 0
        self.cuenta_a = self.cuenta_b = 0
        self.vent_enc = []        # giro según encoders en cada tick (rad)
        self.vent_gir = []        # giro según giroscopio en cada tick (rad)
        self.v_enc = 0.0          # velocidad hacia adelante según encoders (mm/s)
        self.v_imu = 0.0          # velocidad hacia adelante según acelerómetro (mm/s)
        self.w_enc = 0.0          # giro según encoders (rad/s)
        self.cuenta_av = 0
        self.cuenta_quietas = 0   # ticks seguidos sin pulsos en ninguna rueda
        self.cuenta_parado = 0    # ... y además sin motores

    def reiniciar(self):
        # Nuevo comando de movimiento: se borran los avisos y se empieza a juzgar de nuevo
        self.estado = 0
        self.cuenta_a = self.cuenta_b = self.cuenta_av = 0
        self.v_imu = self.v_enc

    def parado(self):
        return self.cuenta_parado >= PARADO_TICKS

    def _bloqueo(self, meta, rpm, pwm, cuenta):
        if abs(meta) >= 15 and abs(pwm) >= BLOQ_PWM:
            avance = rpm if meta > 0 else -rpm       # velocidad en el sentido pedido
            if avance < max(BLOQ_RPM_MIN, BLOQ_FRACCION * abs(meta)):
                return cuenta + 1
        return 0

    def paso(self, meta_a, meta_b, rpm_a, rpm_b, pwm_a, pwm_b,
             ds_a, ds_b, dth_gir, dv_acel, dt):
        """Un tick del lazo de control.
        ds_a, ds_b: avance de cada rueda hacia adelante (mm); dth_gir: giro medido por el
        giroscopio (rad); dv_acel: integral de la aceleración hacia adelante (mm/s).
        Devuelve True si en este tick se debe sumar el avance a la trayectoria."""
        # 1) Bloqueo de cada rueda
        self.cuenta_a = self._bloqueo(meta_a, rpm_a, pwm_a, self.cuenta_a)
        self.cuenta_b = self._bloqueo(meta_b, rpm_b, pwm_b, self.cuenta_b)
        if self.cuenta_a >= BLOQ_TICKS:
            self.estado |= BLOQUEO_A
        if self.cuenta_b >= BLOQ_TICKS:
            self.estado |= BLOQUEO_B

        # 2) Patinaje al girar: comparar el giro de las ruedas con el del giroscopio
        dth_enc = (ds_b - ds_a) / ANCHO_MM
        self.w_enc = dth_enc / dt
        self.vent_enc.append(dth_enc)
        self.vent_gir.append(dth_gir)
        if len(self.vent_enc) > GIRO_VENTANA:
            self.vent_enc.pop(0)
            self.vent_gir.pop(0)
        enc = sum(self.vent_enc)
        gir = sum(self.vent_gir)
        falta = abs(enc) - (gir if enc > 0 else -gir)   # giro que el giroscopio no vio
        if falta > max(GIRO_FALTA_MIN, GIRO_FALTA_REL * abs(enc)):
            self.estado |= PATINA_GIRO
        else:
            self.estado &= ~PATINA_GIRO

        # 3) Avance real: la velocidad de las ruedas contra la integral del acelerómetro.
        #    Es solo un aviso: no frena la trayectoria (el acelerómetro confunde inclinación
        #    con aceleración y en la mesa daba falsas alarmas).
        self.v_enc = (ds_a + ds_b) / 2 / dt
        if ds_a == 0 and ds_b == 0:
            self.cuenta_quietas += 1
        else:
            self.cuenta_quietas = 0
        if self.cuenta_quietas and meta_a == 0 and meta_b == 0:
            self.cuenta_parado += 1
        else:
            self.cuenta_parado = 0
        if self.cuenta_quietas >= RUEDAS_QUIETAS_TICKS:
            self.v_imu = 0.0                           # ruedas quietas: carro quieto
        else:
            self.v_imu += dv_acel
        dif = self.v_enc - self.v_imu
        dif_sentido = dif if self.v_enc >= 0 else -dif  # > 0: las ruedas van más rápido que el carro
        if not self.estado & SIN_AVANCE:
            self.v_imu += (self.v_enc - self.v_imu) * dt / AV_TAU
            if abs(self.v_enc) > AV_V_MIN and dif_sentido > max(AV_E_MIN, AV_E_REL * abs(self.v_enc)):
                self.cuenta_av += 1
            else:
                self.cuenta_av = 0
            if self.cuenta_av >= AV_TICKS:
                self.estado |= SIN_AVANCE
        else:
            self.v_imu -= self.v_imu * dt / AV_FUGA
            # Se quita el aviso si las ruedas paran o si el carro vuelve a seguirlas
            if abs(self.v_enc) < 5 or dif_sentido < 0.25 * abs(self.v_enc):
                self.estado &= ~SIN_AVANCE
                self.cuenta_av = 0
                self.v_imu = self.v_enc
        return not (self.estado & PATINA_GIRO)
# === DETECTOR FIN ===


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
        delta = -(p - self.pulsos_ant)   # IN1=1 cuenta hacia abajo
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


# --- Comunicación con el PC, por dos caminos a la vez y con el mismo texto ---
#   - USB (print / sys.stdin): con el cable, como control_pc.py COM8
#   - UART1 hacia la ESP32, que lo reenvía por Wi-Fi (UDP): control_pc.py wifi
# Para la Pico W solo cambian estas dos funciones: en vez del UART se usa un socket
# UDP en el puerto 8600 y el PC no nota la diferencia.
uart = UART(1, baudrate=115200, tx=Pin(4), rx=Pin(5), txbuf=2048, rxbuf=512)
entrada = select.poll()
entrada.register(sys.stdin, select.POLLIN)


def enviar(linea):
    print(linea)                  # USB (si nadie lo está leyendo, se descarta)
    uart.write(linea + '\n')      # ESP32 -> Wi-Fi


def comandos():
    """Entrega, una por una, las letras que llegaron: (letra, True si vino por la ESP32)."""
    while entrada.poll(0):
        yield sys.stdin.read(1), False
    n = uart.any()
    if n:
        for b in uart.read(n):
            yield chr(b), True


# --- IMU: acelerómetro y giroscopio leídos juntos (14 bytes desde 0x3B) ---
i2c = I2C(1, sda=Pin(26), scl=Pin(27), freq=100000)
IMU = 0x68
i2c.writeto_mem(IMU, 0x6B, b'\x00')   # despertar
time.sleep_ms(100)
i2c.writeto_mem(IMU, 0x1B, b'\x08')   # giroscopio ±500 °/s -> 65.5 por °/s
i2c.writeto_mem(IMU, 0x1A, b'\x03')   # filtro del giroscopio ~41 Hz
i2c.writeto_mem(IMU, 0x1C, b'\x00')   # acelerómetro ±2 g -> 16384 por g
i2c.writeto_mem(IMU, 0x1D, b'\x03')   # filtro del acelerómetro ~45 Hz (ACCEL_CONFIG2)
buf_imu = bytearray(14)


def leer_imu():
    """Devuelve (ax, ay, az) en g y (gx, gy, gz) en °/s, sin restar sesgos."""
    i2c.readfrom_mem_into(IMU, 0x3B, buf_imu)
    ax, ay, az, _, gx, gy, gz = struct.unpack('>hhhhhhh', buf_imu)
    return ax / 16384, ay / 16384, az / 16384, gx / 65.5, gy / 65.5, gz / 65.5


def calibrar():
    while True:
        s = [0.0] * 6
        for _ in range(300):
            for i, val in enumerate(leer_imu()):
                s[i] += val
            time.sleep_ms(3)
        s = [v / 300 for v in s]
        if abs(s[5]) < 3:
            return s           # ax, ay, az, gx, gy, gz promedio con el carro quieto
        enviar("El carro se movio durante la calibracion, repitiendo...")


stby = Pin(12, Pin.OUT, value=0)
motor_a = Motor(19, 20, 13, 14, 15)
motor_b = Motor(6, 7, 11, 10, 9)
det = Detector()

enviar("Calibrando IMU, no muevas el carro...")
sesgo_ax, sesgo_ay, az0, sesgo_gx, sesgo_gy, sesgo = calibrar()
enviar("Listo (sesgo giro %.2f grados/s, acel adelante %.3f g)" % (sesgo, sesgo_ay))
enviar("Datos: D,t_ms,x_mm,y_mm,rumbo,gz,ax,ay,rpm_a,rpm_b,pwm_a,pwm_b,vel,v_enc,v_imu,w_enc,estado")
enviar("Pasos: P,t_ms,dt,ds_a,ds_b,dth_gir,dv_acel,meta_a,meta_b,rpm_a,rpm_b,pwm_a,pwm_b,gx,gy,ay,az,estado")

vel = VEL_INICIAL
v = 0              # velocidad hacia adelante (RPM)
w = 0              # giro en el sitio (RPM, positivo = izquierda)
rumbo = 0.0        # ángulo acumulado del carro en grados (positivo = izquierda)
rumbo_ref = None   # rumbo a mantener al ir recto
meta_giro = None   # rumbo objetivo del giro de 90°
con_pc = False     # True cuando llega el latido de control_pc.py
ultimo_rx = time.ticks_ms()
t_imu = time.ticks_us()
t_ctrl = time.ticks_ms()
t_print = t_ctrl
rpm_a = rpm_b = 0.0
meta_a = meta_b = 0.0
gz = ax = ay = az = gx = gy = 0.0
suma_gx = suma_gy = suma_az = 0.0   # promedios por tick para el registro
dv_acel = 0.0      # integral de la aceleración hacia adelante desde el último tick (mm/s)
suma_ay = 0.0      # para recalibrar el acelerómetro cuando el carro está quieto
n_ay = 0
rumbo_tick = 0.0   # rumbo en el tick anterior
estado_envio = 0   # avisos ocurridos desde el último envío
x = y = 0.0        # posición estimada en mm (x = adelante al arrancar, y = izquierda)
t_inicio = time.ticks_ms()


def mover(nv, nw):
    global v, w, rumbo_ref, meta_giro
    v, w, meta_giro = nv, nw, None
    rumbo_ref = rumbo if (nv != 0 and nw == 0) else None
    if nv or nw:
        det.reiniciar()        # al parar se conservan los avisos para poder verlos


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
        for c, desde_esp in comandos():
            if desde_esp:
                con_pc = True          # todo lo que llega por Wi-Fi queda vigilado por el latido
            if c == '.':
                con_pc = True
                ultimo_rx = time.ticks_ms()
            elif '@' <= c <= 'O':
                # Estado de las teclas mantenidas, lo manda control_pc.py cada 50 ms:
                # bit 0 arriba, bit 1 abajo, bit 2 izquierda, bit 3 derecha.
                # Solo estos bytes y el '.' cuentan como latido: un byte suelto o basura
                # en el cable no mantiene vivo el carro.
                con_pc = True
                ultimo_rx = time.ticks_ms()
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
                det.reiniciar()
                meta_giro = rumbo + (90 if c == 'q' else -90)
            elif c == 'c' and v == 0 and w == 0:   # recalibrar la IMU (carro quieto)
                mover(0, 0)
                motor_a.frenar()
                motor_b.frenar()
                enviar("Calibrando IMU, no muevas el carro...")
                sesgo_ax, sesgo_ay, az0, sesgo_gx, sesgo_gy, sesgo = calibrar()
                enviar("Listo (sesgo giro %.2f grados/s, acel adelante %.3f g)" % (sesgo, sesgo_ay))
                x = y = rumbo = rumbo_tick = 0.0
                t_imu = time.ticks_us()
                ultimo_rx = time.ticks_ms()
            elif c == 'r':                         # reiniciar trayectoria y rumbo
                x = y = rumbo = rumbo_tick = 0.0
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

        # 3) IMU en cada vuelta: rumbo (giroscopio) y velocidad (acelerómetro)
        ax, ay, az, gx, gy, gz = leer_imu()
        ahora = time.ticks_us()
        dt_imu = time.ticks_diff(ahora, t_imu) / 1e6
        t_imu = ahora
        gz -= sesgo
        rumbo += gz * dt_imu
        dv_acel += (ay - sesgo_ay) * G_MM_S2 * dt_imu
        suma_ay += ay
        suma_gx += gx - sesgo_gx
        suma_gy += gy - sesgo_gy
        suma_az += az
        n_ay += 1

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
            meta_a = -v + giro
            meta_b = v + giro
            rpm_a = motor_a.control(meta_a, dt)
            rpm_b = motor_b.control(meta_b, dt)

            # Detección de patinaje / bloqueo (todo en el sentido "adelante del carro")
            ds_a = -motor_a.delta * MM_POR_PULSO
            ds_b = motor_b.delta * MM_POR_PULSO
            dth_gir = math.radians(rumbo - rumbo_tick)
            rumbo_tick = rumbo
            sumar = det.paso(-meta_a, meta_b, -rpm_a, rpm_b, -motor_a.salida, motor_b.salida,
                             ds_a, ds_b, dth_gir, dv_acel, dt)
            if n_ay:   # registro de cada tick (20 por segundo) para revisar el detector en el PC
                enviar("P,%d,%.4f,%.3f,%.3f,%.5f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.4f,%.4f,%d" %
                      (time.ticks_diff(t_ctrl, t_inicio), dt, ds_a, ds_b, dth_gir, dv_acel,
                       -meta_a, meta_b, -rpm_a, rpm_b, -motor_a.salida, motor_b.salida,
                       suma_gx / n_ay, suma_gy / n_ay, suma_ay / n_ay, suma_az / n_ay, det.estado))
            dv_acel = 0.0
            suma_gx = suma_gy = suma_az = 0.0
            if det.estado & (BLOQUEO_A | BLOQUEO_B) and (v or w or meta_giro is not None):
                mover(0, 0)                           # rueda frenada: parar los motores
            if det.parado() and abs(gz) < 2 and n_ay:
                sesgo_ay += 0.1 * (suma_ay / n_ay - sesgo_ay)   # recalibrar estando quieto
            suma_ay = 0.0
            n_ay = 0

            # Odometría: cuánto avanzó el centro del carro y en qué dirección
            d = (ds_a + ds_b) / 2 if sumar else 0.0
            th = math.radians(rumbo)
            x += d * math.cos(th)
            y += d * math.sin(th)
            estado_envio |= det.estado

        # 5) Mandar los datos (control_pc.py los grafica y los guarda)
        if time.ticks_diff(time.ticks_ms(), t_print) >= DATOS_MS:
            t_print = time.ticks_ms()
            enviar("D,%d,%.1f,%.1f,%.2f,%.2f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%d,%.1f,%.1f,%.1f,%d" %
                  (time.ticks_diff(t_print, t_inicio), x, y, rumbo, gz, ax, ay,
                   rpm_a, rpm_b, motor_a.salida, motor_b.salida, vel,
                   det.v_enc, det.v_imu, math.degrees(det.w_enc), estado_envio | det.estado))
            estado_envio = 0
        time.sleep_ms(1)
except KeyboardInterrupt:
    pass
finally:
    motor_a.frenar()
    motor_b.frenar()
    time.sleep_ms(150)
    stby.value(0)
    enviar("Motores detenidos.")
