/**
 * @file main.c
 * @brief Pulso de memoria (Practica 3) - version C sobre el Raspberry Pi Pico SDK.
 *
 * Este archivo contiene la LOGICA DEL JUEGO y la COORDINACION general.
 * El acceso a los dispositivos vive en modulos aparte:
 *
 *   tablero.h / .c   -> los cuatro digitos de 7 segmentos multiplexados
 *   luces.h   / .c   -> los cinco LEDs, el eco y el palpito
 *   entradas.h / .c  -> los pulsadores con antirrebote
 *   pines.h          -> la asignacion de GPIO del montaje
 *   tiempo.h         -> la base de tiempo en milisegundos
 *
 * Nada bloquea: no hay una sola espera activa. Todo se resuelve comparando
 * marcas de tiempo en cada vuelta del bucle. Eso es lo que permite atender los
 * pulsadores en cualquier instante y, sobre todo, sostener el barrido de los
 * displays: con multiplexacion por software, una espera de mas de ~8 ms se ve
 * como parpadeo.
 *
 * Equivalencia con la practica 2: la maquina de estados, las reglas, los
 * nueve niveles, las tres vidas, el acumulado de tiempo y las seis senales
 * luminosas son los mismos. Lo que cambia es la realizacion, no el producto.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/rand.h"

#include "pines.h"
#include "tiempo.h"
#include "tablero.h"
#include "luces.h"
#include "entradas.h"
#include "sonido.h"

/* =====================================================================
 * 1. DISPOSITIVOS
 * ===================================================================== */

static tablero_t tablero;      /**< los cuatro digitos */
static luces_t   luces;        /**< los cinco LEDs */
static boton_t   btn[4];       /**< pulsadores del jugador */
static boton_t   btn_inicio;   /**< pulsador de inicio / reinicio */

/* =====================================================================
 * 2. PARAMETROS DEL JUEGO
 * ===================================================================== */

#define NIVEL_MAX       9       /**< ultimo nivel de la partida */
#define VIDAS_INICIALES 3       /**< vidas con que arranca el jugador */
#define T_RESET      2000u      /**< ms de pulsacion sostenida que reinicia */
#define T_PAUSA      1500u      /**< ms de pausa entre intentos sin senales */
#define PALPITO_LENTO  400u     /**< periodo del LED 5 al empezar a responder */
#define PALPITO_RAPIDO  90u     /**< periodo del LED 5 al vencerse el tiempo */

/**
 * Interruptor general de las senales luminosas.
 *
 * Poner en false deja la pausa entre intentos en silencio (util para probar
 * la logica del juego sin la realimentacion visual encima).
 */
#define SENALES true

/* =====================================================================
 * 3. SENALES DE REALIMENTACION
 * ===================================================================== */

/** Forma en que una senal usa los LEDs. */
typedef enum {
    S_PARPADEO,   /**< enciende y apaga una mascara de LEDs N veces */
    S_FIJO,       /**< enciende una mascara y la deja quieta */
    S_BARRIDO     /**< recorre los cuatro LEDs uno por uno, ida y vuelta */
} tipo_senal_t;

/**
 * @brief Una senal luminosa de realimentacion.
 *
 * Las senales son DATOS, no codigo: cada evento del juego es una instancia
 * distinta de esta estructura, asi que cambiar una senal es cambiar numeros.
 *
 * El enunciado exige senales claramente diferenciables para seis eventos. La
 * distincion se logra combinando tres dimensiones: cuales LEDs se usan, el
 * ritmo, y cuantos destellos. Nunca dos senales comparten a la vez el mismo
 * conjunto de LEDs y el mismo ritmo.
 */
typedef struct {
    tipo_senal_t tipo;
    uint8_t      mascara;       /**< bits 0-3 = LEDs 1-4; bit 4 (0x10) = LED de tiempo */
    uint32_t     periodo;       /**< ms de un ciclo (duracion total si S_FIJO; ms por paso si S_BARRIDO) */
    int          repeticiones;  /**< destellos, o recorridos completos en el barrido */
} senal_t;

/* =====================================================================
 * 4. ESTADO DE LA PARTIDA
 * ===================================================================== */

/** Estados de la maquina principal. */
typedef enum {
    ESPERA,        /**< esperando el pulsador de inicio */
    PRESENTACION,  /**< mostrando la secuencia (pulsadores 1-4 desactivados) */
    INGRESO,       /**< el jugador responde, con el LED 5 palpitando */
    PAUSA,         /**< intervalo entre intentos */
    FIN            /**< partida terminada; el tablero queda congelado */
} estado_t;

static estado_t estado = ESPERA;

static int secuencia[NIVEL_MAX];            /**< indices de LED 0..3 */
static int nivel = 1;
static int vidas = VIDAS_INICIALES;

/* --- Presentacion --- */
static uint32_t periodo   = 1000;           /**< duracion de cada elemento, en ms */
static uint32_t encendido = 700;            /**< parte del periodo con el LED prendido */
static int      paso = 0;                   /**< cual elemento se esta mostrando */
static bool     led_encendido = false;
static uint32_t t_paso = 0;                 /**< ms en que empezo el elemento o la pausa */

/* --- Ingreso --- */
static int      indice_jugador = 0;         /**< cuantos elementos correctos lleva */
static uint32_t t_maximo = 0;               /**< ms permitidos para este intento */
static uint32_t t_inicio_ingreso = 0;       /**< ms en que empezo el turno */
static uint32_t tiempo_acum_ms = 0;         /**< suma de TODOS los intentos */

/* --- Control de flujo --- */
static bool subir_despues = false;          /**< al terminar la pausa: subir o repetir */
static bool ir_a_fin = false;               /**< la partida termino */
static bool reset_ya_hecho = false;         /**< evita reiniciar en bucle con INICIO sostenido */

/* --- Senal en curso --- */
static senal_t  senal_actual;
static senal_t  senal_siguiente;
static bool     hay_siguiente = false;      /**< hay una segunda fase encolada */
static bool     senal_terminada = true;     /**< ya acabo toda la cadena de senales */
static int      senal_paso = 0;
static bool     senal_on = false;
static uint32_t t_senal = 0;

/* --- Modos de prueba (se cambian por el monitor serie) --- */
static bool modo_tiempo = true;             /**< false = sin limite de tiempo */
static bool modo_vidas  = true;             /**< false = vidas infinitas */

/* =====================================================================
 * 5. LOGICA DEL JUEGO
 * ===================================================================== */

/**
 * @brief Duracion de cada elemento de la secuencia en el nivel n, en ms.
 * @param n numero de nivel, 1 a NIVEL_MAX.
 * @return Periodo de presentacion en milisegundos.
 *
 * La frecuencia arranca en 1,0 Hz y sube 0,5 Hz cada dos niveles:
 * 1-2 -> 1,0 | 3-4 -> 1,5 | 5-6 -> 2,0 | 7-8 -> 2,5 | 9 -> 3,0.
 *
 * Decision revisada respecto a la practica 2: alli se calculaba con coma
 * flotante (1000.0 / f). El RP2040 no tiene unidad de coma flotante, asi que
 * aqui se despeja en aritmetica entera. Como f = 1 + 0,5k con k = (n-1)/2,
 * el periodo es 2000/(2+k), que da exactamente los mismos valores:
 * 1000, 666, 500, 400 y 333 ms. El requisito no cambia.
 */
static uint32_t periodo_de_nivel(int n)
{
    int k = (n - 1) / 2;
    return 2000u / (uint32_t)(2 + k);
}

/**
 * @brief Suma el tiempo de un intento al acumulado. Satura en 99 s.
 * @param ms duracion del intento en milisegundos.
 */
static void sumar_tiempo(uint32_t ms)
{
    tiempo_acum_ms += ms;
    if (tiempo_acum_ms > 99000u) {
        tiempo_acum_ms = 99000u;
    }
}

/** @brief Refleja el estado de la partida en los cuatro digitos. */
static void actualizar_tablero(void)
{
    tablero_mostrar(&tablero, nivel, vidas, (int)(tiempo_acum_ms / 1000u));
}

/**
 * @brief Arranca una senal desde cero, descartando lo que hubiera encolado.
 * @param s     senal a mostrar.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 */
static void lanzar_senal(senal_t s, uint32_t ahora)
{
    if (!SENALES) {
        return;
    }
    luces_apagar_todo(&luces);
    senal_actual    = s;
    senal_paso      = 0;
    senal_on        = false;
    t_senal         = ahora;
    hay_siguiente   = false;
    senal_terminada = false;
}

/**
 * @brief Programa una segunda senal que se mostrara al terminar la actual.
 * @param s senal de la segunda fase.
 */
static void encolar_senal(senal_t s)
{
    if (!SENALES) {
        return;
    }
    senal_siguiente = s;
    hay_siguiente   = true;
}

/**
 * @brief Avanza la senal en curso. No bloquea.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 * @return true cuando termino toda la cadena, incluida la segunda fase.
 */
static bool senal_actualizar(uint32_t ahora)
{
    if (senal_actual.tipo == S_FIJO) {
        if (senal_paso == 0) {                  /* primer paso: encender y quedarse */
            luces_mascara(&luces, senal_actual.mascara, true);
            senal_paso = 1;
            t_senal = ahora;
        }
        if (ahora - t_senal < senal_actual.periodo) {
            return false;
        }
    }
    else if (senal_actual.tipo == S_PARPADEO) {
        if (ahora - t_senal < senal_actual.periodo / 2u) {
            return false;
        }
        t_senal = ahora;
        senal_on = !senal_on;
        luces_mascara(&luces, senal_actual.mascara, senal_on);
        senal_paso++;
        /* cada destello son dos semiciclos: encender y apagar */
        if (senal_paso < senal_actual.repeticiones * 2) {
            return false;
        }
    }
    else {   /* S_BARRIDO */
        static const int orden[6] = { 0, 1, 2, 3, 2, 1 };  /* ida y vuelta sin repetir extremos */
        if (ahora - t_senal < senal_actual.periodo) {
            return false;
        }
        t_senal = ahora;
        luces_apagar_todo(&luces);
        luces_encender(&luces, orden[senal_paso % 6]);
        senal_paso++;
        if (senal_paso < senal_actual.repeticiones * 6) {
            return false;
        }
    }

    luces_apagar_todo(&luces);

    if (hay_siguiente) {                        /* pasar a la segunda fase */
        hay_siguiente = false;
        senal_actual  = senal_siguiente;
        senal_paso    = 0;
        senal_on      = false;
        t_senal       = ahora;
        return false;
    }
    return true;
}

/** @brief Secuencia correcta: los cuatro LEDs juntos, dos destellos rapidos. */
static senal_t senal_correcta(void)
{
    return (senal_t){ .tipo = S_PARPADEO, .mascara = M_LEDS, .periodo = 200, .repeticiones = 2 };
}

/**
 * @brief Entrada incorrecta: el LED que ERA el correcto, dos destellos lentos.
 * @param led indice 0 a 3 del LED esperado.
 */
static senal_t senal_incorrecta(int led)
{
    return (senal_t){ .tipo = S_PARPADEO, .mascara = (uint8_t)(1u << led), .periodo = 400, .repeticiones = 2 };
}

/** @brief Tiempo agotado: el LED 5 solo, tres destellos. */
static senal_t senal_tiempo_agotado(void)
{
    return (senal_t){ .tipo = S_PARPADEO, .mascara = M_TIEMPO, .periodo = 250, .repeticiones = 3 };
}

/**
 * @brief Perdida de una vida: los cinco LEDs, tantos destellos como vidas queden.
 * @param v vidas restantes.
 */
static senal_t senal_vida(int v)
{
    return (senal_t){ .tipo = S_PARPADEO, .mascara = M_TODO, .periodo = 250, .repeticiones = (v < 1 ? 1 : v) };
}

/** @brief Nivel 9 completado: barrido de los cuatro LEDs, dos recorridos. */
static senal_t senal_victoria(void)
{
    return (senal_t){ .tipo = S_BARRIDO, .mascara = M_LEDS, .periodo = 120, .repeticiones = 2 };
}

/** @brief Sin vidas: los cinco LEDs encendidos fijos un segundo. */
static senal_t senal_derrota(void)
{
    return (senal_t){ .tipo = S_FIJO, .mascara = M_TODO, .periodo = 1000, .repeticiones = 1 };
}

/**
 * @brief Prepara el nivel actual y arranca la presentacion de su secuencia.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 *
 * El tiempo maximo de respuesta es 1,25 veces el de presentacion; en entero,
 * multiplicar por 5 y dividir entre 4.
 */
static void iniciar_nivel(uint32_t ahora)
{
    periodo   = periodo_de_nivel(nivel);
    encendido = periodo * 7u / 10u;                         /* ciclo util del 70 % */
    t_maximo  = (uint32_t)nivel * periodo * 5u / 4u;        /* factor 1,25 */

    actualizar_tablero();

    printf("Nivel %d -> secuencia: ", nivel);
    for (int i = 0; i < nivel; i++) {
        printf("%d ", secuencia[i] + 1);
    }
    printf(" | Tpres %u ms | Tmax %u ms\n",
           (unsigned)((uint32_t)nivel * periodo), (unsigned)t_maximo);

    luces_apagar_todo(&luces);            /* incluye cancelar un eco pendiente */

    paso = 0;
    led_encendido = true;
    t_paso = ahora;
    luces_encender(&luces, secuencia[0]);
    estado = PRESENTACION;
}

/** @brief Sube de nivel conservando la secuencia y anadiendo un elemento al final. */
static void subir_nivel(void)
{
    if (nivel < NIVEL_MAX) {
        secuencia[nivel] = (int)(get_rand_32() % 4u);
        nivel++;
    } else {
        nivel = 1;
        secuencia[0] = (int)(get_rand_32() % 4u);
    }
}

/** @brief Deja el juego en espera con los valores iniciales. */
static void reiniciar_partida(void)
{
    nivel = 1;
    vidas = VIDAS_INICIALES;
    tiempo_acum_ms = 0;
    ir_a_fin = false;
    subir_despues = false;

    luces_apagar_todo(&luces);
    actualizar_tablero();
    estado = ESPERA;

    printf("\n-- En espera. Pulsa INICIO para comenzar. --\n");
}

/**
 * @brief Comienza una partida nueva.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 *
 * En la version Arduino habia que sembrar el generador con micros() en el
 * instante de la pulsacion. Aqui no hace falta: get_rand_32() del SDK toma su
 * entropia del hardware, asi que la secuencia cambia en cada partida sin
 * depender del momento del arranque.
 */
static void empezar_partida(uint32_t ahora)
{
    nivel = 1;
    vidas = VIDAS_INICIALES;
    tiempo_acum_ms = 0;
    ir_a_fin = false;
    subir_despues = false;
    secuencia[0] = (int)(get_rand_32() % 4u);

    printf("\n=== PARTIDA NUEVA ===\n");
    iniciar_nivel(ahora);
}

/**
 * @brief El jugador reprodujo toda la secuencia correctamente.
 * @param ahora marca de tiempo de esta vuelta, en ms.
 */
static void acierto(uint32_t ahora)
{
    sumar_tiempo(ahora - t_inicio_ingreso);
    luces_tiempo(&luces, false);

    printf("  == NIVEL SUPERADO | intento %u ms | acumulado %u s ==\n",
           (unsigned)(ahora - t_inicio_ingreso), (unsigned)(tiempo_acum_ms / 1000u));

    actualizar_tablero();

    if (nivel >= NIVEL_MAX) {
        printf("  *** NIVEL 9 COMPLETADO: PARTIDA GANADA ***\n");
        ir_a_fin = true;
        lanzar_senal(senal_victoria(), ahora);
    } else {
        subir_despues = true;
        lanzar_senal(senal_correcta(), ahora);
    }

    t_paso = ahora;
    estado = PAUSA;
}

/**
 * @brief El jugador se equivoco o se le acabo el tiempo.
 * @param ahora        marca de tiempo de esta vuelta, en ms.
 * @param por_tiempo   true si vencio el plazo; false si pulso el LED equivocado.
 * @param led_esperado indice del LED que era el correcto; se ignora si por_tiempo.
 *
 * Muestra DOS senales seguidas: primero la causa del fallo y luego la de
 * perdida de vida (o la de fin de partida si era la ultima).
 *
 * Si vencio el tiempo se suma el maximo completo; si fallo antes, lo que
 * alcanzo a usar. El enunciado incluye los intentos vencidos en el acumulado.
 */
static void fallo(uint32_t ahora, bool por_tiempo, int led_esperado)
{
    sumar_tiempo(por_tiempo ? t_maximo : (ahora - t_inicio_ingreso));
    luces_tiempo(&luces, false);

    /* Primera senal: la causa del fallo. */
    if (por_tiempo) {
        lanzar_senal(senal_tiempo_agotado(), ahora);
    } else {
        lanzar_senal(senal_incorrecta(led_esperado), ahora);
    }

    if (modo_vidas) {
        vidas--;
        printf("  == FALLASTE | vidas restantes: %d", vidas);
    } else {
        printf("  == FALLASTE (vidas infinitas)");
    }
    printf(" | acumulado %u s ==\n", (unsigned)(tiempo_acum_ms / 1000u));

    /* Segunda senal: perdida de vida, o fin de partida. */
    if (modo_vidas && vidas <= 0) {
        printf("  *** SIN VIDAS: FIN DE PARTIDA ***\n");
        ir_a_fin = true;
        encolar_senal(senal_derrota());
    } else if (modo_vidas) {
        encolar_senal(senal_vida(vidas));
    }

    actualizar_tablero();
    subir_despues = false;         /* se repite el mismo nivel con la misma secuencia */
    t_paso = ahora;
    estado = PAUSA;
}

/**
 * @brief Comandos de prueba por el monitor serie (USB CDC).
 *
 * Permiten verificar los requisitos por separado, sin que el limite de tiempo
 * o las vidas interrumpan la prueba de otra cosa.
 *   t -> activa/desactiva el limite de tiempo
 *   v -> activa/desactiva las vidas
 *   r -> reinicia la partida
 *   e -> imprime el estado actual
 *
 * getchar_timeout_us(0) no bloquea: devuelve PICO_ERROR_TIMEOUT si no hay
 * nada pendiente, de modo que sustituye al par Serial.available()/read().
 */
static void leer_comandos(void)
{
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        switch (c) {
        case 't':
            modo_tiempo = !modo_tiempo;
            printf("\n*** limite de tiempo: %s ***\n",
                   modo_tiempo ? "ACTIVADO" : "DESACTIVADO");
            if (!modo_tiempo) {
                luces_tiempo(&luces, false);
            }
            break;

        case 'v':
            modo_vidas = !modo_vidas;
            printf("\n*** vidas: %s ***\n", modo_vidas ? "ACTIVADAS" : "INFINITAS");
            break;

        case 'r':
            printf("\n*** reinicio manual ***\n");
            reiniciar_partida();
            break;

        case 'e':
            printf("\n*** nivel %d | vidas %d | acumulado %u s | modo tiempo %s | modo vidas %s ***\n",
                   nivel, vidas, (unsigned)(tiempo_acum_ms / 1000u),
                   modo_tiempo ? "ON" : "OFF", modo_vidas ? "ON" : "OFF");
            break;

        default:
            break;
        }
    }
}

/* =====================================================================
 * 6. COORDINACION
 * ===================================================================== */

int main(void)
{
    stdio_init_all();

    tablero_init(&tablero, PIN_SEGMENTO, PIN_COMUN);
    luces_init(&luces, PIN_LED, PIN_LED_TIEMPO);
    sonido_init();
    for (int i = 0; i < 4; i++) {
        boton_init(&btn[i], PIN_BOTON[i]);
    }
    boton_init(&btn_inicio, PIN_BOTON_INICIO);

    printf("\nPulso de memoria - Practica 3 (version C, Pico SDK)\n");
    printf("Comandos: t=limite tiempo  v=vidas  r=reiniciar  e=estado\n");

    reiniciar_partida();

    while (true) {
        /* Una sola marca de tiempo por vuelta: todos los modulos comparan
           contra el mismo instante. */
        uint32_t ahora = millis();

        /* El barrido de los displays va PRIMERO y sin condiciones: sostiene
           la ilusion de los cuatro digitos encendidos. Va antes de cualquier
           salto de iteracion. */
        tablero_refrescar(&tablero, ahora);

        /* --- Entradas y comandos: siempre, en todos los estados --- */
        leer_comandos();

        uint32_t entradas = entradas_muestrear();   /* instantanea unica */
        for (int i = 0; i < 4; i++) {
            boton_leer(&btn[i], entradas, ahora);
        }
        boton_leer(&btn_inicio, entradas, ahora);

        luces_actualizar(&luces, ahora);            /* apaga el eco cuando le toca */

        /* --- Reinicio por pulsacion sostenida: vale en cualquier estado --- */
        if (boton_sostenido(&btn_inicio, ahora, T_RESET) && !reset_ya_hecho) {
            reset_ya_hecho = true;
            boton_descartar(&btn_inicio);   /* que no cuente ademas como pulsacion corta */
            printf("\n-- Reinicio por pulsacion sostenida --\n");
            reiniciar_partida();
            continue;
        }
        if (!boton_esta_presionado(&btn_inicio)) {
            reset_ya_hecho = false;         /* listo para otro reset */
        }

        /* --- Maquina de estados --- */
        switch (estado) {

        case ESPERA:
            if (boton_presionado(&btn_inicio)) {
                empezar_partida(ahora);
            }
            break;

        case PRESENTACION: {
            /* Los pulsadores 1-4 no generan entradas aqui: no se consultan. */
            uint32_t transcurrido = ahora - t_paso;

            if (led_encendido && transcurrido >= encendido) {
                luces_apagar(&luces, secuencia[paso]);  /* separa elementos iguales */
                led_encendido = false;
            }
            else if (!led_encendido && transcurrido >= periodo) {
                paso++;
                if (paso >= nivel) {            /* termino: turno del jugador */
                    indice_jugador = 0;
                    t_inicio_ingreso = ahora;   /* arranca el cronometro de respuesta */
                    luces_palpito_reiniciar(&luces, ahora);

                    /* Descarta pulsaciones hechas DURANTE la presentacion. */
                    for (int i = 0; i < 4; i++) {
                        boton_descartar(&btn[i]);
                    }

                    printf("  tu turno...\n");
                    estado = INGRESO;
                } else {
                    t_paso = ahora;
                    led_encendido = true;
                    luces_encender(&luces, secuencia[paso]);
                }
            }
            break;
        }

        case INGRESO: {
            uint32_t t = ahora - t_inicio_ingreso;

            if (modo_tiempo) {
                /* El palpito se acelera de forma lineal conforme se agota el
                   plazo. En entero: per = LENTO - (LENTO-RAPIDO)*t/tmax. */
                uint32_t avance = (t > t_maximo) ? t_maximo : t;
                uint32_t per = PALPITO_LENTO
                             - (PALPITO_LENTO - PALPITO_RAPIDO) * avance / t_maximo;
                luces_palpito(&luces, per, ahora);

                if (t >= t_maximo) {
                    printf("  TIEMPO AGOTADO\n");
                    fallo(ahora, true, 0);
                    continue;
                }
            }

            for (int i = 0; i < 4; i++) {
                if (boton_presionado(&btn[i])) {
                    luces_eco(&luces, i, ahora);    /* confirma la entrada reconocida */

                    printf("  pulsaste %d", i + 1);

                    if (i == secuencia[indice_jugador]) {
                        indice_jugador++;
                        printf("  ok (%d/%d)\n", indice_jugador, nivel);
                        if (indice_jugador >= nivel) {
                            acierto(ahora);
                        }
                    } else {
                        printf("  MAL, era el %d\n", secuencia[indice_jugador] + 1);
                        fallo(ahora, false, secuencia[indice_jugador]);
                    }
                    break;                          /* solo un boton por vuelta */
                }
            }
            break;
        }

        case PAUSA:
            if (!senal_terminada) {
                /* Mientras haya senal, la pausa la marca la senal, no el reloj. */
                if (senal_actualizar(ahora)) {
                    senal_terminada = true;
                    luces_apagar_todo(&luces);
                    t_paso = ahora;             /* arranca el respiro final */
                }
            }
            else if (ahora - t_paso >= (SENALES ? 500u : T_PAUSA)) {
                if (ir_a_fin) {
                    ir_a_fin = false;
                    estado = FIN;
                    printf("-- Fin de partida. Manten INICIO 2 s para reiniciar. --\n");
                } else {
                    if (subir_despues) {
                        subir_nivel();          /* acierto -> nivel y elemento nuevos */
                    }
                    iniciar_nivel(ahora);       /* fallo -> mismo nivel, misma secuencia */
                }
            }
            break;

        case FIN:
            /* El tablero conserva nivel, vidas y tiempo alcanzados.
               Solo se sale con la pulsacion sostenida de 2 s. */
            boton_descartar(&btn_inicio);
            break;
        }
    }
}
