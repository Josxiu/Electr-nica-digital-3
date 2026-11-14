/**
 * @file cola_flancos.c
 * @brief Implementación de la cola de eventos de flanco.
 */
#include "cola_flancos.h"

#if (COLA_FLANCOS_TAM & (COLA_FLANCOS_TAM - 1u)) != 0
#error "COLA_FLANCOS_TAM debe ser potencia de 2"
#endif
/** Máscara para recortar los índices (el tamaño es 2^k). */
#define MASCARA (COLA_FLANCOS_TAM - 1u)

static evento_flanco_t eventos[COLA_FLANCOS_TAM];  /**< Buffer circular de eventos. */
static volatile uint32_t cabeza = 0u;   /**< Índice de escritura; la escribe solo la ISR.  */
static volatile uint32_t cola   = 0u;   /**< Índice de lectura; la escribe solo el main. */
static volatile uint32_t desbordes = 0u; /**< Eventos descartados por cola llena. */

void cola_flancos_iniciar(void)
{
    cabeza = 0u;
    cola   = 0u;
    desbordes = 0u;
}

bool cola_flancos_meter(const evento_flanco_t *e)
{
    uint32_t h = cabeza;
    uint32_t siguiente = (h + 1u) & MASCARA;
    if (siguiente == cola) {
        desbordes++;
        return false;               /* llena */
    }
    eventos[h] = *e;                /* 1) escribir el dato  */
    cabeza = siguiente;             /* 2) publicarlo        */
    return true;
}

bool cola_flancos_sacar(evento_flanco_t *e)
{
    uint32_t c = cola;
    if (c == cabeza) {
        return false;               /* vacía */
    }
    *e = eventos[c];                /* 1) leer el dato      */
    cola = (c + 1u) & MASCARA;      /* 2) liberar la celda  */
    return true;
}

uint32_t cola_flancos_desbordes(void)
{
    return desbordes;
}
