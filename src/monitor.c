#include <stdio.h>

#include "cmatch.h"

/*
 * Registro de las partidas que estan en curso. Es una lista enlazada simple
 * protegida por su propio mutex, asi el monitoreo no interfiere con la tabla de
 * jugadores ni con la sincronizacion de cada partida.
 */
static Match           *reg_head = NULL;
static pthread_mutex_t  reg_mtx  = PTHREAD_MUTEX_INITIALIZER;

void registry_add(Match *m) {
    pthread_mutex_lock(&reg_mtx);
    m->reg_next = reg_head;
    reg_head = m;
    pthread_mutex_unlock(&reg_mtx);
}

void registry_remove(Match *m) {
    pthread_mutex_lock(&reg_mtx);
    Match **pp = &reg_head;
    while (*pp) {
        if (*pp == m) {
            *pp = m->reg_next;
            break;
        }
        pp = &(*pp)->reg_next;
    }
    pthread_mutex_unlock(&reg_mtx);
}

/* convierte la marca de una casilla en un caracter para imprimir el tablero */
static char cell_char(int v) {
    if (v == 1) return 'X';
    if (v == 2) return 'O';
    return '.';
}

/* imprime partidas ganadas, perdidas y empatadas de un jugador */
void player_stats(int player_id) {
    if (player_id < 0 || player_id >= g_cfg.n_players) {
        printf("[player_stats] no existe el jugador %d\n", player_id);
        return;
    }

    pthread_mutex_lock(&g_mutex);
    Player *p = &g_players[player_id];
    const char *st = (p->state == ST_WAITING) ? "esperando"
                   : (p->state == ST_PLAYING) ? "jugando" : "terminado";
    printf("[player_stats] jugador %d | ELO=%.1f | estado=%s | ganadas=%d perdidas=%d empatadas=%d\n",
           p->id, p->elo, st, p->wins, p->losses, p->draws);
    pthread_mutex_unlock(&g_mutex);
}

/* imprime cuantas partidas se estan jugando y el detalle de cada una */
void current_matches(void) {
    pthread_mutex_lock(&reg_mtx);
    int count = 0;
    for (Match *m = reg_head; m; m = m->reg_next) count++;

    printf("[current_matches] partidas en curso: %d\n", count);
    for (Match *m = reg_head; m; m = m->reg_next) {
        pthread_mutex_lock(&m->mtx);
        printf("    match %d -> jugador %d (X) vs jugador %d (O)\n",
               m->game_id, m->players[0]->id, m->players[1]->id);
        pthread_mutex_unlock(&m->mtx);
    }
    pthread_mutex_unlock(&reg_mtx);
}

/* imprime el estado de una partida: jugadores, estado y jugadas ocurridas */
void match_status(int game_id) {
    pthread_mutex_lock(&reg_mtx);
    Match *found = NULL;
    for (Match *m = reg_head; m; m = m->reg_next) {
        if (m->game_id == game_id) {
            found = m;
            break;
        }
    }

    if (!found) {
        pthread_mutex_unlock(&reg_mtx);
        printf("[match_status] la partida %d no esta activa (no existe o ya termino)\n", game_id);
        return;
    }

    /* copio los datos bajo el lock de la partida para tener un estado consistente */
    pthread_mutex_lock(&found->mtx);
    int p0 = found->players[0]->id;
    int p1 = found->players[1]->id;
    int turn_player = found->players[found->turn]->id;
    MatchStatus status = found->status;
    int cells[BOARD_CELLS];
    int history[BOARD_CELLS];
    int moves = found->moves;
    for (int i = 0; i < BOARD_CELLS; i++) {
        cells[i] = found->cells[i];
        history[i] = found->history[i];
    }
    pthread_mutex_unlock(&found->mtx);
    pthread_mutex_unlock(&reg_mtx);

    const char *st = (status == M_ONGOING) ? "en curso"
                   : (status == M_P0_WIN)  ? "gano X"
                   : (status == M_P1_WIN)  ? "gano O" : "empate";

    printf("[match_status] match %d | jugador %d (X) vs jugador %d (O) | estado: %s\n",
           game_id, p0, p1, st);
    if (status == M_ONGOING) {
        printf("    turno de: jugador %d\n", turn_player);
    }

    printf("    tablero:\n");
    for (int r = 0; r < 3; r++) {
        printf("        %c %c %c\n",
               cell_char(cells[r * 3 + 0]),
               cell_char(cells[r * 3 + 1]),
               cell_char(cells[r * 3 + 2]));
    }

    printf("    jugadas (%d): ", moves);
    for (int i = 0; i < moves; i++) {
        printf("%d%s", history[i], (i + 1 < moves) ? " -> " : "");
    }
    printf("\n");
}
