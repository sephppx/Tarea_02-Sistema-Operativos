#include <stdio.h>
#include <stdlib.h>

#include "cmatch.h"

/* todas las combinaciones ganadoras del gato */
static const int LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},   /* filas */
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},   /* columnas */
    {0, 4, 8}, {2, 4, 6}               /* diagonales */
};

/*
 * Revisa el tablero y devuelve el estado actual de la partida.
 * mark 1 = players[0], mark 2 = players[1].
 */
static MatchStatus evaluate_board(const int *cells) {
    for (int i = 0; i < 8; i++) {
        int a = cells[LINES[i][0]];
        int b = cells[LINES[i][1]];
        int c = cells[LINES[i][2]];
        if (a != 0 && a == b && b == c) {
            return (a == 1) ? M_P0_WIN : M_P1_WIN;
        }
    }
    /* si no hay ganador y el tablero esta lleno -> empate */
    for (int i = 0; i < BOARD_CELLS; i++) {
        if (cells[i] == 0) return M_ONGOING;
    }
    return M_DRAW;
}

/*
 * Thread del tablero. Hace de arbitro de la partida: en cada turno avisa al
 * jugador que le toca, espera su jugada, la registra, revisa si alguien gano y
 * cambia el turno. Cuando la partida termina actualiza el ELO/estadisticas,
 * espera a que los dos jugadores salgan de la partida, libera el tablero y se
 * encarga de liberar la memoria de la partida.
 */
void *board_thread(void *arg) {
    Match *m = (Match *)arg;

    registry_add(m);

    pthread_mutex_lock(&m->mtx);
    while (m->status == M_ONGOING) {
        /* aviso: le toca jugar al jugador m->turn */
        m->awaiting  = 1;
        m->submitted = 0;
        pthread_cond_broadcast(&m->cv);

        /* espero la jugada del jugador de turno */
        while (m->status == M_ONGOING && !m->submitted) {
            pthread_cond_wait(&m->cv, &m->mtx);
        }
        if (!m->submitted) break;   /* salida de seguridad */

        /* el jugador ya escribio su marca en cells[]; registro la jugada */
        m->history[m->moves] = m->last_cell;
        m->moves++;
        m->awaiting = 0;

        /* reviso resultado tras la ultima jugada */
        m->status = evaluate_board(m->cells);
        if (m->status == M_ONGOING) {
            m->turn = 1 - m->turn;   /* le toca al otro jugador */
        }
        pthread_cond_broadcast(&m->cv);

        /* retardo entre jugadas, fuera de la seccion critica del tablero */
        pthread_mutex_unlock(&m->mtx);
        sleep_ms(g_cfg.turn_delay_ms);
        pthread_mutex_lock(&m->mtx);
    }
    /* despierto a los jugadores para que vean que la partida termino */
    pthread_cond_broadcast(&m->cv);
    pthread_mutex_unlock(&m->mtx);

    /* actualizo ELO y estadisticas de forma consistente */
    elo_apply_result(m);

    /* espero a que ambos jugadores salgan de la partida antes de liberar */
    pthread_mutex_lock(&m->mtx);
    while (m->players_done < 2) {
        pthread_cond_wait(&m->cv, &m->mtx);
    }
    pthread_mutex_unlock(&m->mtx);

    /* el tablero queda disponible de inmediato para otras partidas y aviso a los
     * jugadores que esperan en el matchmaking que se libero un tablero */
    registry_remove(m);
    pthread_mutex_lock(&g_mutex);
    g_free_boards++;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mutex);

    pthread_mutex_destroy(&m->mtx);
    pthread_cond_destroy(&m->cv);
    free(m);

    boards_dec();
    return NULL;
}
