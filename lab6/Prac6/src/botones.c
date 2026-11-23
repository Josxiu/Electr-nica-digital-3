/**
 * @file botones.c
 * @brief Botones con antirrebote por interrupciones. Ver botones.h.
 */
#include "botones.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#define FLANCOS     (GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE)

typedef enum {
    SIN_EVENTO = 0,
    EVENTO_CORTO,
    EVENTO_LARGO
} evento_t;

static const uint8_t pines[BOTONES_N] = { BOTON_PIN_MODO, BOTON_PIN_MAS, BOTON_PIN_MENOS };

/* Solo los tocan las alarmas, que no se interrumpen entre si */
static bool presionado[BOTONES_N];
static uint32_t t_presion_ms[BOTONES_N];

/* Los escribe la alarma y los consume el main */
static volatile evento_t evento[BOTONES_N];

static void activar_irq(int i) {
    gpio_acknowledge_irq(pines[i], FLANCOS);
    gpio_set_irq_enabled(pines[i], FLANCOS, true);
}

/**
 * Alarma del antirrebote: el contacto ya dejo de rebotar. La interrupcion se
 * activa antes de leer el nivel; asi un cambio justo despues de la lectura
 * genera otro flanco y no se pierde.
 */
static int64_t confirmar(alarm_id_t id, void *dato) {
    (void)id;
    const int i = (int)(intptr_t)dato;

    activar_irq(i);
    const bool abajo = !gpio_get(pines[i]);
    const uint32_t ahora_ms = time_us_32() / 1000u;

    if (abajo && !presionado[i]) {
        presionado[i] = true;
        t_presion_ms[i] = ahora_ms;
    } else if (!abajo && presionado[i]) {
        presionado[i] = false;
        const bool largo = (ahora_ms - t_presion_ms[i]) >= BOTON_LARGO_MS;
        evento[i] = largo ? EVENTO_LARGO : EVENTO_CORTO;
    }
    return 0;   /* no se repite */
}

/** ISR del GPIO: ignora los rebotes y deja la lectura a la alarma. */
static void isr_botones(void) {
    for (int i = 0; i < BOTONES_N; i++) {
        if (gpio_get_irq_event_mask(pines[i]) & FLANCOS) {
            gpio_set_irq_enabled(pines[i], FLANCOS, false);
            gpio_acknowledge_irq(pines[i], FLANCOS);
            if (add_alarm_in_ms(BOTON_ANTIRREBOTE_MS, confirmar, (void *)(intptr_t)i, true) < 0) {
                activar_irq(i);     /* sin alarmas libres: se espera el siguiente flanco */
            }
        }
    }
}

void botones_iniciar(void) {
    uint32_t mascara = 0;
    for (int i = 0; i < BOTONES_N; i++) {
        gpio_init(pines[i]);
        gpio_set_dir(pines[i], GPIO_IN);
        gpio_pull_up(pines[i]);
        presionado[i] = false;
        evento[i] = SIN_EVENTO;
        mascara |= 1u << pines[i];
    }

    gpio_add_raw_irq_handler_masked(mascara, isr_botones);
    for (int i = 0; i < BOTONES_N; i++) {
        activar_irq(i);
    }
    irq_set_priority(IO_IRQ_BANK0, PRIORIDAD_IRQ_GPIO);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

bool botones_hay_evento(void) {
    for (int i = 0; i < BOTONES_N; i++) {
        if (evento[i] != SIN_EVENTO) {
            return true;
        }
    }
    return false;
}

bool botones_tomar_evento(boton_evento_t *ev) {
    for (int i = 0; i < BOTONES_N; i++) {
        /* Leer y borrar juntos: la alarma no puede dejar uno nuevo en medio */
        const uint32_t estado = save_and_disable_interrupts();
        const evento_t e = evento[i];
        evento[i] = SIN_EVENTO;
        restore_interrupts(estado);

        if (e != SIN_EVENTO) {
            ev->boton = (boton_id_t)i;
            ev->largo = (e == EVENTO_LARGO);
            return true;
        }
    }
    return false;
}
