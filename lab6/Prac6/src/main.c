/**
 * @file main.c
 * @brief CalCamino: contador de pasos con la IMU, pantalla OLED y botones.
 *
 * Flujo polling + interrupciones:
 *   - ISR del pin INT (200 Hz): el chip tiene una muestra, se pide la lectura.
 *   - ISR del I2C de la IMU: la transferencia termino, guarda los bytes.
 *   - Timer cada PANTALLA_PERIODO_MS: marca que toca refrescar la pantalla.
 *   - ISR del I2C de la pantalla: envia el cuadro o los comandos y avisa
 *     cuando termina.
 *   - ISR del GPIO y alarma de los botones: antirrebote y evento corto o largo.
 *   - Timer cada BATERIA_PERIODO_MS e ISR del ADC: una medida de la bateria.
 *   - main: recoge la lectura, la pasa al contador de pasos, suma las calorias
 *     de los pasos nuevos, decide si el dispositivo esta activo o en reposo,
 *     actualiza la bateria, atiende los botones y la pantalla y duerme cuando
 *     no queda nada.
 *
 * Reposo: mas de REPOSO_ESPERA_MS sin caminar desde el fin de la marcha apaga
 * la pantalla y detiene su refresco; la IMU sigue muestreando para reconocer
 * una marcha nueva. Al confirmarse la marcha la pantalla se enciende sola.
 *
 * Botones (MODO, MAS y MENOS):
 *   - Pantalla normal: MODO corto entra a configurar; MODO largo inicia una
 *     sesion nueva (pasos y kcal en cero).
 *   - Configurar peso y luego estatura: MAS y MENOS cambian el valor dentro
 *     de su rango; MODO pasa a lo siguiente y al final guarda. Si algo cambio
 *     empieza una sesion nueva, para que las kcal de una sesion usen siempre
 *     los mismos datos.
 *   - En reposo cualquier boton enciende la pantalla VISTA_MS, sin reiniciar
 *     la espera de inactividad.
 *
 * El main no escribe por el serial: para ver los pasos y ajustar el detector
 * se usa pruebas/prueba_pasos.c. stdio_init_all() solo deja el USB listo para
 * cargar programas.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "config.h"
#include "mpu6050.h"
#include "pasos.h"
#include "calorias.h"
#include "estado.h"
#include "botones.h"
#include "bateria.h"
#include "bateria_adc.h"
#include "pantalla.h"

typedef enum {
    MENU_NORMAL,        /**< Pasos, kcal y bateria. */
    MENU_PESO,          /**< Configurando el peso. */
    MENU_ESTATURA       /**< Configurando la estatura. */
} menu_t;

typedef struct {
    uint32_t peso_kg;
    uint32_t estatura_cm;
} usuario_t;

static pasos_t pasos;
static calorias_t calorias;
static estado_disp_t dispositivo;
static bateria_t bateria;

static usuario_t usuario = { USUARIO_PESO_KG, USUARIO_ESTATURA_CM };
static usuario_t editando;          /* valores mientras se configura */
static menu_t menu = MENU_NORMAL;

static repeating_timer_t timer_pantalla;
static volatile bool toca_pantalla = false;     /* la marca el timer */

/* Solo las usa el ciclo principal */
static bool cambio_estado = false;
static bool pantalla_encendida = true;
static bool apagar_pendiente = false;
static bool encender_pendiente = false;

/** Callback del timer (corre en interrupcion): solo marca la bandera. */
static bool marcar_pantalla(repeating_timer_t *t) {
    (void)t;
    toca_pantalla = true;
    return true;
}

static void arrancar_refresco(void) {
    add_repeating_timer_ms(-(int32_t)PANTALLA_PERIODO_MS, marcar_pantalla, NULL, &timer_pantalla);
}

/** Sesion nueva: pasos y calorias en cero, con los datos del usuario. */
static void iniciar_sesion(void) {
    const float paso_m = calorias_paso_por_estatura((float)usuario.estatura_cm / 100.0f);
    pasos_iniciar(&pasos);
    calorias_iniciar(&calorias, (float)usuario.peso_kg, paso_m);
}

/** Inicializacion: aqui si se puede esperar. Sin IMU o sin pantalla se reintenta cada segundo. */
static void iniciar(void) {
    stdio_init_all();

    while (!pantalla_iniciar(PANTALLA_I2C, PANTALLA_PIN_SDA, PANTALLA_PIN_SCL, PANTALLA_I2C_HZ)) {
        sleep_ms(1000);
    }
    irq_set_priority(I2C0_IRQ + i2c_get_index(PANTALLA_I2C), PRIORIDAD_IRQ_PANTALLA);

    while (!mpu6050_iniciar()) {
        sleep_ms(1000);
    }
    while (!mpu6050_muestrear_con_int()) {
        sleep_ms(1000);
    }
    botones_iniciar();
    bateria_iniciar(&bateria);
    bateria_adc_iniciar();

    iniciar_sesion();
    estado_iniciar(&dispositivo, time_us_32());
    arrancar_refresco();
}

/**
 * Pasa la muestra al contador de pasos, suma la energia de los pasos nuevos y
 * actualiza el estado del dispositivo.
 */
static void procesar_muestra(const mpu6050_datos_t *d) {
    const uint32_t nuevos = pasos_agregar(&pasos, d);
    if (nuevos > 0) {
        calorias_sumar(&calorias, nuevos, pasos.duracion_s);
    }
    if (estado_actualizar(&dispositivo, d->t_us, nuevos)) {
        cambio_estado = true;
    }
}

/** Sube o baja un valor en 1 segun el boton, sin salirse del rango. */
static uint32_t ajustar(uint32_t valor, boton_id_t boton, uint32_t minimo, uint32_t maximo) {
    if (boton == BOTON_MAS && valor < maximo) {
        return valor + 1u;
    }
    if (boton == BOTON_MENOS && valor > minimo) {
        return valor - 1u;
    }
    return valor;
}

/** Sale del menu guardando; si algo cambio empieza una sesion nueva. */
static void guardar_configuracion(void) {
    const bool cambio = (editando.peso_kg != usuario.peso_kg) ||
                        (editando.estatura_cm != usuario.estatura_cm);
    usuario = editando;
    if (cambio) {
        iniciar_sesion();
    }
    menu = MENU_NORMAL;
}

/**
 * Atiende un evento de boton. En reposo el boton solo enciende la pantalla;
 * en activo o en vista actua sobre el menu.
 */
static void atender_boton(const boton_evento_t *ev) {
    if (estado_boton(&dispositivo, time_us_32())) {
        cambio_estado = true;
        return;
    }

    switch (menu) {
        case MENU_NORMAL:
            if (ev->boton == BOTON_MODO && ev->largo) {
                iniciar_sesion();
            } else if (ev->boton == BOTON_MODO) {
                editando = usuario;
                menu = MENU_PESO;
            }
            break;
        case MENU_PESO:
            if (ev->boton == BOTON_MODO) {
                menu = MENU_ESTATURA;
            } else {
                editando.peso_kg = ajustar(editando.peso_kg, ev->boton, PESO_MIN_KG, PESO_MAX_KG);
            }
            break;
        case MENU_ESTATURA:
            if (ev->boton == BOTON_MODO) {
                guardar_configuracion();
            } else {
                editando.estatura_cm = ajustar(editando.estatura_cm, ev->boton,
                                               ESTATURA_MIN_CM, ESTATURA_MAX_CM);
            }
            break;
    }
    toca_pantalla = true;   /* que se vea el cambio enseguida */
}

/**
 * La pantalla va encendida en activo y en vista, y apagada en reposo. Al
 * apagar se para el refresco antes de pedir el apagado, para que un tic
 * atrasado no vuelva a dibujar; lo que se estaba configurando se descarta.
 */
static void aplicar_estado(void) {
    const bool encender = (dispositivo.actual != ESTADO_REPOSO);
    if (encender == pantalla_encendida) {
        return;
    }
    pantalla_encendida = encender;

    if (encender) {
        apagar_pendiente = false;
        encender_pendiente = true;
    } else {
        cancel_repeating_timer(&timer_pantalla);
        toca_pantalla = false;
        encender_pendiente = false;
        apagar_pendiente = true;
        menu = MENU_NORMAL;
    }
}

/** Pantalla normal: pasos, calorias, distancia y bateria. */
static void dibujar_normal(void) {
    char texto[20];
    const float dist_m = calorias_distancia_m(&calorias, pasos.total);

    pantalla_texto("Pasos", 0, 0, 1);
    pantalla_dibujar_bateria(70, 0, bateria.pct);
    if (bateria.iniciado) {
        snprintf(texto, sizeof(texto), "%3u%%", (unsigned)bateria.pct);
    } else {
        snprintf(texto, sizeof(texto), " --%%");    /* el primer segundo, sin medida */
    }
    pantalla_texto(texto, 96, 1, 1);

    snprintf(texto, sizeof(texto), "%5lu", (unsigned long)pasos.total);
    pantalla_texto(texto, 4, 14, 3);
    pantalla_hline(0, 41, PANTALLA_ANCHO, 1);

    snprintf(texto, sizeof(texto), "%.1f kcal", calorias.kcal);
    pantalla_texto(texto, 0, 45, 1);

    if (dist_m >= 1000.0f) {
        snprintf(texto, sizeof(texto), "%.2f km", dist_m / 1000.0f);
    } else {
        snprintf(texto, sizeof(texto), "%.1f m", dist_m);
    }
    pantalla_texto(texto, 0, 55, 1);

    const int n_peso = snprintf(texto, sizeof(texto), "%lukg", (unsigned long)usuario.peso_kg);
    pantalla_texto(texto, PANTALLA_ANCHO - n_peso * 8, 55, 1);
}

/** Pantalla de configuracion del peso o de la estatura. */
static void dibujar_menu(void) {
    char texto[20];

    if (menu == MENU_PESO) {
        pantalla_texto("Peso", 0, 0, 1);
        snprintf(texto, sizeof(texto), "%lu kg", (unsigned long)editando.peso_kg);
    } else {
        pantalla_texto("Estatura", 0, 0, 1);
        snprintf(texto, sizeof(texto), "%lu cm", (unsigned long)editando.estatura_cm);
    }
    pantalla_texto(texto, 8, 18, 2);

    if (menu == MENU_ESTATURA) {
        const float paso_m = calorias_paso_por_estatura((float)editando.estatura_cm / 100.0f);
        snprintf(texto, sizeof(texto), "paso %.2f m", paso_m);
        pantalla_texto(texto, 0, 38, 1);
    }
    pantalla_hline(0, 50, PANTALLA_ANCHO, 1);
    /* 16 caracteres de 8 px llenan los 128 px del ancho */
    pantalla_texto(menu == MENU_PESO ? "+/-    MODO:sig" : "+/-    MODO:ok", 0, 55, 1);
}

/** Dibuja la pantalla que toca en el bufer y empieza a enviarlo. */
static void actualizar_pantalla(void) {
    pantalla_limpiar(0);
    if (menu == MENU_NORMAL) {
        dibujar_normal();
    } else {
        dibujar_menu();
    }
    pantalla_mostrar();
}

/**
 * Con el bus de la pantalla libre hace lo pendiente, en este orden: apagar,
 * encender (y refrescar enseguida) o refrescar.
 */
static void atender_pantalla(void) {
    if (apagar_pendiente) {
        apagar_pendiente = !pantalla_apagar();
    } else if (encender_pendiente) {
        if (pantalla_encender()) {
            encender_pendiente = false;
            toca_pantalla = true;       /* que muestre los valores al encender */
            arrancar_refresco();
        }
    } else if (toca_pantalla && pantalla_encendida) {
        toca_pantalla = false;
        actualizar_pantalla();
    }
}

/** Con 3 lecturas fallidas seguidas el bus quedo colgado: se libera. */
static void vigilar_bus(void) {
    if (mpu6050_fallos() >= 3) {
        mpu6050_recuperar_bus();
    }
}

/** true si hay algo que el ciclo principal pueda atender ya. */
static bool hay_trabajo(void) {
    const bool pantalla_pendiente = apagar_pendiente || encender_pendiente ||
                                    (toca_pantalla && pantalla_encendida);
    return mpu6050_hay_lectura() || botones_hay_evento() || bateria_adc_hay_muestra() ||
           (pantalla_pendiente && !pantalla_ocupada());
}

/**
 * Duerme hasta la proxima interrupcion si no hay nada por atender. La consulta
 * y el wfi van con las interrupciones enmascaradas: si un evento llega justo
 * entre los dos, queda pendiente y el wfi retorna enseguida.
 */
static void dormir(void) {
    const uint32_t estado = save_and_disable_interrupts();
    if (!hay_trabajo()) {
        __wfi();
    }
    restore_interrupts(estado);
}

int main(void) {
    iniciar();

    while (true) {
        mpu6050_datos_t d;
        if (mpu6050_tomar_lectura(&d)) {
            procesar_muestra(&d);
        }
        uint16_t crudo;
        if (bateria_adc_tomar(&crudo)) {
            bateria_agregar(&bateria, crudo);
        }
        boton_evento_t ev;
        if (botones_tomar_evento(&ev)) {
            atender_boton(&ev);
        }
        if (cambio_estado) {
            cambio_estado = false;
            aplicar_estado();
        }
        if (!pantalla_ocupada()) {
            atender_pantalla();
        }
        vigilar_bus();
        dormir();
    }
}
