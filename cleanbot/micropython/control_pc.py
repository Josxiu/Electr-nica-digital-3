# Control del CleanBot con el teclado + gráfica en vivo de la trayectoria (se corre en el PC)
#
# Uso (con Thonny cerrado):   python control_pc.py COM8   (o doble clic en manejar.bat)
#
# Manda carro_teclado.py a la RAM de la Pico (no lo guarda en la flash), lo arranca,
# abre una ventana con la trayectoria y las señales, y lee el teclado:
#   Flechas o W A S D (mantenidas)   el carro se mueve solo mientras están oprimidas
#   Arriba/abajo + izquierda/derecha curva
#   q / e                            girar 90° izq. / der. con el giroscopio
#   + / -                            más / menos velocidad
#   r                                reiniciar la trayectoria (el carro queda en 0, 0)
#   Esc (o cerrar la gráfica)        salir
# Las teclas cuentan cuando la ventana activa es la consola o la gráfica.
# Si este programa se cierra o se suelta el cable, la Pico para sola en 0,5 s.
#
# Cada recorrido se guarda en datos/recorrido_AAAAMMDD_HHMMSS.csv (y .png al salir).
#
# Requiere:  pip install pyserial matplotlib   (solo Windows: usa msvcrt y user32)

import ctypes
import math
import msvcrt
import os
import sys
import time

import matplotlib.pyplot as plt
import serial

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM8"
CARPETA = os.path.dirname(os.path.abspath(__file__))
CODIGO = os.path.join(CARPETA, "carro_teclado.py")
PERIODO = 0.05            # cada cuánto se manda el estado de las flechas (s)
VENTANA_S = 30            # segundos que se ven en las gráficas de tiempo

COLUMNAS = ["t_ms", "x_mm", "y_mm", "rumbo_grados", "gz_grados_s", "ax_g", "ay_g",
            "rpm_a", "rpm_b", "pwm_a", "pwm_b", "vel"]

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
TECLAS_UNA_VEZ = {ord('Q'): b'q', ord('E'): b'e', ord('R'): b'r',
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


def cargar_programa(pico):
    pico.write(b'\r\x03\x03')        # detener lo que esté corriendo
    time.sleep(0.2)
    pico.reset_input_buffer()
    pico.write(b'\x01')              # modo REPL crudo
    esperar(pico, b'raw REPL; CTRL-B to exit\r\n>')
    with open(CODIGO, 'rb') as f:
        codigo = f.read()
    for i in range(0, len(codigo), 256):
        pico.write(codigo[i:i + 256])
        time.sleep(0.01)
    pico.write(b'\x04')              # ejecutar
    esperar(pico, b'OK')


# ---------------------------------------------------------------- gráfica
class Grafica:
    def __init__(self):
        for k in list(plt.rcParams):            # quitar atajos de matplotlib (q cierra, s guarda...)
            if k.startswith('keymap.'):
                plt.rcParams[k] = []
        plt.ion()
        self.fig = plt.figure("CleanBot - trayectoria", figsize=(12, 7))
        g = self.fig.add_gridspec(3, 2, width_ratios=(1.3, 1))
        self.ax_t = self.fig.add_subplot(g[:, 0])
        self.ax_r = self.fig.add_subplot(g[0, 1])
        self.ax_g = self.fig.add_subplot(g[1, 1], sharex=self.ax_r)
        self.ax_v = self.fig.add_subplot(g[2, 1], sharex=self.ax_r)

        self.ax_t.set_title("Trayectoria estimada (encoders + giroscopio)")
        self.ax_t.set_xlabel("derecha (mm)")
        self.ax_t.set_ylabel("adelante (mm)")
        self.ax_t.set_aspect('equal')
        self.ax_t.grid(True, alpha=0.3)
        self.l_tray, = self.ax_t.plot([], [], '-', color='tab:blue', lw=2)
        self.l_flecha, = self.ax_t.plot([], [], '-', color='tab:red', lw=3)
        self.p_carro, = self.ax_t.plot([], [], 'o', color='tab:red')
        self.ax_t.plot([0], [0], 'k+', ms=12)

        self.ax_r.set_ylabel("rumbo (°)")
        self.ax_g.set_ylabel("giro z (°/s)")
        self.ax_v.set_ylabel("ruedas (RPM)")
        self.ax_v.set_xlabel("tiempo (s)")
        for a in (self.ax_r, self.ax_g, self.ax_v):
            a.grid(True, alpha=0.3)
        self.l_rumbo, = self.ax_r.plot([], [], color='tab:purple')
        self.l_gz, = self.ax_g.plot([], [], color='tab:orange')
        self.l_a, = self.ax_v.plot([], [], label='A (adelante +)')
        self.l_b, = self.ax_v.plot([], [], label='B (adelante +)')
        self.ax_v.legend(loc='upper left', fontsize=8)
        self.fig.tight_layout(rect=(0, 0, 1, 0.95))
        self.reiniciar()
        plt.show(block=False)

    def reiniciar(self):
        self.t, self.px, self.py, self.rumbo, self.gz, self.ra, self.rb = ([] for _ in range(7))

    def agregar(self, d):
        # Ejes de la gráfica: horizontal = derecha (-y), vertical = adelante (x)
        self.t.append(d[0] / 1000)
        self.px.append(-d[2])
        self.py.append(d[1])
        self.rumbo.append(d[3])
        self.gz.append(d[4])
        self.ra.append(-d[7])          # el motor A va en espejo: adelante del carro = RPM negativas
        self.rb.append(d[8])

    def dibujar(self, estado):
        if not self.t:
            return
        self.l_tray.set_data(self.px, self.py)
        x, y, th = self.px[-1], self.py[-1], math.radians(self.rumbo[-1])
        largo = 40
        self.l_flecha.set_data([x, x - largo * math.sin(th)], [y, y + largo * math.cos(th)])
        self.p_carro.set_data([x], [y])
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
                             (self.l_a, self.ra), (self.l_b, self.rb)):
            linea.set_data(t, datos[i0:])
        self.ax_r.set_xlim(max(0, t[-1] - VENTANA_S), max(VENTANA_S, t[-1]))
        for a in (self.ax_r, self.ax_g, self.ax_v):
            a.relim()
            a.autoscale_view(scalex=False)
        for a, minimo in ((self.ax_r, 10), (self.ax_g, 20), (self.ax_v, 40)):
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
    os.makedirs(os.path.join(CARPETA, "datos"), exist_ok=True)
    nombre = os.path.join(CARPETA, "datos", time.strftime("recorrido_%Y%m%d_%H%M%S"))
    csv = open(nombre + ".csv", "w")
    csv.write(",".join(COLUMNAS) + "\n")

    pico = serial.Serial(PUERTO, 115200, timeout=0.01)
    print("Cargando carro_teclado.py en la Pico...")
    cargar_programa(pico)
    graf = Grafica()
    print("Mantén las flechas (o WASD) para mover | q/e: 90 grados | +/-: velocidad | "
          "r: reiniciar trayectoria | Esc: salir")
    print("Guardando en", nombre + ".csv\n")
    consola = ventanas_consola()
    antes = set()              # teclas de una vez que ya estaban oprimidas
    ultimo_envio = ultimo_dibujo = 0
    linea = b''
    estado = ''
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
                pico.write(TECLAS_UNA_VEZ[vk])
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
                pico.write(bytes([0x40 | bits]))
            # Leer lo que manda la Pico
            for b in pico.read(pico.in_waiting or 1):
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
                    estado = ("vel %d RPM | x %.0f mm  y %.0f mm | rumbo %.1f° | A %.0f RPM  B %.0f RPM"
                              % (d[11], d[1], d[2], d[3], -d[7], d[8]))
                    print('\r' + estado + ('' if activa else '  (sin foco)') + ' ' * 8,
                          end='', flush=True)
                elif texto and not texto.startswith('Datos:'):
                    print('\n' + texto)
            # Redibujar unas 10 veces por segundo
            if time.time() - ultimo_dibujo > 0.1:
                ultimo_dibujo = time.time()
                graf.dibujar(estado + ('' if activa else '   (sin foco: teclas ignoradas)'))
            graf.fig.canvas.flush_events()
    finally:
        pico.write(b'@')
        time.sleep(0.1)
        pico.write(b'\x03')                          # Ctrl+C: la Pico frena y apaga el driver
        time.sleep(0.3)
        resto = pico.read(pico.in_waiting or 1).decode(errors='replace').replace('\x04', '')
        print('\n' + '\n'.join(l for l in resto.splitlines()
                               if l.strip('\r >') and not l.startswith('D,')))
        pico.write(b'\x02')                          # volver al REPL normal
        pico.close()
        csv.close()
        if plt.fignum_exists(graf.fig.number):
            graf.fig.savefig(nombre + ".png", dpi=120)
            print("Gráfica guardada en", nombre + ".png")
        print("Datos guardados en", nombre + ".csv")


if __name__ == '__main__':
    main()
