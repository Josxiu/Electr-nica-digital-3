/**
 * @file medicion.c
 * @brief Implementación del cálculo de frecuencia y ciclo de trabajo.
 *
 * Ver medicion.h para el método. Aquí no hay acceso a hardware.
 */
#include "medicion.h"
#include "config.h"

void medicion_iniciar(medicion_t *m, uint32_t t_us)
{
    m->hay_subida         = false;
    m->hay_bajada         = false;
    m->t_subida_us        = 0u;
    m->t_bajada_us        = 0u;
    m->t_ultimo_flanco_us = t_us;    /* los 500 ms se cuentan desde el arranque */
    m->en_timeout         = false;

    m->n_periodos       = 0u;
    m->suma_periodos_us = 0u;
    m->suma_alto_us     = 0u;

    m->pulso_min_us            = UINT32_MAX;
    m->pulso_min_referencia_us = 0u;

    m->eventos_perdidos   = 0u;
    m->cortes_tramo_ciego = 0u;
}

void medicion_registrar_flanco(medicion_t *m, bool subida, uint32_t t_us)
{
    m->t_ultimo_flanco_us = t_us;
    m->en_timeout = false;

    if (subida) {
        if (m->hay_subida && m->hay_bajada) {
            /* Hay subida -> bajada -> subida: un periodo completo. */
            uint32_t periodo_us = t_us - m->t_subida_us;
            uint32_t alto_us    = m->t_bajada_us - m->t_subida_us;

            m->suma_periodos_us += periodo_us;
            m->suma_alto_us     += alto_us;
            m->n_periodos++;

            /* Pulso más corto de la ventana: el menor entre tH y tL. */
            uint32_t bajo_us = periodo_us - alto_us;
            uint32_t pulso   = (alto_us < bajo_us) ? alto_us : bajo_us;
            if (pulso < m->pulso_min_us) {
                m->pulso_min_us = pulso;
            }
        } else if (m->hay_subida) {
            /* Dos subidas seguidas: se perdió la bajada intermedia. */
            m->eventos_perdidos++;
        }
        /* Esta subida es el inicio del siguiente periodo. */
        m->t_subida_us = t_us;
        m->hay_subida  = true;
        m->hay_bajada  = false;
    } else {
        if (m->hay_subida && !m->hay_bajada) {
            m->t_bajada_us = t_us;
            m->hay_bajada  = true;
        } else if (m->hay_bajada) {
            /* Dos bajadas seguidas: se perdió la subida intermedia.
             * El periodo en curso ya no es confiable. */
            m->eventos_perdidos++;
            m->hay_subida = false;
            m->hay_bajada = false;
        }
        /* Bajada sin subida previa conocida: se ignora (aún no hay
         * referencia para medir tH). */
    }
}

void medicion_marcar_perdida(medicion_t *m)
{
    m->eventos_perdidos++;
    m->hay_subida = false;
    m->hay_bajada = false;
}

bool medicion_revisar_tramo_ciego(medicion_t *m, uint32_t duracion_us)
{
    bool pulso_desconocido = (m->pulso_min_referencia_us == 0u);
    if (!m->hay_subida || (!pulso_desconocido && duracion_us < m->pulso_min_referencia_us)) {
        return false;   /* no hay periodo en curso, o en el tramo no cabe un pulso */
    }
    m->cortes_tramo_ciego++;
    m->hay_subida = false;
    m->hay_bajada = false;
    return true;
}

void medicion_cerrar_ventana(medicion_t *m, uint32_t t_us, medicion_ventana_t *v)
{
    /* Copiar acumuladores y reiniciarlos para la siguiente ventana. */
    v->n_periodos       = m->n_periodos;
    v->suma_periodos_us = m->suma_periodos_us;
    v->suma_alto_us     = m->suma_alto_us;

    m->n_periodos       = 0u;
    m->suma_periodos_us = 0u;
    m->suma_alto_us     = 0u;

    /* El pulso más corto de esta ventana es la referencia de la siguiente. */
    if (v->n_periodos > 0u) {
        m->pulso_min_referencia_us = m->pulso_min_us;
    }
    m->pulso_min_us = UINT32_MAX;

    /* ¿Más de 500 ms sin flancos?
     * La resta se interpreta con signo: si un flanco llegó justo después de
     * leer t_us, la diferencia es negativa y no cuenta como timeout. */
    int32_t sin_flancos_us = (int32_t)(t_us - m->t_ultimo_flanco_us);
    if (m->en_timeout || sin_flancos_us > (int32_t)TIMEOUT_SIN_FLANCOS_US) {
        /* Se queda en timeout hasta el próximo flanco (así no importa que el
         * timer dé la vuelta si la entrada queda quieta mucho tiempo). */
        m->en_timeout = true;
        m->hay_subida = false;
        m->hay_bajada = false;
        m->pulso_min_referencia_us = 0u;   /* la próxima señal puede ser otra */
        v->sin_transiciones = true;
    } else {
        v->sin_transiciones = false;
    }
}

void medicion_calcular(const medicion_ventana_t *v, medicion_resultado_t *res)
{
    res->frecuencia_mhz = 0u;
    res->duty_decimas   = 0u;

    if (v->sin_transiciones) {
        res->estado = MEDICION_SIN_TRANSICIONES;
        return;
    }
    if (v->n_periodos == 0u || v->suma_periodos_us == 0u) {
        /* Hay flancos recientes pero ningún periodo completo en la ventana. */
        res->estado = MEDICION_ESPERANDO;
        return;
    }

    uint64_t suma_t = v->suma_periodos_us;

    /* f [mHz] = N / ΣT[µs] · 10^9, redondeado al entero más cercano. */
    uint64_t f_mhz = ((uint64_t)v->n_periodos * 1000000000ull + suma_t / 2u) / suma_t;

    /* D [décimas de %] = ΣtH / ΣT · 1000, redondeado. */
    uint64_t d_dec = ((uint64_t)v->suma_alto_us * 1000ull + suma_t / 2u) / suma_t;

    res->estado         = MEDICION_OK;
    res->frecuencia_mhz = (f_mhz > UINT32_MAX) ? UINT32_MAX : (uint32_t)f_mhz;
    res->duty_decimas   = (uint16_t)d_dec;
}
