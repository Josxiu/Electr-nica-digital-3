/**
 * @file sintetizador.c
 * @brief Implementación del DDS polifónico con tabla en Flash, interpolación y rampa.
 *
 * Idea general de la polifonía:
 *   - Hay SINT_VOCES voces iguales. Cada una es un DDS completo (paso, fase y
 *     rampa de amplitud propios), así que cada nota conserva su altura.
 *   - En cada interrupción se calcula la muestra de cada voz y se suman.
 *   - La suma se divide entre SINT_VOCES para que nunca se salga del DAC.
 */
#include "sintetizador.h"
#include "lut_seno.h"
#include "dac.h"
#include "pines.h"
#include "hardware/gpio.h"

/** Desplazamiento que deja en los bits bajos el índice de la tabla. */
#define DESPL_INDICE (32u - LUT_BITS_INDICE)
/** Desplazamiento que deja en los bits bajos los 8 bits de fracción. */
#define DESPL_FRACCION (DESPL_INDICE - 8u)

/**
 * @brief Estado de una voz (un DDS independiente).
 *
 * El bucle principal escribe paso y nota_pulsada; la interrupción escribe
 * amplitud y fase. Todos son de 32 bits o menos, así que el M0+ los lee y
 * escribe de una sola vez (sin quedar a medias).
 */
typedef struct {
    volatile uint32_t paso;          /**< paso de fase de la nota que tiene asignada     */
    volatile bool     nota_pulsada;  /**< true mientras el botón de su nota siga abajo   */
    volatile uint32_t amplitud;      /**< rampa: 0 (callada) a SINT_RAMPA_MUESTRAS       */
    uint32_t          fase;          /**< acumulador de fase (solo lo toca la interrupción) */
    uint32_t          orden_asignacion; /**< número de asignación: menor = más antigua   */
} voz_t;

static voz_t voces[SINT_VOCES];
static uint32_t contador_asignaciones = 0;   /**< crece cada vez que se asigna una voz */
static volatile bool interpolar = true;      /**< modo de lectura de la tabla          */

void sintetizador_init(void)
{
    for (int v = 0; v < SINT_VOCES; v++) {
        voces[v].paso = 0u;
        voces[v].nota_pulsada = false;
        voces[v].amplitud = 0u;
        voces[v].fase = 0u;
        voces[v].orden_asignacion = 0u;
    }
    interpolar = true;
    gpio_init(PIN_PRUEBA_ISR);
    gpio_set_dir(PIN_PRUEBA_ISR, GPIO_OUT);
}

uint32_t sintetizador_paso_de(float freq_hz)
{
    if (freq_hz <= 0.0f) {
        return 0u;
    }
    return (uint32_t)((double)freq_hz * 4294967296.0 / (double)FS_HZ + 0.5);
}

/**
 * @brief Busca una voz libre: sin nota pulsada y con su rampa de salida terminada.
 * @return índice de la voz, o -1 si todas están ocupadas.
 */
static int buscar_voz_libre(void)
{
    for (int v = 0; v < SINT_VOCES; v++) {
        if (!voces[v].nota_pulsada && voces[v].amplitud == 0u) {
            return v;
        }
    }
    return -1;
}

/**
 * @brief Busca la voz asignada hace más tiempo (la de menor orden_asignacion).
 * @return índice de la voz.
 */
static int buscar_voz_mas_antigua(void)
{
    int mas_antigua = 0;
    for (int v = 1; v < SINT_VOCES; v++) {
        /* Resta con signo: sigue funcionando aunque el contador dé la vuelta. */
        if ((int32_t)(voces[v].orden_asignacion - voces[mas_antigua].orden_asignacion) < 0) {
            mas_antigua = v;
        }
    }
    return mas_antigua;
}

int sintetizador_tocar(uint32_t paso)
{
    int voz = buscar_voz_libre();
    if (voz < 0) {
        voz = buscar_voz_mas_antigua();      /* voces llenas: se reutiliza la más antigua */
    }

    voces[voz].orden_asignacion = ++contador_asignaciones;
    voces[voz].paso = paso;                  /* primero el paso y luego la marca, para que */
    voces[voz].nota_pulsada = (paso != 0u);  /* la interrupción nunca vea un paso viejo     */
    return voz;
}

void sintetizador_soltar(int voz)
{
    if (voz >= 0 && voz < SINT_VOCES) {
        voces[voz].nota_pulsada = false;     /* la interrupción la baja en rampa hasta 0 */
    }
}

void sintetizador_interpolacion(bool activa)
{
    interpolar = activa;
}

bool sintetizador_interpolando(void)
{
    return interpolar;
}

/**
 * @brief Avanza una voz una muestra y devuelve su valor.
 * @param voz voz a calcular.
 * @return muestra con signo, de -128 a 127, ya escalada por la rampa
 *         (0 si la voz está callada).
 */
static inline int32_t calcular_muestra_voz(voz_t *voz)
{
    /* 1) Rampa: sube mientras la nota esté pulsada y baja al soltarla. */
    uint32_t amp = voz->amplitud;
    if (voz->nota_pulsada) {
        if (amp < SINT_RAMPA_MUESTRAS) amp++;
    } else if (amp > 0u) {
        amp--;
    }
    voz->amplitud = amp;

    if (amp == 0u) {
        voz->fase = 0u;                     /* la próxima nota arranca en el cruce por cero */
        return 0;
    }

    /* 2) DDS: avanzar la fase y leer la tabla. */
    voz->fase += voz->paso;
    uint32_t i = voz->fase >> DESPL_INDICE;
    int32_t s = (int32_t)lut_seno[i] - (int32_t)DAC_MEDIO;     /* -128..127 */

    /* 3) Interpolación lineal con la muestra siguiente de la tabla. */
    if (interpolar) {
        uint32_t frac = (voz->fase >> DESPL_FRACCION) & 0xFFu;
        int32_t s2 = (int32_t)lut_seno[(i + 1u) & (LUT_TAMANO - 1u)] - (int32_t)DAC_MEDIO;
        s += ((s2 - s) * (int32_t)frac) / 256;
    }

    /* 4) Escalar por la rampa: s * amp / SINT_RAMPA_MUESTRAS. */
    return (s * (int32_t)amp) / (int32_t)SINT_RAMPA_MUESTRAS;
}

void sintetizador_muestra(void)
{
    gpio_put(PIN_PRUEBA_ISR, 1);            /* el ancho del pulso = tiempo de cálculo */

    /* Suma de todas las voces (las calladas aportan 0). */
    int32_t mezcla = 0;
    for (int v = 0; v < SINT_VOCES; v++) {
        mezcla += calcular_muestra_voz(&voces[v]);
    }

    /* Cada voz da -128..127, así que la suma de SINT_VOCES voces dividida
       entre SINT_VOCES vuelve a -128..127: no se desborda ni se recorta,
       aunque suenen todas a la vez. */
    mezcla /= SINT_VOCES;
    dac_escribir((uint8_t)(mezcla + (int32_t)DAC_MEDIO));

    gpio_put(PIN_PRUEBA_ISR, 0);
}
