#ifndef GAME_H
#define GAME_H

#include <pthread.h>
#include <time.h>
#include <netinet/in.h>

/* =====================================================================
 * Constantes del juego
 * ===================================================================== */
#define MAP_WIDTH         20
#define MAP_HEIGHT        20
#define MAX_PLAYERS       60      /* slots totales de jugadores */
#define MAX_ROOMS         10
#define MAX_PLAYERS_ROOM  10      /* maximos jugadores por sala */
#define MAX_RESOURCES      5
#define MITIGATE_TIMEOUT  30      /* segundos antes de destruir recurso */
#define GAME_DURATION    300      /* duracion maxima de partida en segundos */

/* =====================================================================
 * Tipos enumerados
 * ===================================================================== */
typedef enum { ROLE_ATTACKER, ROLE_DEFENDER }          PlayerRole;
typedef enum { STATE_AUTH, STATE_LOBBY, STATE_GAME }   ClientState;
typedef enum { RES_SAFE, RES_ATTACKED, RES_DESTROYED } ResourceState;
typedef enum { ROOM_WAITING, ROOM_ACTIVE, ROOM_DONE }  RoomState;

/* =====================================================================
 * Estructuras de datos
 * ===================================================================== */

typedef struct {
    int           id;
    int           x, y;
    ResourceState state;
    time_t        attack_start;
    char          attacker[64];  /* nombre del atacante */
} Resource;

typedef struct {
    int         id;
    char        username[64];
    PlayerRole  role;
    ClientState state;
    int         x, y;           /* posicion en el mapa */
    int         sockfd;
    int         room_id;        /* -1 si no esta en sala */
    char        ip[INET6_ADDRSTRLEN];
    int         port;
    int         active;         /* 1 si slot en uso */
} Player;

typedef struct {
    int       id;
    RoomState state;
    int       player_ids[MAX_PLAYERS_ROOM];
    int       player_count;
    Resource  resources[MAX_RESOURCES];
    int       resource_count;
    time_t    start_time;
} Room;

typedef struct {
    Player          players[MAX_PLAYERS];
    Room            rooms[MAX_ROOMS];
    int             room_count;
    pthread_mutex_t mutex;
} GameState;

extern GameState gs;

/* =====================================================================
 * API publica
 * NOTA: game_get_player() y game_get_room() son funciones internas,
 *       deben llamarse MIENTRAS se tiene el mutex tomado.
 * ===================================================================== */

void    game_init(void);

/* Crea un jugador nuevo para la conexion entrante */
Player *game_new_player(int sockfd, const char *ip, int port);

/* Remueve un jugador (lo desloguea de su sala si corresponde) */
void    game_remove_player(int pid);

/* Obtener jugador por ID (sin locking - llamar con mutex) */
Player *game_get_player(int pid);

/* Obtener sala por ID (sin locking - llamar con mutex) */
Room   *game_get_room(int rid);

/* Crear sala (llamar con mutex tomado) */
Room   *game_create_room(void);

/* Agregar jugador a sala (maneja su propio locking) */
int     game_add_player_to_room(int pid, int rid);

/* Iniciar partida: retorna 0=ok, -1=estado invalido, -2=faltan roles */
int     game_start_room(int rid);

/* Mover jugador: 0=ok, -1=dir invalida, -2=fuera de limites */
int     game_move(int pid, const char *dir, int *nx, int *ny);

/* Escanear celda actual: 1=recurso encontrado, 0=vacio, -1=error */
int     game_scan(int pid, int *res_id, int *rx, int *ry);

/* Atacar recurso: 0=ok, -1=error, -2=no esta ahi, -3=ya atacado, -4=no es atacante */
int     game_attack(int pid, int resource_id);

/* Mitigar ataque: 0=ok, -1=error, -2=no esta ahi, -3=no atacado, -4=no es defensor */
int     game_mitigate(int pid, int resource_id);

/* Enviar mensaje a un jugador especifico */
void    game_send(int pid, const char *msg);

/* Broadcast a todos en una sala, opcionalmente excluyendo a exclude_pid (-1 para incluir todos) */
void    game_broadcast(int room_id, const char *msg, int exclude_pid);

/* Enviar GAME_START personalizado a cada jugador de la sala (defensores ven recursos, atacantes no) */
void    game_broadcast_start(int room_id);

/* Llenar buffer con lista de partidas activas (protocolo GAMES ...) */
void    game_list(char *buf, size_t sz);

/* Llenar buffer con estado de la sala (protocolo STATUS ...) */
void    game_status(int room_id, char *buf, size_t sz);

/* Llamado por el hilo del loop del juego cada segundo */
void    game_tick(void);

#endif /* GAME_H */
