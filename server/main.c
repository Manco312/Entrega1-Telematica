#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "logger.h"
#include "game.h"
#include "protocol.h"
#include "http_server.h"

/* =====================================================================
 * Estructura para pasar datos a hilo de cliente
 * ===================================================================== */
typedef struct {
    int  fd;
    char ip[INET6_ADDRSTRLEN];
    int  port;
} ClientArg;

/* =====================================================================
 * Hilos
 * ===================================================================== */

static void *client_thread(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    handle_client(ca->fd, ca->ip, ca->port);
    close(ca->fd);
    free(ca);
    return NULL;
}

/* Hilo del loop del juego: verifica timeouts de ataques cada segundo */
static void *game_loop_thread(void *arg) {
    (void)arg;
    while (1) {
        sleep(1);
        game_tick();
    }
    return NULL;
}

static void *http_thread(void *arg) {
    int port = *(int *)arg;
    free(arg);
    run_http_server(port);
    return NULL;
}

/* =====================================================================
 * Main
 * Uso: ./server <puerto> <archivoDeLogs>
 * ===================================================================== */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 8082 server.log\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Puerto invalido: %s (debe ser 1-65535)\n", argv[1]);
        return 1;
    }

    /* Inicializar logger */
    logger_init(argv[2]);
    log_info("=== Servidor CyberGame iniciando ===");
    log_info("Puerto juego: %d | Puerto HTTP: %d | Log: %s",
             port, port + 1, argv[2]);

    /* Inicializar estado del juego */
    game_init();

    /* Ignorar SIGPIPE para no caer cuando un cliente se desconecta */
    signal(SIGPIPE, SIG_IGN);

    /* Hilo del loop del juego */
    pthread_t gl_tid;
    if (pthread_create(&gl_tid, NULL, game_loop_thread, NULL) != 0) {
        log_error("No se pudo crear hilo del game loop");
        return 1;
    }
    pthread_detach(gl_tid);

    /* Hilo del servidor HTTP en puerto + 1 */
    int *http_port = malloc(sizeof(int));
    *http_port = port + 1;
    pthread_t ht_tid;
    if (pthread_create(&ht_tid, NULL, http_thread, http_port) != 0) {
        log_error("No se pudo crear hilo HTTP");
        free(http_port);
    } else {
        pthread_detach(ht_tid);
    }

    /* ================================================================
     * Crear socket de juego (TCP/SOCK_STREAM)
     * Justificacion: TCP garantiza entrega ordenada y fiable de mensajes
     * lo cual es esencial para el protocolo de juego orientado a estado.
     * ================================================================ */
    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        log_error("socket() fallido: %s", strerror(errno));
        return 1;
    }

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind() fallido en puerto %d: %s", port, strerror(errno));
        close(srv_fd);
        return 1;
    }

    if (listen(srv_fd, 10) < 0) {
        log_error("listen() fallido: %s", strerror(errno));
        close(srv_fd);
        return 1;
    }

    log_info("Servidor de juego listo. Esperando conexiones...");
    log_info("Variables de entorno: IDENTITY_HOST=%s IDENTITY_PORT=%s",
             getenv("IDENTITY_HOST") ? getenv("IDENTITY_HOST") : "localhost",
             getenv("IDENTITY_PORT") ? getenv("IDENTITY_PORT") : "9090");

    /* ================================================================
     * Loop principal: aceptar conexiones y crear hilo por cliente
     * Soporta multiples clientes simultaneos sin bloquear.
     * ================================================================ */
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int cli_fd = accept(srv_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) {
            log_error("accept() fallido: %s", strerror(errno));
            continue;
        }

        ClientArg *ca = malloc(sizeof(ClientArg));
        if (!ca) {
            log_error("malloc() fallido para ClientArg");
            close(cli_fd);
            continue;
        }
        ca->fd = cli_fd;
        inet_ntop(AF_INET, &cli_addr.sin_addr, ca->ip, sizeof(ca->ip));
        ca->port = ntohs(cli_addr.sin_port);

        log_info("Nueva conexion de %s:%d", ca->ip, ca->port);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, ca) != 0) {
            log_error("pthread_create() fallido: %s", strerror(errno));
            close(cli_fd);
            free(ca);
        } else {
            pthread_detach(tid);
        }
    }

    close(srv_fd);
    logger_close();
    return 0;
}
