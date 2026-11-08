# Control del CleanBot con el teclado + gráfica en vivo de la trayectoria (se corre en el PC)
#
# Uso (con Thonny cerrado):
#   python control_pc.py COM8            por el cable USB (o doble clic en manejar.bat).
#                                        Manda carro_teclado.py a la RAM de la Pico y lo arranca.
#   python control_pc.py wifi            por Wi-Fi (o doble clic en manejar_wifi.bat), a través
#                                        de la ESP32 puente (o de una Pico W): cleanbot.local, UDP 8600.
#   python control_pc.py wifi 192.168.1.50   igual, pero con la IP de la ESP si el nombre no resuelve.
#   python control_pc.py instalar COM8   guarda carro_teclado.py en la flash de la Pico y un main.py
#                                        que lo arranca al encenderla (necesario para usarla por Wi-Fi).
#   python control_pc.py desinstalar COM8   borra ese main.py (la Pico deja de arrancarlo sola).
#
# Abre una ventana con la trayectoria y las señales, y lee el teclado:
#   Flechas o W A S D (mantenidas)   el carro se mueve solo mientras están oprimidas
#   Arriba/abajo + izquierda/derecha curva
#   q / e                            girar 90° izq. / der. con el giroscopio
#   + / -                            más / menos velocidad
#   r                                reiniciar la trayectoria (el carro queda en 0, 0)
#   c                                recalibrar la IMU (con el carro quieto; por Wi-Fi se hace al conectar)
#   Esc (o cerrar la gráfica)        salir
# Las teclas cuentan cuando la ventana activa es la consola o la gráfica.
# Si este programa se cierra, se suelta el cable o se cae el Wi-Fi, la Pico para sola en 0,5 s.
#
# La Pico avisa si las ruedas giran pero el carro no se mueve (en el aire, atascado),
# si patina al girar o si una rueda se bloquea; los avisos se marcan en la gráfica.
#
# Cada recorrido se guarda en datos/recorrido_AAAAMMDD_HHMMSS.csv (y .png al salir).
#
# Requiere:  pip install pyserial matplotlib   (solo Windows: usa msvcrt y user32)

import ctypes
import math
import msvcrt
import os
import socket
import sys
import time

import matplotlib.pyplot as plt
import serial

PUERTO_UDP = 8600        # el mismo de la ESP32 puente (y, después, de la Pico W)
CARPETA = os.path.dirname(os.path.abspath(__file__))
CODIGO = os.path.join(CARPETA, "carro_teclado.py")
PERIODO = 0.05            # cada cuánto se manda el estado de las flechas (s)
VENTANA_S = 30            # segundos que se ven en las gráficas de tiempo

COLUMNAS = ["t_ms", "x_mm", "y_mm", "rumbo_grados", "gz_grados_s", "ax_g", "ay_g",
            "rpm_a", "rpm_b", "pwm_a", "pwm_b", "vel",
            "v_ruedas_mm_s", "v_imu_mm_s", "giro_ruedas_grados_s", "estado"]

COLUMNAS_PASOS = ["t_ms", "dt_s", "ds_a_mm", "ds_b_mm", "dth_giro_rad", "dv_acel_mm_s",
                  "meta_a", "meta_b", "rpm_a", "rpm_b", "pwm_a", "pwm_b",
                  "gx_grados_s", "gy_grados_s", "ay_g", "az_g", "estado"]

# Bits del estado que manda la Pico (ver Detector en carro_teclado.py)
AVISOS = ((1, "SIN AVANCE (en el aire o atascado)"), (2, "PATINA AL GIRAR"),
          (4, "BLOQUEO RUEDA A"), (8, "BLOQUEO RUEDA B"))


def texto_avisos(estado):
    return " + ".join(n for b, n in AVISOS if estado & b)

# ---------------------------------------------------------------- teclado (Windows)
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
user32.GetForegroundWindow.restype = ctypes.c_void_p
user32.GetAncestor.restype = ctypes.c_void_p
user32.GetAncestor.argtypes = (ctypes.c_void_p, ctypes.c_uint)
user32.GetWindow.restype = ctypes.c_void_p
user32.GetWindow.argtypes = (ctypes.c_void_p, ctypes.c_uint)
user32.GetWindowThreadProcessId.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong))
kernel32.GetConsoleWindow.restype = ctypes.c_void_p

# (flecha, letra) para cada bit del estado que se manda a la Pico
TECLAS_BIT = ((0x26, ord('W')),   # bit 0 arriba
              (0x28, ord('S')),   # bit 1 abajo
              (0x25, ord('A')),   # bit 2 izquierda
              (0x27, ord('D')))   # bit 3 derecha
# Teclas de una sola pulsación: código virtual -> comando
TECLAS_UNA_VEZ = {ord('Q'): b'q', ord('E'): b'e', ord('R'): b'r', ord('C'): b'c',
                  0xBB: b'+', 0x6B: b'+',      # + del teclado y del teclado numérico
                  0xBD: b'-', 0x6D: b'-'}
VK_ESC = 0x1B


def oprimida(vk):
    return bool(user32.GetAsyncKeyState(vk) & 0x8000)


def ventanas_consola():
    h = kernel32.GetConsoleWindow()
    # En Windows Terminal la ventana visible es la "dueña" de la consola
    return {h, user32.GetWindow(h, 4), user32.GetAncestor(h, 3)} - {None}


def ventana_activa(consola):
    h = user32.GetForegroundWindow()
    if h in consola:
        return True
    pid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
    return pid.value == os.getpid()            # la ventana de la gráfica es de este programa


# ---------------------------------------------------------------- Pico
def esperar(pico, fin, limite=5):
    datos = b''
    t = time.time()
    while not datos.endswith(fin):
        if time.time() - t > limite:
            raise RuntimeError("La Pico no respondió (¿Thonny abierto?). Recibido: %r" % datos)
        datos += pico.read(1)
    return datos


def repl_crudo(pico):
    pico.write(b'\r\x03\x03')        # detener lo que esté corriendo
    time.sleep(0.2)
    pico.reset_input_buffer()
    pico.write(b'\x01')              # modo REPL crudo
    esperar(pico, b'raw REPL; CTRL-B to exit\r\n>')


def ejecutar(pico, codigo):
    """Corre una instrucción en el REPL crudo y avisa si la Pico respondió con error."""
    datos = codigo.encode()
    for i in range(0, len(datos), 256):
        pico.write(datos[i:i + 256])
        time.sleep(0.01)
    pico.write(b'\x04')
    resp = esperar(pico, b'\x04>', 10)
    cuerpo = resp[resp.index(b'OK') + 2:-2]
    _, _, error = cuerpo.partition(b'\x04')
    if error.strip():
        raise RuntimeError(error.decode(errors='replace'))


def instalar(puerto):
    """Guarda carro_teclado.py en la flash y un main.py que lo arranca al encender."""
    pico = serial.Serial(puerto, 115200, timeout=0.05)
    repl_crudo(pico)
    with open(CODIGO, 'rb') as f:
        codigo = f.read()
    ejecutar(pico, "f = open('carro_teclado.py', 'wb')")
    for i in range(0, len(codigo), 256):
        ejecutar(pico, "f.write(%r)" % codigo[i:i + 256])
    ejecutar(pico, "f.close()")
    ejecutar(pico, "f = open('main.py', 'w'); f.write('import carro_teclado\\n'); f.close()")
    print("Guardados carro_teclado.py (%d bytes) y main.py en la Pico." % len(codigo))
    pico.write(b'\x02')              # salir del REPL crudo
    time.sleep(0.1)
    pico.write(b'\x04')              # reinicio suave: arranca main.py
    print("La Pico se reinició y está calibrando la IMU: deja el carro quieto un momento.")
    pico.close()


def desinstalar(puerto):
    pico = serial.Serial(puerto, 115200, timeout=0.05)
    repl_crudo(pico)
    ejecutar(pico, "import os\ntry:\n os.remove('main.py')\nexcept OSError:\n pass")
    pico.write(b'\x02')
    pico.close()
    print("main.py borrado: la Pico ya no arranca el programa sola.")


def cargar_programa(pico):
    repl_crudo(pico)
    with open(CODIGO, 'rb') as f:
        codigo = f.read()
    for i in range(0, len(codigo), 256):
        pico.write(codigo[i:i + 256])
        time.sleep(0.01)
    pico.write(b'\x04')              # ejecutar
    esperar(pico, b'OK')


# ---------------------------------------------------------------- enlaces con el carro
class EnlaceUSB:
    """La Pico conectada con el cable: se le carga el programa en la RAM y se habla por serial."""

    def __init__(self, puerto):
        self.pico = serial.Serial(puerto, 115200, timeout=0.01)
        print("Cargando carro_teclado.py en la Pico...")
        cargar_programa(self.pico)

    def escribir(self, datos):
        self.pico.write(datos)

    def leer(self):
        return self.pico.read(self.pico.in_waiting or 1)

    def segundos_sin_datos(self):
        return 0.0

    def cerrar(self):
        self.pico.write(b'@')
        time.sleep(0.1)
        self.pico.write(b'\x03')                     # Ctrl+C: la Pico frena y apaga el driver
        time.sleep(0.3)
        resto = self.leer()
        self.pico.write(b'\x02')                     # volver al REPL normal
        time.sleep(0.1)
        self.pico.write(b'\x04')                     # reinicio suave: si está instalado, main.py
        self.pico.close()                            # vuelve a arrancar y queda listo para el Wi-Fi
        return resto


class EnlaceWifi:
    """Por Wi-Fi: paquetes UDP a la ESP32 puente (o a una Pico W) en el puerto 8600.
    El programa ya tiene que estar corriendo en la Pico (python control_pc.py instalar COM8)."""

    def __init__(self, destino):
        try:
            ip = socket.gethostbyname(destino)
        except OSError:
            raise SystemExit("No encuentro '%s'. ¿La ESP está encendida y en la misma red? "
                             "Prueba con su IP: python control_pc.py wifi 192.168.x.x" % destino)
        self.destino = (ip, PUERTO_UDP)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.ultimo_rx = time.time()
        print("Hablando con %s (%s), UDP %d" % (destino, ip, PUERTO_UDP))
        print("Recalibrando la IMU: deja el carro quieto un momento...")
        self.escribir(b'@c')                         # parar y recalibrar (también reinicia x, y)

    def escribir(self, datos):
        try:
            self.sock.sendto(datos, self.destino)
        except OSError:
            pass                                     # sin red por un momento: se reintenta solo

    def leer(self):
        datos = b''
        for _ in range(50):
            try:
                datos += self.sock.recv(2048)
            except BlockingIOError:
                break
            except ConnectionResetError:             # Windows avisa así de un paquete perdido
                continue
        if datos:
            self.ultimo_rx = time.time()
        else:
            time.sleep(0.005)
        return datos

    def segundos_sin_datos(self):
        return time.time() - self.ultimo_rx

    def cerrar(self):
        for _ in range(5):                           # parar (varias veces por si se pierde uno)
            self.escribir(b'@ ')
            time.sleep(0.05)
        resto = self.leer()
        self.sock.close()
        return resto


# ---------------------------------------------------------------- gráfica
class Grafica:
    def __init__(self):
        for k in list(plt.rcParams):            # quitar atajos de matplotlib (q cierra, s guarda...)
            if k.startswith('keymap.'):
                plt.rcParams[k] = []
        plt.ion()
        self.fig = plt.figure("CleanBot - trayectoria", figsize=(12, 8))
        g = self.fig.add_gridspec(4, 2, width_ratios=(1.3, 1))
        self.ax_t = self.fig.add_subplot(g[:, 0])
        self.ax_r = self.fig.add_subplot(g[0, 1])
        self.ax_g = self.fig.add_subplot(g[1, 1], sharex=self.ax_r)
        self.ax_a = self.fig.add_subplot(g[2, 1], sharex=self.ax_r)
        self.ax_v = self.fig.add_subplot(g[3, 1], sharex=self.ax_r)

        self.ax_t.set_title("Trayectoria estimada (encoders + giroscopio)")
        self.ax_t.set_xlabel("derecha (mm)")
        self.ax_t.set_ylabel("adelante (mm)")
        self.ax_t.set_aspect('equal')
        self.ax_t.grid(True, alpha=0.3)
        self.l_tray, = self.ax_t.plot([], [], '-', color='tab:blue', lw=2)
        self.l_flecha, = self.ax_t.plot([], [], '-', color='tab:red', lw=3)
        self.p_carro, = self.ax_t.plot([], [], 'o', color='tab:red')
        self.p_patina, = self.ax_t.plot([], [], 'x', color='tab:orange', ms=7,
                                        label='patina / sin avance')
        self.p_bloqueo, = self.ax_t.plot([], [], 's', color='k', ms=7, label='rueda bloqueada')
        self.ax_t.legend(loc='upper right', fontsize=8)
        self.ax_t.plot([0], [0], 'k+', ms=12)

        self.ax_r.set_ylabel("rumbo (°)")
        self.ax_g.set_ylabel("giro (°/s)")
        self.ax_a.set_ylabel("avance (mm/s)")
        self.ax_v.set_ylabel("ruedas (RPM)")
        self.ax_v.set_xlabel("tiempo (s)")
        for a in (self.ax_r, self.ax_g, self.ax_a, self.ax_v):
            a.grid(True, alpha=0.3)
        self.l_rumbo, = self.ax_r.plot([], [], color='tab:purple')
        self.l_gz, = self.ax_g.plot([], [], color='tab:orange', label='giroscopio')
        self.l_wenc, = self.ax_g.plot([], [], color='tab:gray', label='ruedas')
        self.ax_g.legend(loc='upper left', fontsize=8)
        self.l_venc, = self.ax_a.plot([], [], color='tab:gray', label='ruedas')
        self.l_vimu, = self.ax_a.plot([], [], color='tab:green', label='acelerómetro')
        self.ax_a.legend(loc='upper left', fontsize=8)
        self.l_a, = self.ax_v.plot([], [], label='A (adelante +)')
        self.l_b, = self.ax_v.plot([], [], label='B (adelante +)')
        self.ax_v.legend(loc='upper left', fontsize=8)
        self.fig.tight_layout(rect=(0, 0, 1, 0.95))
        self.reiniciar()
        plt.show(block=False)

    def reiniciar(self):
        (self.t, self.px, self.py, self.rumbo, self.gz, self.ra, self.rb,
         self.wenc, self.venc, self.vimu) = ([] for _ in range(10))
        self.patina = ([], [])
        self.bloqueo = ([], [])

    def agregar(self, d):
        # Ejes de la gráfica: horizontal = derecha (-y), vertical = adelante (x)
        self.t.append(d[0] / 1000)
        self.px.append(-d[2])
        self.py.append(d[1])
        self.rumbo.append(d[3])
        self.gz.append(d[4])
        self.ra.append(-d[7])          # el motor A va en espejo: adelante del carro = RPM negativas
        self.rb.append(d[8])
        self.venc.append(d[12])
        self.vimu.append(d[13])
        self.wenc.append(d[14])
        estado = int(d[15])
        if estado & 3:
            self.patina[0].append(-d[2])
            self.patina[1].append(d[1])
        if estado & 12:
            self.bloqueo[0].append(-d[2])
            self.bloqueo[1].append(d[1])

    def dibujar(self, estado):
        if not self.t:
            return
        self.l_tray.set_data(self.px, self.py)
        x, y, th = self.px[-1], self.py[-1], math.radians(self.rumbo[-1])
        largo = 40
        self.l_flecha.set_data([x, x - largo * math.sin(th)], [y, y + largo * math.cos(th)])
        self.p_carro.set_data([x], [y])
        self.p_patina.set_data(*self.patina)
        self.p_bloqueo.set_data(*self.bloqueo)
        xmin, xmax = min(self.px + [x]), max(self.px + [x])
        ymin, ymax = min(self.py), max(self.py)
        lado = max(400, xmax - xmin + 150, ymax - ymin + 150)
        cx, cy = (xmin + xmax) / 2, (ymin + ymax) / 2
        self.ax_t.set_xlim(cx - lado / 2, cx + lado / 2)
        self.ax_t.set_ylim(cy - lado / 2, cy + lado / 2)

        i0 = 0
        while self.t[-1] - self.t[i0] > VENTANA_S:
            i0 += 1
        t = self.t[i0:]
        for linea, datos in ((self.l_rumbo, self.rumbo), (self.l_gz, self.gz),
                             (self.l_wenc, self.wenc), (self.l_venc, self.venc),
                             (self.l_vimu, self.vimu), (self.l_a, self.ra), (self.l_b, self.rb)):
            linea.set_data(t, datos[i0:])
        self.ax_r.set_xlim(max(0, t[-1] - VENTANA_S), max(VENTANA_S, t[-1]))
        for a in (self.ax_r, self.ax_g, self.ax_a, self.ax_v):
            a.relim()
            a.autoscale_view(scalex=False)
        for a, minimo in ((self.ax_r, 10), (self.ax_g, 20), (self.ax_a, 60), (self.ax_v, 40)):
            y0, y1 = a.get_ylim()                    # que el ruido no se vea gigante
            if y1 - y0 < minimo:
                c = (y0 + y1) / 2
                a.set_ylim(c - minimo / 2, c + minimo / 2, auto=True)   # auto=True: seguir autoescalando
        self.fig.suptitle(estado, fontsize=10)
        self.fig.canvas.draw_idle()

    def abierta(self):
        return plt.fignum_exists(self.fig.number)


# ---------------------------------------------------------------- programa
def main():
    args = sys.argv[1:]
    modo = args[0].lower() if args else "com8"
    if modo == "instalar":
        instalar(args[1] if len(args) > 1 else "COM8")
        return
    if modo == "desinstalar":
        desinstalar(args[1] if len(args) > 1 else "COM8")
        return
    if modo == "wifi":
        pico = EnlaceWifi(args[1] if len(args) > 1 else "cleanbot.local")
    else:
        pico = EnlaceUSB(args[0] if args else "COM8")

    os.makedirs(os.path.join(CARPETA, "datos"), exist_ok=True)
    nombre = os.path.join(CARPETA, "datos", time.strftime("recorrido_%Y%m%d_%H%M%S"))
    csv = open(nombre + ".csv", "w")
    csv.write(",".join(COLUMNAS) + "\n")
    pasos = open(nombre + "_pasos.csv", "w")      # cada tick del control (20 por segundo)
    pasos.write(",".join(COLUMNAS_PASOS) + "\n")
    graf = Grafica()
    print("Mantén las flechas (o WASD) para mover | q/e: 90 grados | +/-: velocidad | "
          "r: reiniciar trayectoria | c: recalibrar | Esc: salir")
    print("Guardando en", nombre + ".csv\n")
    consola = ventanas_consola()
    antes = set()              # teclas de una vez que ya estaban oprimidas
    ultimo_envio = ultimo_dibujo = 0
    linea = b''
    estado = ''
    avisos_antes = ''
    try:
        while graf.abierta():
            activa = ventana_activa(consola)
            while msvcrt.kbhit():               # vaciar lo que se escriba en la consola
                msvcrt.getch()
            if activa and oprimida(VK_ESC):
                break
            # Teclas de una sola pulsación (se manda al oprimir, no mientras se mantiene)
            ahora_opr = {vk for vk in TECLAS_UNA_VEZ if activa and oprimida(vk)}
            for vk in ahora_opr - antes:
                pico.escribir(TECLAS_UNA_VEZ[vk])
                if TECLAS_UNA_VEZ[vk] == b'r':
                    graf.reiniciar()
            antes = ahora_opr
            # Estado de las flechas mantenidas (también sirve de latido de seguridad)
            if time.time() - ultimo_envio >= PERIODO:
                ultimo_envio = time.time()
                bits = 0
                if activa:
                    for i, (flecha, letra) in enumerate(TECLAS_BIT):
                        if oprimida(flecha) or oprimida(letra):
                            bits |= 1 << i
                pico.escribir(bytes([0x40 | bits]))
            # Leer lo que manda la Pico
            for b in pico.leer():
                if b != 10:
                    linea += bytes([b])
                    continue
                texto = linea.decode(errors='replace').strip('\r\x04>')
                linea = b''
                if texto.startswith('D,'):
                    try:
                        d = [float(v) for v in texto[2:].split(',')]
                    except ValueError:
                        continue
                    if len(d) != len(COLUMNAS):
                        continue
                    csv.write(texto[2:] + "\n")
                    graf.agregar(d)
                    avisos = texto_avisos(int(d[15]))
                    if avisos and avisos != avisos_antes:
                        print("\n[%.1f s] AVISO: %s" % (d[0] / 1000, avisos))
                    avisos_antes = avisos
                    estado = ("vel %d RPM | x %.0f mm  y %.0f mm | rumbo %.1f° | A %.0f RPM  B %.0f RPM"
                              % (d[11], d[1], d[2], d[3], -d[7], d[8]))
                    if avisos:
                        estado += " | " + avisos
                    print('\r' + estado + ('' if activa else '  (sin foco)') + ' ' * 8,
                          end='', flush=True)
                elif texto.startswith('Listo') or texto.startswith('Calibrando'):
                    print('\n' + texto)
                    if texto.startswith('Listo'):
                        graf.reiniciar()                 # tras calibrar, el carro vuelve a (0, 0)
                elif texto.startswith('P,'):
                    pasos.write(texto[2:] + "\n")
                elif texto and not texto.startswith(('Datos:', 'Pasos:')):
                    print('\n' + texto)
            # Redibujar unas 10 veces por segundo
            if time.time() - ultimo_dibujo > 0.1:
                ultimo_dibujo = time.time()
                sin_datos = pico.segundos_sin_datos()
                aviso = '' if activa else '   (sin foco: teclas ignoradas)'
                if sin_datos > 1:
                    aviso += '   (SIN DATOS DEL CARRO HACE %.0f s)' % sin_datos
                graf.dibujar(estado + aviso)
            graf.fig.canvas.flush_events()
    finally:
        resto = pico.cerrar().decode(errors='replace').replace('\x04', '')
        print('\n' + '\n'.join(l for l in resto.splitlines()
                               if l.strip('\r >') and not l.startswith(('D,', 'P,'))))
        csv.close()
        pasos.close()
        if plt.fignum_exists(graf.fig.number):
            graf.fig.savefig(nombre + ".png", dpi=120)
            print("Gráfica guardada en", nombre + ".png")
        print("Datos guardados en", nombre + ".csv")


if __name__ == '__main__':
    main()
