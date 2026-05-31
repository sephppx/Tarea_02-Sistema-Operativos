#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "cmatch.h"

/* numero aleatorio en [0,1) usando rand_r para que sea seguro entre threads */
static double rand_double(unsigned int *seed) {
    return rand_r(seed) / (RAND_MAX + 1.0);
}

/*
 * Busca el mejor oponente para 'self' entre los jugadores que estan en la cola.
 * Reglas: la diferencia de ELO no puede superar MAX_ELO_DIFF y, si hay varios
 * candidatos validos, gana el que lleva mas tiempo esperando (menor wait_seq).
 * Debe llamarse con g_mutex tomado.
 */
static Player *find_best_partner(Player *self) {
    Player *best = NULL;
    for (int i = 0; i < g_cfg.n_players; i++) {
        Player *p = &g_players[i];
        if (p == self) continue;
        if (!p->in_queue) continue;
        if (fabs(p->elo - self->elo) > g_cfg.max_elo_diff) continue;

        /* entre los validos elijo al que llego primero a la cola */
        if (best == NULL || p->wait_seq < best->wait_seq) {
            best = p;
        }
    }
    return best;
}

/* crea e inicializa una partida nueva entre dos jugadores. Con g_mutex tomado. */
static Match *create_match(Player *a, Player *b) {
    Match *m = calloc(1, sizeof(Match));
    if (!m) {
        perror("calloc match");
        exit(EXIT_FAILURE);
    }
    m->game_id    = g_game_counter++;
    m->players[0] = a;
    m->players[1] = b;
    m->turn       = 0;            /* parte jugando players[0] */
    m->status     = M_ONGOING;
    pthread_mutex_init(&m->mtx, NULL);
    pthread_cond_init(&m->cv, NULL);
    return m;
}

/*
 * Fase de matchmaking. El jugador entra a la cola y se queda esperando (sin
 * busy-waiting, bloqueado en la condicion) hasta que pasa una de tres cosas:
 *   1. encuentra un oponente y consigue un tablero libre -> es el "anfitrion",
 *   2. otro jugador lo elige a el como oponente          -> es el "invitado",
 *   3. se pidio terminar el programa                     -> retorna 0.
 * Devuelve 1 si entro a una partida (deja la partida en *out y si es anfitrion
 * en *is_host), 0 si debe terminar.
 */
static int find_match(Player *self, Match **out, int *is_host) {
    pthread_mutex_lock(&g_mutex);

    self->assigned = NULL;
    self->in_queue = 1;
    self->state    = ST_WAITING;
    self->wait_seq = g_wait_counter++;
    pthread_cond_broadcast(&g_cv);   /* aviso que llego un candidato nuevo */

    for (;;) {
        /* caso 2: alguien me eligio como oponente */
        if (self->assigned != NULL) {
            Match *m = self->assigned;
            self->assigned = NULL;
            self->state = ST_PLAYING;
            pthread_mutex_unlock(&g_mutex);
            *out = m;
            *is_host = 0;
            return 1;
        }

        /* caso 3: nos pidieron terminar */
        if (!g_running) {
            self->in_queue = 0;
            self->state = ST_DONE;
            pthread_cond_broadcast(&g_cv);
            pthread_mutex_unlock(&g_mutex);
            return 0;
        }

        /* caso 1: intento emparejarme yo mismo */
        Player *p = find_best_partner(self);
        if (p != NULL && g_free_boards > 0) {
            /* consegui oponente y tablero: reclamo a ambos */
            g_free_boards--;
            self->in_queue = 0;
            p->in_queue    = 0;
            self->state    = ST_PLAYING;
            p->state       = ST_PLAYING;

            Match *m = create_match(self, p);
            p->assigned = m;                 /* le aviso al invitado cual es su partida */
            pthread_cond_broadcast(&g_cv);   /* lo despierto */
            pthread_mutex_unlock(&g_mutex);
            *out = m;
            *is_host = 1;
            return 1;
        }

        /* no hay con quien jugar o no hay tablero libre: espero sin busy-waiting */
        pthread_cond_wait(&g_cv, &g_mutex);
    }
}

/* devuelve una casilla vacia al azar del tablero */
static int random_empty_cell(Match *m, unsigned int *seed) {
    int empties[BOARD_CELLS];
    int n = 0;
    for (int i = 0; i < BOARD_CELLS; i++) {
        if (m->cells[i] == 0) empties[n++] = i;
    }
    return empties[rand_r(seed) % n];
}

/*
 * Parte del jugador dentro de la partida. Espera su turno, juega una casilla al
 * azar y sigue hasta que la partida termina. Se sincroniza con el tablero (y el
 * otro jugador) mediante el mutex y la condicion propios de la partida.
 */
static void play_match(Player *self, Match *m, unsigned int *seed) {
    int me = (m->players[0] == self) ? 0 : 1;
    int my_mark = me + 1;   /* 1 o 2 */

    pthread_mutex_lock(&m->mtx);
    while (m->status == M_ONGOING) {
        if (m->awaiting && m->turn == me && !m->submitted) {
            int cell = random_empty_cell(m, seed);
            m->cells[cell] = my_mark;
            m->last_cell   = cell;
            m->submitted   = 1;
            pthread_cond_broadcast(&m->cv);
        }
        pthread_cond_wait(&m->cv, &m->mtx);
    }
    /* aviso que termine de usar la partida */
    m->players_done++;
    pthread_cond_broadcast(&m->cv);
    pthread_mutex_unlock(&m->mtx);
}

/*
 * Funcion principal del thread de cada jugador: busca partida, juega y al
 * terminar decide autonomamente si reingresa a la cola (segun
 * REENTER_PROBABILITY) o si termina su ejecucion.
 */
void *player_thread(void *arg) {
    Player *self = (Player *)arg;
    unsigned int seed = (unsigned int)(time(NULL) ^ (self->id * 2654435761u));

    for (;;) {
        Match *m;
        int is_host;
        if (!find_match(self, &m, &is_host)) {
            break;   /* shutdown */
        }

        if (is_host) {
            /* como anfitrion lanzo el thread del tablero de esta partida */
            boards_inc();
            pthread_t bt;
            if (pthread_create(&bt, NULL, board_thread, m) != 0) {
                perror("pthread_create board");
                exit(EXIT_FAILURE);
            }
            pthread_detach(bt);
        }

        play_match(self, m, &seed);
        /* el tablero se encarga del ELO/estadisticas y de liberar la partida */

        /* si ya nos pidieron terminar, no reingreso */
        if (!g_running) break;

        /* decido autonomamente si vuelvo a la cola o me retiro */
        if (rand_double(&seed) >= g_cfg.reenter_prob) {
            break;
        }
    }

    pthread_mutex_lock(&g_mutex);
    self->state    = ST_DONE;
    self->in_queue = 0;
    pthread_mutex_unlock(&g_mutex);
    return NULL;
}
