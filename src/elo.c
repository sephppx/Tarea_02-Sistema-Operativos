#include <math.h>

#include "cmatch.h"

/*
 * Expectativa de victoria del jugador A frente a B (funcion logistica).
 *   E_A = 1 / (1 + 10^((ELO_B - ELO_A)/400))
 */
double elo_expected(double elo_a, double elo_b) {
    return 1.0 / (1.0 + pow(10.0, (elo_b - elo_a) / 400.0));
}

/*
 * Actualiza el ELO y las estadisticas de los dos jugadores de la partida segun
 * el resultado. La modificacion del ELO es seccion critica, por eso tomamos
 * g_mutex (el mismo lock que protege la lectura del ELO en el matchmaking),
 * asi nunca se lee un ELO a medio actualizar.
 *
 *   ELO_nuevo = ELO_anterior + K_elo * (S - E)
 */
void elo_apply_result(Match *m) {
    Player *a = m->players[0];
    Player *b = m->players[1];

    /* S = resultado real: 1 gana, 0 pierde, 0.5 empate */
    double sa, sb;
    if (m->status == M_P0_WIN)      { sa = 1.0; sb = 0.0; }
    else if (m->status == M_P1_WIN) { sa = 0.0; sb = 1.0; }
    else                            { sa = 0.5; sb = 0.5; }  /* empate */

    pthread_mutex_lock(&g_mutex);

    double ea = elo_expected(a->elo, b->elo);
    double eb = elo_expected(b->elo, a->elo);

    a->elo += g_cfg.k_elo * (sa - ea);
    b->elo += g_cfg.k_elo * (sb - eb);

    /* actualizamos contadores de partidas */
    if (m->status == M_P0_WIN)      { a->wins++;  b->losses++; }
    else if (m->status == M_P1_WIN) { b->wins++;  a->losses++; }
    else                            { a->draws++; b->draws++;  }

    pthread_mutex_unlock(&g_mutex);
}
