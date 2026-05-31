#ifndef CMATCH_H
#define CMATCH_H

#include <pthread.h>

/* cantidad de casillas del gato (3x3) */
#define BOARD_CELLS 9

/* estados posibles de un jugador dentro de la simulacion */
typedef enum {
    ST_WAITING,   /* esperando en la cola de matchmaking */
    ST_PLAYING,   /* jugando una partida */
    ST_DONE       /* el jugador termino su ejecucion */
} PlayerState;

/* resultado de una partida */
typedef enum {
    M_ONGOING,    /* todavia se esta jugando */
    M_P0_WIN,     /* gano players[0] */
    M_P1_WIN,     /* gano players[1] */
    M_DRAW        /* empate */
} MatchStatus;

/* parametros leidos desde el archivo .env */
typedef struct {
    int    n_players;
    int    k_boards;
    int    k_elo;
    int    max_elo_diff;
    int    turn_delay_ms;
    double reenter_prob;
    char   snapshot_path[256];
} Config;

typedef struct Match Match;

/* representa a un jugador. Cada uno corre en su propio thread */
typedef struct {
    int          id;
    double       elo;
    int          wins;
    int          losses;
    int          draws;
    PlayerState  state;
    long long    wait_seq;   /* numero de llegada a la cola: menor = lleva mas tiempo esperando */
    int          in_queue;   /* 1 si esta disponible para ser emparejado */
    Match       *assigned;   /* la partida en la que lo metio otro jugador (cuando es invitado) */
} Player;

/* una partida en curso. La comparten los 2 jugadores y el tablero */
struct Match {
    int          game_id;
    int          cells[BOARD_CELLS];   /* 0 vacia, 1 = marca de players[0], 2 = marca de players[1] */
    Player      *players[2];
    int          turn;                 /* 0 o 1: a quien le toca jugar */
    int          awaiting;             /* el tablero esta esperando la jugada del jugador de turno */
    int          submitted;            /* el jugador de turno ya dejo su jugada */
    int          last_cell;            /* ultima casilla jugada */
    int          history[BOARD_CELLS]; /* secuencia de casillas jugadas */
    int          moves;                /* cantidad de jugadas realizadas */
    MatchStatus  status;
    int          players_done;         /* cuantos jugadores ya salieron de la partida */
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    Match       *reg_next;             /* enlace para el registro de partidas activas */
};

/* ---- estado global compartido (definido en main.c) ---- */
extern Config          g_cfg;
extern Player         *g_players;
extern pthread_mutex_t g_mutex;        /* protege la "tabla de jugadores": cola, estado, elo, stats, tableros libres */
extern pthread_cond_t  g_cv;           /* se usa para esperar en el matchmaking */
extern int             g_free_boards;  /* cuenta los K tableros disponibles (protegido por g_mutex) */
extern long long       g_wait_counter; /* contador para el orden de llegada a la cola */
extern int             g_game_counter; /* asigna ids unicos a las partidas */
extern volatile int    g_running;      /* 0 cuando se pidio terminar (no se inician partidas nuevas) */

/* ---- config.c ---- */
int  config_load(const char *path, Config *cfg);
void config_print(const Config *cfg);

/* ---- elo.c ---- */
double elo_expected(double elo_a, double elo_b);
void   elo_apply_result(Match *m);

/* ---- game.c ---- */
void *board_thread(void *arg);

/* ---- player.c ---- */
void *player_thread(void *arg);

/* ---- monitor.c (interfaz de monitoreo pedida por el enunciado) ---- */
void player_stats(int player_id);
void current_matches(void);
void match_status(int game_id);
void registry_add(Match *m);
void registry_remove(Match *m);

/* ---- snapshot.c ---- */
int  snapshot_save(const char *path);
int  snapshot_load(const char *path);

/* ---- utilidades (main.c) ---- */
void sleep_ms(int ms);
void boards_inc(void);
void boards_dec(void);
void boards_wait_empty(void);

#endif /* CMATCH_H */
