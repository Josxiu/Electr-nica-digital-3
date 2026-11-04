/**
 * @file medicion.h
 * @brief Cálculo de frecuencia y ciclo de trabajo a partir de flancos.
 *
 * Este módulo NO toca hardware. Solo recibe "hubo un flanco de subida o de
 * bajada en el instante t (en microsegundos)" y, cuando se cierra una ventana
 * de reporte, entrega la frecuencia y el ciclo de trabajo promedio de los
 * periodos completos que vio en esa ventana.
 *
 * Por eso lo pueden usar las tres versiones: lo único que cambia entre ellas
 * es QUIÉN llama a estas funciones (el while, una ISR o el main tras una
 * bandera). También se puede probar en el PC con timestamps inventados.
 *
 * Método (promedio por ventana):
 * @verbatim
 *        ┌───────┐       ┌───────┐       ┌───────┐
 *   ─────┘       └───────┘       └───────┘       └───
 *        s1      b1      s2      b2      s3
 *        |<-tH->|        |
 *        |<----- T ----->|
 * @endverbatim
 *   - Cada subida cierra un periodo: T = s2 - s1, tH = b1 - s1.
 *   - Durante la ventana se suman todos los T y tH y se cuentan los periodos N.
 *   - Al cerrar: f = N / ΣT   y   D = 100 · ΣtH / ΣT.
 *
 * Promediar muchos periodos reduce el efecto de la resolución de 1 µs del
 * timer: a 10 kHz, un periodo aislado (100 µs) tendría hasta 1 % de error,
 * pero en 100 ms se acumulan unos 1000 periodos.
 *
 * Unidades: tiempos en µs (uint32_t, aritmética sin signo para que el
 * desbordamiento del timer cada ~71 min no afecte las restas).
 * Resultados en enteros de punto fijo: frecuencia en mHz y ciclo de trabajo
 * en décimas de %. Así no se usa float (el M0+ no tiene FPU) y el redondeo
 * es exacto y reproducible.
 */
#ifndef MEDICION_H
#define MEDICION_H

#include <stdbool.h>
#include <stdint.h>

/** Estado de una medición, tal como se reporta por la UART. */
typedef enum {
    MEDICION_ESPERANDO,         /**< Aún no hay un periodo completo.          */
    MEDICION_OK,                /**< Hay frecuencia y ciclo de trabajo válidos. */
    MEDICION_SIN_TRANSICIONES   /**< Más de 500 ms sin flancos.              */
} medicion_estado_t;

/**
 * Estado interno del medidor. Se declara aquí para poder crearlo como
 * variable estática, pero solo las funciones de este módulo lo modifican.
 */
typedef struct {
    /* Cadena de flancos: permite armar un periodo con flancos de ventanas
     * distintas, así no se pierde el periodo que cruza el cierre. */
    bool     hay_subida;          /**< Se conoce la última subida.             */
    bool     hay_bajada;          /**< Hubo bajada después de esa subida.      */
    uint32_t t_subida_us;         /**< Instante de la última subida.           */
    uint32_t t_bajada_us;         /**< Instante de la bajada posterior.        */
    uint32_t t_ultimo_flanco_us;  /**< Para detectar entrada sin transiciones. */
    bool     en_timeout;          /**< Ya se declaró SIN_TRANSICIONES.         */

    /* Acumuladores de la ventana actual (se ponen en 0 al cerrarla). */
    uint32_t n_periodos;          /**< Periodos completos en la ventana.       */
    uint32_t suma_periodos_us;    /**< Σ T  de esos periodos.                  */
    uint32_t suma_alto_us;        /**< Σ tH de esos periodos.                  */

    /* Pulso más corto (el menor entre tH y tL), para el tramo ciego de polling. */
    uint32_t pulso_min_us;            /**< Mínimo en la ventana actual.              */
    uint32_t pulso_min_referencia_us; /**< Mínimo de la última ventana con periodos
                                           (0 = aún no se conoce).                   */

    /* Diagnóstico acumulado desde el arranque. */
    uint32_t eventos_perdidos;    /**< Flancos repetidos o perdidos detectados. */
    uint32_t cortes_tramo_ciego;  /**< Periodos descartados por un tramo ciego.  */
} medicion_t;

/**
 * Copia de los acumuladores de una ventana ya cerrada.
 * Tomarla es muy rápido (solo copia); el cálculo pesado se hace después con
 * medicion_calcular(), fuera de cualquier sección crítica.
 */
typedef struct {
    uint32_t n_periodos;
    uint32_t suma_periodos_us;
    uint32_t suma_alto_us;
    bool     sin_transiciones;
} medicion_ventana_t;

/** Resultado listo para reportar. */
typedef struct {
    medicion_estado_t estado;
    uint32_t frecuencia_mhz;      /**< Frecuencia en milihertz (1 kHz = 1 000 000). */
    uint16_t duty_decimas;        /**< Ciclo de trabajo en décimas de % (25,0 % = 250). */
} medicion_resultado_t;

/**
 * @brief Pone el medidor en su estado inicial (sin flancos conocidos).
 * @param m       medidor.
 * @param t_us    instante actual; desde aquí se cuentan los 500 ms sin flancos.
 */
void medicion_iniciar(medicion_t *m, uint32_t t_us);

/**
 * @brief Registra un flanco.
 * @param m       medidor.
 * @param subida  true si la señal pasó de 0 a 1; false si pasó de 1 a 0.
 * @param t_us    instante del flanco en µs (time_us_32()).
 *
 * Contexto: se puede llamar desde el while (polling), desde la ISR del GPIO
 * (versión IRQ) o desde el main (versión híbrida). Tarda pocos ciclos.
 * No es reentrante: no debe llamarse a la vez desde dos contextos.
 */
void medicion_registrar_flanco(medicion_t *m, bool subida, uint32_t t_us);

/**
 * @brief Avisa que se perdieron flancos (no se sabe cuántos).
 *
 * Rompe la cadena para no armar un periodo con dos flancos que no son
 * consecutivos. Se llama si la ISR vio subida y bajada pendientes a la vez o
 * si la cola de eventos se llenó.
 */
void medicion_marcar_perdida(medicion_t *m);

/**
 * @brief Revisa un tramo en que nadie miró la entrada (solo versión polling).
 * @param m            medidor.
 * @param duracion_us  cuánto duró el tramo (desde la última lectura del pin
 *                     hasta la siguiente).
 * @return true si el periodo en curso se descartó.
 *
 * Mientras el ciclo de polling arma y envía un reporte no lee el pin. Si en
 * ese tramo cabe un pulso completo (subida y bajada, o al revés), el nivel
 * final es igual al inicial y los dos flancos pasan sin verse: el siguiente
 * periodo mediría el doble. Como no se puede saber si eso ocurrió, se
 * compara la duración del tramo con el pulso más corto de la ventana
 * anterior: si el tramo es igual o más largo, se descarta el periodo en
 * curso (se rompe la cadena). Si aún no se conoce el pulso, se descarta
 * por precaución. Solo se pierde un periodo por ventana.
 */
bool medicion_revisar_tramo_ciego(medicion_t *m, uint32_t duracion_us);

/**
 * @brief Cierra la ventana actual: copia los acumuladores y los pone en 0.
 * @param m       medidor.
 * @param t_us    instante del cierre (para el control de 500 ms sin flancos).
 * @param v       [salida] copia de la ventana.
 *
 * Contexto: si los flancos se registran desde una ISR, el llamador debe
 * envolver esta función en una sección crítica corta
 * (save_and_disable_interrupts / restore_interrupts).
 */
void medicion_cerrar_ventana(medicion_t *m, uint32_t t_us, medicion_ventana_t *v);

/**
 * @brief Calcula frecuencia y ciclo de trabajo de una ventana cerrada.
 * @param v    ventana cerrada.
 * @param res  [salida] resultado con estado y valores en punto fijo.
 *
 * Usa divisiones de 64 bits con redondeo al más cercano. Función pura: no
 * depende de ningún estado global, se puede llamar desde cualquier contexto.
 */
void medicion_calcular(const medicion_ventana_t *v, medicion_resultado_t *res);

#endif /* MEDICION_H */
