#include "protocol.h"
#include "game.h"
#include "logger.h"
#include "identity_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

#define BUFSIZE 2048

/* =====================================================================
 * Lectura de lineas del socket
 * Lee caracter a caracter hasta '\n' o cierre de conexion.
 * ===================================================================== */
static int read_line(int fd, char *buf, int sz) {
    int  i = 0;
    char c;
    while (i < sz - 1) {
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) return n; /* 0=cerrado, <0=error */
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static void trim(char *s) {
    int l = (int)strlen(s);
    while (l > 0 && (s[l-1]=='\n'||s[l-1]=='\r'||s[l-1]==' ')) s[--l]='\0';
}

/* =====================================================================
 * Procesamiento de comandos por estado
 * ===================================================================== */

static void process_command(int pid, const char *ip, int port, char *cmd) {
    char resp[BUFSIZE];

    /* Obtener estado actual del jugador */
    pthread_mutex_lock(&gs.mutex);
    Player *p = game_get_player(pid);
    if (!p) { pthread_mutex_unlock(&gs.mutex); return; }
    ClientState st = p->state;
    pthread_mutex_unlock(&gs.mutex);

    /* ----------------------------------------------------------------
     * ESTADO: AUTH - Solo acepta LOGIN
     * ---------------------------------------------------------------- */
    if (st == STATE_AUTH) {
        if (strncmp(cmd, "LOGIN ", 6) == 0) {
            char user[64] = {0}, pass[64] = {0};
            if (sscanf(cmd + 6, "%63s %63s", user, pass) != 2) {
                snprintf(resp, sizeof(resp),
                         "ERROR 400 Formato: LOGIN <usuario> <contrasena>\n");
                game_send(pid, resp);
                return;
            }
            char role[32];
            int rc = identity_auth(user, pass, role, sizeof(role));
            if (rc == -2) {
                snprintf(resp, sizeof(resp),
                         "ERROR 503 Servicio de identidad no disponible\n");
                game_send(pid, resp);
                return;
            }
            if (rc == -1) {
                snprintf(resp, sizeof(resp),
                         "ERROR 401 Credenciales invalidas\n");
                game_send(pid, resp);
                return;
            }
            /* Autenticacion exitosa */
            pthread_mutex_lock(&gs.mutex);
            p = game_get_player(pid);
            if (p) {
                strncpy(p->username, user, sizeof(p->username) - 1);
                p->role  = (strcmp(role, "ATTACKER") == 0) ? ROLE_ATTACKER : ROLE_DEFENDER;
                p->state = STATE_LOBBY;
            }
            pthread_mutex_unlock(&gs.mutex);
            snprintf(resp, sizeof(resp), "OK LOGIN role:%s\n", role);
            game_send(pid, resp);
        } else {
            game_send(pid, "ERROR 403 Debe autenticarse primero con LOGIN\n");
        }
        return;
    }

    /* ----------------------------------------------------------------
     * ESTADO: LOBBY - Gestion de partidas
     * ---------------------------------------------------------------- */
    if (st == STATE_LOBBY) {
        if (strcmp(cmd, "LIST_GAMES") == 0) {
            game_list(resp, sizeof(resp));
            game_send(pid, resp);

        } else if (strcmp(cmd, "CREATE_GAME") == 0) {
            pthread_mutex_lock(&gs.mutex);
            Room *room = game_create_room();
            int   rid  = room ? room->id : -1;
            pthread_mutex_unlock(&gs.mutex);

            if (rid < 0) {
                game_send(pid, "ERROR 503 No hay salas disponibles\n");
                return;
            }
            int rc = game_add_player_to_room(pid, rid);
            if (rc == 0) {
                snprintf(resp, sizeof(resp), "OK CREATE room:%d\n", rid);
                game_send(pid, resp);
            } else {
                game_send(pid, "ERROR 500 Error interno\n");
            }

        } else if (strncmp(cmd, "JOIN_GAME ", 10) == 0) {
            int rid = atoi(cmd + 10);
            int rc  = game_add_player_to_room(pid, rid);
            if      (rc == -1) game_send(pid, "ERROR 404 Sala no encontrada\n");
            else if (rc == -2) game_send(pid, "ERROR 410 La partida ya termino\n");
            else if (rc == -3) game_send(pid, "ERROR 409 Sala llena\n");
            else {
                pthread_mutex_lock(&gs.mutex);
                p = game_get_player(pid);
                const char *role_str = (p && p->role == ROLE_ATTACKER) ? "ATTACKER" : "DEFENDER";
                pthread_mutex_unlock(&gs.mutex);
                snprintf(resp, sizeof(resp), "OK JOIN room:%d role:%s\n", rid, role_str);
                game_send(pid, resp);
            }

        } else if (strcmp(cmd, "START_GAME") == 0) {
            pthread_mutex_lock(&gs.mutex);
            p = game_get_player(pid);
            int rid = p ? p->room_id : -1;
            pthread_mutex_unlock(&gs.mutex);

            if (rid < 0) {
                game_send(pid, "ERROR 412 No esta en ninguna sala\n");
                return;
            }
            int rc = game_start_room(rid);
            if (rc == -1) {
                game_send(pid, "ERROR 409 La sala no esta en estado de espera\n");
            } else if (rc == -2) {
                game_send(pid, "ERROR 412 Se necesita al menos 1 atacante y 1 defensor\n");
            } else {
                /* Enviar GAME_START personalizado a cada jugador */
                game_broadcast_start(rid);
                log_info("Partida %d iniciada", rid);
            }

        } else if (strcmp(cmd, "QUIT") == 0) {
            game_send(pid, "OK QUIT\n");
            /* El loop en handle_client detectara el cierre */

        } else {
            game_send(pid, "ERROR 400 Comando desconocido en lobby\n");
        }
        return;
    }

    /* ----------------------------------------------------------------
     * ESTADO: GAME - Acciones de juego
     * ---------------------------------------------------------------- */
    if (st == STATE_GAME) {
        if (strncmp(cmd, "MOVE ", 5) == 0) {
            const char *dir = cmd + 5;
            int nx, ny;
            int rc = game_move(pid, dir, &nx, &ny);

            if      (rc == -1) game_send(pid, "ERROR 400 Direccion invalida. Use NORTH/SOUTH/EAST/WEST\n");
            else if (rc == -2) game_send(pid, "ERROR 403 Fuera de los limites del mapa\n");
            else {
                snprintf(resp, sizeof(resp), "OK MOVE pos:%d,%d\n", nx, ny);
                game_send(pid, resp);

                /* Broadcast del movimiento a otros jugadores en la sala */
                pthread_mutex_lock(&gs.mutex);
                p = game_get_player(pid);
                char uname[64] = {0};
                const char *role_str = "UNKNOWN";
                int room_id = -1;
                if (p) {
                    strncpy(uname, p->username, 63);
                    role_str = (p->role == ROLE_ATTACKER) ? "ATTACKER" : "DEFENDER";
                    room_id  = p->room_id;
                }
                pthread_mutex_unlock(&gs.mutex);

                char bcast[256];
                snprintf(bcast, sizeof(bcast),
                         "PLAYER_MOVED player:%s role:%s pos:%d,%d\n",
                         uname, role_str, nx, ny);
                game_broadcast(room_id, bcast, pid);
            }

        } else if (strcmp(cmd, "SCAN") == 0) {
            int res_id, rx, ry;
            int rc = game_scan(pid, &res_id, &rx, &ry);
            if      (rc == 1)  snprintf(resp, sizeof(resp), "SCAN_RESULT FOUND resource_id:%d pos:%d,%d\n", res_id, rx, ry);
            else if (rc == 0)  snprintf(resp, sizeof(resp), "SCAN_RESULT EMPTY\n");
            else               snprintf(resp, sizeof(resp), "ERROR 500 Error interno\n");
            game_send(pid, resp);

        } else if (strncmp(cmd, "ATTACK ", 7) == 0) {
            int res_id = atoi(cmd + 7);
            int rc     = game_attack(pid, res_id);

            if      (rc == -4) game_send(pid, "ERROR 403 Solo los atacantes pueden atacar\n");
            else if (rc == -2) game_send(pid, "ERROR 400 No estas en la ubicacion del recurso\n");
            else if (rc == -3) game_send(pid, "ERROR 409 El recurso ya esta bajo ataque\n");
            else if (rc == -1) game_send(pid, "ERROR 404 Recurso no encontrado o destruido\n");
            else {
                snprintf(resp, sizeof(resp), "OK ATTACK resource_id:%d\n", res_id);
                game_send(pid, resp);

                /* Notificar a todos en la sala (defensores deben responder) */
                pthread_mutex_lock(&gs.mutex);
                p = game_get_player(pid);
                char uname[64] = {0};
                int px = 0, py = 0, room_id = -1;
                if (p) {
                    strncpy(uname, p->username, 63);
                    px = p->x; py = p->y;
                    room_id = p->room_id;
                }
                pthread_mutex_unlock(&gs.mutex);

                char bcast[256];
                snprintf(bcast, sizeof(bcast),
                         "NOTIFY_ATTACK resource_id:%d pos:%d,%d attacker:%s timeout:%d\n",
                         res_id, px, py, uname, MITIGATE_TIMEOUT);
                game_broadcast(room_id, bcast, -1); /* incluir al atacante */
            }

        } else if (strncmp(cmd, "MITIGATE ", 9) == 0) {
            int res_id = atoi(cmd + 9);
            int rc     = game_mitigate(pid, res_id);

            if      (rc == -4) game_send(pid, "ERROR 403 Solo los defensores pueden mitigar\n");
            else if (rc == -2) game_send(pid, "ERROR 400 No estas en la ubicacion del recurso\n");
            else if (rc == -3) game_send(pid, "ERROR 409 El recurso no esta bajo ataque\n");
            else if (rc == -1) game_send(pid, "ERROR 404 Recurso no encontrado\n");
            else {
                snprintf(resp, sizeof(resp), "OK MITIGATE resource_id:%d\n", res_id);
                game_send(pid, resp);

                pthread_mutex_lock(&gs.mutex);
                p = game_get_player(pid);
                char uname[64] = {0};
                int room_id = -1;
                if (p) { strncpy(uname, p->username, 63); room_id = p->room_id; }
                pthread_mutex_unlock(&gs.mutex);

                char bcast[256];
                snprintf(bcast, sizeof(bcast),
                         "NOTIFY_MITIGATED resource_id:%d defender:%s\n",
                         res_id, uname);
                game_broadcast(room_id, bcast, -1);
            }

        } else if (strcmp(cmd, "STATUS") == 0) {
            pthread_mutex_lock(&gs.mutex);
            p = game_get_player(pid);
            int room_id = p ? p->room_id : -1;
            pthread_mutex_unlock(&gs.mutex);
            game_status(room_id, resp, sizeof(resp));
            game_send(pid, resp);

        } else if (strcmp(cmd, "QUIT") == 0) {
            game_send(pid, "OK QUIT\n");

        } else {
            game_send(pid, "ERROR 400 Comando desconocido en partida\n");
        }
        return;
    }
}

/* =====================================================================
 * Hilo por cliente
 * ===================================================================== */

void handle_client(int sockfd, const char *ip, int port) {
    Player *p = game_new_player(sockfd, ip, port);
    if (!p) {
        const char *err = "ERROR 503 Servidor lleno\n";
        send(sockfd, err, strlen(err), 0);
        return;
    }
    int pid = p->id;
    log_info("Cliente pid=%d conectado desde %s:%d", pid, ip, port);

    char buf[BUFSIZE];
    while (1) {
        int n = read_line(sockfd, buf, sizeof(buf));
        if (n <= 0) break; /* cliente desconectado */

        trim(buf);
        if (strlen(buf) == 0) continue;

        log_request(ip, port, buf);
        process_command(pid, ip, port, buf);

        /* Si el cliente envio QUIT, cerrar la conexion */
        if (strcmp(buf, "QUIT") == 0) break;
    }

    /* Notificar desconexion a compañeros de sala */
    pthread_mutex_lock(&gs.mutex);
    p = game_get_player(pid);
    int  room_id = -1;
    char uname[64] = {0};
    if (p) {
        room_id = p->room_id;
        strncpy(uname, p->username, 63);
    }
    pthread_mutex_unlock(&gs.mutex);

    if (room_id >= 0 && uname[0] != '\0') {
        char disc[128];
        snprintf(disc, sizeof(disc), "NOTIFY_DISCONNECT player:%s\n", uname);
        game_broadcast(room_id, disc, pid);
    }

    game_remove_player(pid);
    log_info("Cliente pid=%d (%s:%d) desconectado", pid, ip, port);
}
