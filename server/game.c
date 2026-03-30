#include "game.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

/* Estado global del juego */
GameState gs;

/* Posiciones fijas de los recursos criticos en el mapa */
static const int RES_X[] = { 5, 14 };
static const int RES_Y[] = { 5, 14 };
static const int RES_COUNT = 2;

/* =====================================================================
 * Inicializacion
 * ===================================================================== */

void game_init(void) {
    memset(&gs, 0, sizeof(gs));
    pthread_mutex_init(&gs.mutex, NULL);
    srand((unsigned)time(NULL));
    log_info("Juego inicializado. Mapa: %dx%d, Recursos: %d en (%d,%d) y (%d,%d)",
             MAP_WIDTH, MAP_HEIGHT, RES_COUNT,
             RES_X[0], RES_Y[0], RES_X[1], RES_Y[1]);
}

/* =====================================================================
 * Gestion de jugadores
 * ===================================================================== */

static Player *alloc_player_slot(void) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!gs.players[i].active) {
            memset(&gs.players[i], 0, sizeof(Player));
            gs.players[i].id      = i;
            gs.players[i].active  = 1;
            gs.players[i].room_id = -1;
            gs.players[i].state   = STATE_AUTH;
            return &gs.players[i];
        }
    }
    return NULL;
}

Player *game_new_player(int sockfd, const char *ip, int port) {
    pthread_mutex_lock(&gs.mutex);
    Player *p = alloc_player_slot();
    if (p) {
        p->sockfd = sockfd;
        strncpy(p->ip, ip, sizeof(p->ip) - 1);
        p->port = port;
    }
    pthread_mutex_unlock(&gs.mutex);
    return p;
}

/* Llamar CON mutex tomado */
Player *game_get_player(int pid) {
    if (pid < 0 || pid >= MAX_PLAYERS) return NULL;
    return gs.players[pid].active ? &gs.players[pid] : NULL;
}

void game_remove_player(int pid) {
    pthread_mutex_lock(&gs.mutex);
    Player *p = game_get_player(pid);
    if (p) {
        if (p->room_id >= 0) {
            Room *room = game_get_room(p->room_id);
            if (room) {
                for (int i = 0; i < room->player_count; i++) {
                    if (room->player_ids[i] == pid) {
                        room->player_ids[i] = room->player_ids[--room->player_count];
                        break;
                    }
                }
            }
        }
        p->active = 0;
    }
    pthread_mutex_unlock(&gs.mutex);
}

/* =====================================================================
 * Gestion de salas
 * ===================================================================== */

/* Llamar CON mutex tomado */
Room *game_create_room(void) {
    if (gs.room_count >= MAX_ROOMS) return NULL;

    Room *room = &gs.rooms[gs.room_count];
    memset(room, 0, sizeof(Room));
    room->id             = gs.room_count++;
    room->state          = ROOM_WAITING;
    room->resource_count = RES_COUNT;

    for (int i = 0; i < RES_COUNT; i++) {
        room->resources[i].id    = i;
        room->resources[i].x     = RES_X[i];
        room->resources[i].y     = RES_Y[i];
        room->resources[i].state = RES_SAFE;
    }
    return room;
}

/* Llamar CON mutex tomado */
Room *game_get_room(int rid) {
    for (int i = 0; i < gs.room_count; i++) {
        if (gs.rooms[i].id == rid) return &gs.rooms[i];
    }
    return NULL;
}

int game_add_player_to_room(int pid, int rid) {
    pthread_mutex_lock(&gs.mutex);
    Player *p    = game_get_player(pid);
    Room   *room = game_get_room(rid);
    int     ret  = 0;

    if (!p || !room)                          ret = -1;
    else if (room->state == ROOM_DONE)        ret = -2;
    else if (room->player_count >= MAX_PLAYERS_ROOM) ret = -3;
    else {
        p->room_id = rid;
        p->state   = STATE_LOBBY;
        p->x       = rand() % MAP_WIDTH;
        p->y       = rand() % MAP_HEIGHT;
        room->player_ids[room->player_count++] = pid;
    }

    pthread_mutex_unlock(&gs.mutex);
    return ret;
}

int game_start_room(int rid) {
    pthread_mutex_lock(&gs.mutex);
    Room *room = game_get_room(rid);
    int   ret  = 0;

    if (!room || room->state != ROOM_WAITING) {
        ret = -1;
    } else {
        int atk = 0, def = 0;
        for (int i = 0; i < room->player_count; i++) {
            Player *p = game_get_player(room->player_ids[i]);
            if (!p) continue;
            if (p->role == ROLE_ATTACKER) atk++;
            else                          def++;
        }
        if (atk < 1 || def < 1) {
            ret = -2;
        } else {
            room->state      = ROOM_ACTIVE;
            room->start_time = time(NULL);
            for (int i = 0; i < room->player_count; i++) {
                Player *p = game_get_player(room->player_ids[i]);
                if (p) p->state = STATE_GAME;
            }
        }
    }

    pthread_mutex_unlock(&gs.mutex);
    return ret;
}

/* =====================================================================
 * Acciones del juego
 * ===================================================================== */

int game_move(int pid, const char *dir, int *nx, int *ny) {
    pthread_mutex_lock(&gs.mutex);
    Player *p  = game_get_player(pid);
    int     ret = 0;

    if (!p || p->state != STATE_GAME) {
        ret = -1;
    } else {
        int x = p->x, y = p->y;
        if      (strcmp(dir, "NORTH") == 0) y--;
        else if (strcmp(dir, "SOUTH") == 0) y++;
        else if (strcmp(dir, "WEST")  == 0) x--;
        else if (strcmp(dir, "EAST")  == 0) x++;
        else                                 ret = -1;

        if (ret == 0) {
            if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
                ret = -2;
            } else {
                p->x = x; p->y = y;
                *nx = x;  *ny = y;
            }
        }
    }

    pthread_mutex_unlock(&gs.mutex);
    return ret;
}

int game_scan(int pid, int *res_id, int *rx, int *ry) {
    pthread_mutex_lock(&gs.mutex);
    Player *p   = game_get_player(pid);
    int     ret = 0;

    if (!p || p->state != STATE_GAME) {
        ret = -1;
    } else {
        Room *room = game_get_room(p->room_id);
        if (!room) {
            ret = -1;
        } else {
            for (int i = 0; i < room->resource_count; i++) {
                Resource *r = &room->resources[i];
                if (r->x == p->x && r->y == p->y && r->state != RES_DESTROYED) {
                    *res_id = r->id;
                    *rx     = r->x;
                    *ry     = r->y;
                    ret     = 1;
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&gs.mutex);
    return ret;
}

int game_attack(int pid, int resource_id) {
    pthread_mutex_lock(&gs.mutex);
    Player *p   = game_get_player(pid);
    int     ret = 0;

    if (!p || p->state != STATE_GAME || p->role != ROLE_ATTACKER) {
        ret = -4;
    } else {
        Room *room = game_get_room(p->room_id);
        if (!room || room->state != ROOM_ACTIVE) {
            ret = -1;
        } else {
            Resource *res = NULL;
            for (int i = 0; i < room->resource_count; i++) {
                if (room->resources[i].id == resource_id) {
                    res = &room->resources[i];
                    break;
                }
            }
            if (!res || res->state == RES_DESTROYED) ret = -1;
            else if (res->x != p->x || res->y != p->y)   ret = -2;
            else if (res->state == RES_ATTACKED)          ret = -3;
            else {
                res->state        = RES_ATTACKED;
                res->attack_start = time(NULL);
                strncpy(res->attacker, p->username, sizeof(res->attacker) - 1);
            }
        }
    }

    pthread_mutex_unlock(&gs.mutex);
    return ret;
}

int game_mitigate(int pid, int resource_id) {
    pthread_mutex_lock(&gs.mutex);
    Player *p   = game_get_player(pid);
    int     ret = 0;

    if (!p || p->state != STATE_GAME || p->role != ROLE_DEFENDER) {
        ret = -4;
    } else {
        Room *room = game_get_room(p->room_id);
        if (!room || room->state != ROOM_ACTIVE) {
            ret = -1;
        } else {
            Resource *res = NULL;
            for (int i = 0; i < room->resource_count; i++) {
                if (room->resources[i].id == resource_id) {
                    res = &room->resources[i];
                    break;
                }
            }
            if (!res)                                 ret = -1;
            else if (res->x != p->x || res->y != p->y) ret = -2;
            else if (res->state != RES_ATTACKED)       ret = -3;
            else {
                res->state = RES_SAFE;
                memset(res->attacker, 0, sizeof(res->attacker));
            }
        }
    }

    pthread_mutex_unlock(&gs.mutex);
    return ret;
}

/* =====================================================================
 * Comunicacion
 * PATRON: lock mutex -> copiar fd e info -> unlock -> enviar
 * Esto evita bloquear I/O mientras se tiene el mutex.
 * ===================================================================== */

void game_send(int pid, const char *msg) {
    pthread_mutex_lock(&gs.mutex);
    Player *p  = game_get_player(pid);
    int     fd = -1;
    char    ip[INET6_ADDRSTRLEN] = {0};
    int     port = 0;
    if (p) {
        fd = p->sockfd;
        strncpy(ip, p->ip, sizeof(ip) - 1);
        port = p->port;
    }
    pthread_mutex_unlock(&gs.mutex);

    if (fd >= 0) {
        send(fd, msg, strlen(msg), 0);
        log_response(ip, port, msg);
    }
}

void game_broadcast(int room_id, const char *msg, int exclude_pid) {
    int  fds[MAX_PLAYERS_ROOM];
    char ips[MAX_PLAYERS_ROOM][INET6_ADDRSTRLEN];
    int  ports[MAX_PLAYERS_ROOM];
    int  count = 0;

    pthread_mutex_lock(&gs.mutex);
    Room *room = game_get_room(room_id);
    if (room) {
        for (int i = 0; i < room->player_count && count < MAX_PLAYERS_ROOM; i++) {
            if (room->player_ids[i] == exclude_pid) continue;
            Player *p = game_get_player(room->player_ids[i]);
            if (p && p->active) {
                fds[count] = p->sockfd;
                strncpy(ips[count], p->ip, INET6_ADDRSTRLEN - 1);
                ports[count] = p->port;
                count++;
            }
        }
    }
    pthread_mutex_unlock(&gs.mutex);

    for (int i = 0; i < count; i++) {
        send(fds[i], msg, strlen(msg), 0);
        log_response(ips[i], ports[i], msg);
    }
}

void game_broadcast_start(int room_id) {
    typedef struct {
        int  fd;
        PlayerRole role;
        int  x, y;
        char ip[INET6_ADDRSTRLEN];
        int  port;
    } PInfo;

    PInfo pinfos[MAX_PLAYERS_ROOM];
    int   pcount = 0;
    char  res_str[256] = {0};

    pthread_mutex_lock(&gs.mutex);
    Room *room = game_get_room(room_id);
    if (!room) { pthread_mutex_unlock(&gs.mutex); return; }

    /* Construir string de recursos para defensores */
    char *rp  = res_str;
    int   rem = sizeof(res_str);
    for (int i = 0; i < room->resource_count; i++) {
        int n = snprintf(rp, rem, "%d,%d%s",
                         room->resources[i].x, room->resources[i].y,
                         (i < room->resource_count - 1) ? ";" : "");
        rp += n; rem -= n;
    }

    for (int i = 0; i < room->player_count && pcount < MAX_PLAYERS_ROOM; i++) {
        Player *p = game_get_player(room->player_ids[i]);
        if (!p) continue;
        pinfos[pcount].fd   = p->sockfd;
        pinfos[pcount].role = p->role;
        pinfos[pcount].x    = p->x;
        pinfos[pcount].y    = p->y;
        strncpy(pinfos[pcount].ip, p->ip, INET6_ADDRSTRLEN - 1);
        pinfos[pcount].port = p->port;
        pcount++;
    }
    pthread_mutex_unlock(&gs.mutex);

    /* Enviar mensaje diferente segun rol:
     * Defensor: recibe posiciones de recursos
     * Atacante: recibe "?" - debe explorar */
    char msg[512];
    for (int i = 0; i < pcount; i++) {
        if (pinfos[i].role == ROLE_DEFENDER) {
            snprintf(msg, sizeof(msg),
                     "GAME_START map:%dx%d resources:%s pos:%d,%d\n",
                     MAP_WIDTH, MAP_HEIGHT, res_str,
                     pinfos[i].x, pinfos[i].y);
        } else {
            snprintf(msg, sizeof(msg),
                     "GAME_START map:%dx%d resources:? pos:%d,%d\n",
                     MAP_WIDTH, MAP_HEIGHT,
                     pinfos[i].x, pinfos[i].y);
        }
        send(pinfos[i].fd, msg, strlen(msg), 0);
        log_response(pinfos[i].ip, pinfos[i].port, msg);
    }
}

void game_list(char *buf, size_t sz) {
    pthread_mutex_lock(&gs.mutex);

    int cnt = 0;
    for (int i = 0; i < gs.room_count; i++) {
        if (gs.rooms[i].state != ROOM_DONE) cnt++;
    }

    int   w   = snprintf(buf, sz, "GAMES %d", cnt);
    int   rem = (int)sz - w;
    char *ptr = buf + w;

    for (int i = 0; i < gs.room_count && rem > 0; i++) {
        Room *r = &gs.rooms[i];
        if (r->state == ROOM_DONE) continue;
        const char *st = (r->state == ROOM_ACTIVE) ? "ACTIVE" : "WAITING";
        int n = snprintf(ptr, rem, " id:%d players:%d status:%s",
                         r->id, r->player_count, st);
        ptr += n; rem -= n;
    }
    if (rem > 1) { *ptr++ = '\n'; *ptr = '\0'; }

    pthread_mutex_unlock(&gs.mutex);
}

void game_status(int room_id, char *buf, size_t sz) {
    pthread_mutex_lock(&gs.mutex);
    Room *room = game_get_room(room_id);

    if (!room) {
        snprintf(buf, sz, "ERROR 404 Room not found\n");
        pthread_mutex_unlock(&gs.mutex);
        return;
    }

    char *ptr = buf;
    int   rem = (int)sz;
    int   n   = snprintf(ptr, rem, "STATUS room:%d players:%d resources:",
                         room->id, room->player_count);
    ptr += n; rem -= n;

    for (int i = 0; i < room->resource_count && rem > 0; i++) {
        Resource *r  = &room->resources[i];
        const char *st = (r->state == RES_SAFE)      ? "SAFE"
                       : (r->state == RES_ATTACKED)   ? "ATTACKED"
                       :                                "DESTROYED";
        n = snprintf(ptr, rem, "id:%d,state:%s%s",
                     r->id, st,
                     (i < room->resource_count - 1) ? ";" : "");
        ptr += n; rem -= n;
    }
    if (rem > 1) { *ptr++ = '\n'; *ptr = '\0'; }

    pthread_mutex_unlock(&gs.mutex);
}

/* =====================================================================
 * Loop del juego (llamado cada segundo por hilo dedicado)
 * ===================================================================== */

void game_tick(void) {
    typedef struct { int room_id; char msg[256]; } Notif;
    Notif notifs[MAX_ROOMS * (MAX_RESOURCES + 1)];
    int   nc = 0;

    pthread_mutex_lock(&gs.mutex);
    time_t now = time(NULL);

    for (int ri = 0; ri < gs.room_count; ri++) {
        Room *room = &gs.rooms[ri];
        if (room->state != ROOM_ACTIVE) continue;

        int all_destroyed = 1;

        for (int j = 0; j < room->resource_count; j++) {
            Resource *r = &room->resources[j];
            if (r->state == RES_ATTACKED &&
                (now - r->attack_start) >= MITIGATE_TIMEOUT)
            {
                r->state = RES_DESTROYED;
                snprintf(notifs[nc].msg, 256,
                         "NOTIFY_DESTROYED resource_id:%d\n", r->id);
                notifs[nc].room_id = room->id;
                if (nc < (int)(sizeof(notifs)/sizeof(notifs[0])) - 1) nc++;
            }
            if (r->state != RES_DESTROYED) all_destroyed = 0;
        }

        if (all_destroyed && room->resource_count > 0) {
            room->state = ROOM_DONE;
            snprintf(notifs[nc].msg, 256, "GAME_OVER winner:ATTACKERS\n");
            notifs[nc].room_id = room->id;
            if (nc < (int)(sizeof(notifs)/sizeof(notifs[0])) - 1) nc++;
        } else if ((now - room->start_time) >= GAME_DURATION) {
            room->state = ROOM_DONE;
            snprintf(notifs[nc].msg, 256, "GAME_OVER winner:DEFENDERS\n");
            notifs[nc].room_id = room->id;
            if (nc < (int)(sizeof(notifs)/sizeof(notifs[0])) - 1) nc++;
        }
    }

    pthread_mutex_unlock(&gs.mutex);

    /* Broadcast fuera del mutex para no bloquear I/O bajo lock */
    for (int i = 0; i < nc; i++) {
        game_broadcast(notifs[i].room_id, notifs[i].msg, -1);
    }
}
