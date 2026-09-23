/**
 * @file tablero.c
 * @brief Implementacion del multiplexado por software de los cuatro digitos.
 *
 * Idea central de la version en C: la conversion "numero -> que pines hay que
 * poner en alto" se resuelve UNA sola vez, en tablero_init(), y se guarda como
 * palabras de 32 bits listas para escribir. El refresco, que corre ~500 veces
 * por segundo, se reduce a un OR y una escritura enmascarada.
 */
#include "tablero.h"
#include "hardware/gpio.h"

/** Milisegundos que permanece encendido cada digito: 2 ms x 4 = 125 Hz. */
#define MS_POR_DIGITO 2u

/**
 * Codificacion de los digitos 0 a 9 en siete segmentos.
 * Bit 0 = a, bit 1 = b, bit 2 = c, bit 3 = d, bit 4 = e, bit 5 = f, bit 6 = g.
 */
static const uint8_t DIGITO[10] = {
    0x3F,  /* 0 -> a b c d e f     0b0111111 */
    0x06,  /* 1 -> b c             0b0000110 */
    0x5B,  /* 2 -> a b d e g       0b1011011 */
    0x4F,  /* 3 -> a b c d g       0b1001111 */
    0x66,  /* 4 -> b c f g         0b1100110 */
    0x6D,  /* 5 -> a c d f g       0b1101101 */
    0x7D,  /* 6 -> a c d e f g     0b1111101 */
    0x07,  /* 7 -> a b c           0b0000111 */
    0x7F,  /* 8 -> todos           0b1111111 */
    0x6F   /* 9 -> a b c d f g     0b1101111 */
};

/**
 * @brief Convierte una lista de GPIO en una mascara de 32 bits.
 * @param pines lista de numeros de GPIO (0-29).
 * @param n     cuantos elementos tiene la lista.
 * @return Mascara con un 1 en la posicion de cada GPIO de la lista.
 *
 * El numero de GPIO coincide con la posicion del bit en los registros del
 * SIO, por eso basta con desplazar un 1.
 */
static uint32_t mascara_de(const uint8_t *pines, int n)
{
    uint32_t m = 0;
    for (int i = 0; i < n; i++) {
        m |= 1u << pines[i];
    }
    return m;
}

void tablero_init(tablero_t *t,
                  const uint8_t pines_segmento[TAB_NUM_SEG],
                  const uint8_t pines_comun[TAB_NUM_DIG])
{
    t->seg_mask = mascara_de(pines_segmento, TAB_NUM_SEG);
    t->com_mask = mascara_de(pines_comun, TAB_NUM_DIG);

    /* La polaridad se expresa como un XOR: un 1 en 'invertir' significa
       "este pin se activa en bajo". Sustituye a las constantes SEG_ON y
       COM_ON de la version Arduino y se aplica de un golpe a la palabra. */
    t->invertir = (TAB_SEG_ACTIVO_ALTO ? 0u : t->seg_mask)
                | (TAB_COM_ACTIVO_ALTO ? 0u : t->com_mask);

    /* Tabla digito -> palabra de GPIO. Los segmentos estan repartidos en
       pines no consecutivos (GP19, GP17, GP2, ...), asi que esta traduccion
       es imprescindible y conviene pagarla una sola vez. */
    for (int d = 0; d < 10; d++) {
        uint32_t w = 0;
        for (int s = 0; s < TAB_NUM_SEG; s++) {
            if ((DIGITO[d] >> s) & 1u) {
                w |= 1u << pines_segmento[s];
            }
        }
        t->seg_word[d] = w;
    }

    for (int i = 0; i < TAB_NUM_DIG; i++) {
        t->com_word[i] = 1u << pines_comun[i];
        t->buffer[i]   = t->seg_word[0];
    }

    t->activo = 0;
    t->t_refresco = 0;

    /* Los once pines se configuran como grupo, no uno por uno. */
    uint32_t todos = t->seg_mask | t->com_mask;
    gpio_init_mask(todos);
    gpio_set_dir_out_masked(todos);

    /* Palabra logica 0 = todo apagado; el XOR la lleva a niveles fisicos. */
    gpio_put_masked(todos, 0u ^ t->invertir);
}

void tablero_mostrar(tablero_t *t, int nivel, int vidas, int tiempo)
{
    /* Se recortan los valores para no salirse de la tabla si llega algo raro. */
    if (nivel  < 0)  nivel  = 0;
    if (nivel  > 9)  nivel  = 9;
    if (vidas  < 0)  vidas  = 0;
    if (vidas  > 9)  vidas  = 9;
    if (tiempo < 0)  tiempo = 0;
    if (tiempo > 99) tiempo = 99;

    t->buffer[0] = t->seg_word[nivel];
    t->buffer[1] = t->seg_word[vidas];
    t->buffer[2] = t->seg_word[(tiempo / 10) % 10];   /* decenas */
    t->buffer[3] = t->seg_word[tiempo % 10];          /* unidades */
}

void tablero_refrescar(tablero_t *t, uint32_t ahora)
{
    if (ahora - t->t_refresco < MS_POR_DIGITO) {
        return;
    }
    t->t_refresco = ahora;

    t->activo = (uint8_t)((t->activo + 1) % TAB_NUM_DIG);

    /* ACTUALIZACION CONJUNTA DE GPIO (requisito 2.2 de la practica).
     *
     * Segmentos y comun se combinan en una sola palabra y se escriben con
     * una unica operacion enmascarada. gpio_put_masked() se resuelve en una
     * escritura de 32 bits al registro atomico del SIO, de modo que los once
     * pines conmutan en el mismo ciclo de reloj.
     *
     * Consecuencia directa: desaparece el estado transitorio en que el
     * digito anterior seguia encendido mientras los segmentos ya mostraban
     * el valor del siguiente. La version Arduino necesitaba apagar el comun
     * antes de tocar los segmentos justamente para tapar ese efecto; aqui el
     * paso de apagado previo ya no hace falta.
     *
     * Los GPIO que no estan en la mascara (LEDs y pulsadores) quedan intactos.
     */
    uint32_t logico = t->buffer[t->activo] | t->com_word[t->activo];
    gpio_put_masked(t->seg_mask | t->com_mask, logico ^ t->invertir);
}

void tablero_apagar(tablero_t *t)
{
    gpio_put_masked(t->seg_mask | t->com_mask, 0u ^ t->invertir);
}
