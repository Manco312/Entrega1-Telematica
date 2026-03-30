#include "identity_client.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

/* Lee el hostname del servicio de identidad desde variable de entorno.
 * Nunca usa una IP hardcodeada: siempre resuelve por DNS. */
static const char *get_identity_host(void) {
    const char *h = getenv("IDENTITY_HOST");
    return h ? h : "localhost";
}

static const char *get_identity_port(void) {
    const char *p = getenv("IDENTITY_PORT");
    return p ? p : "9090";
}

int identity_auth(const char *username, const char *password,
                  char *role, int role_sz) {
    const char *host     = get_identity_host();
    const char *port_str = get_identity_port();

    /* Resolucion DNS - sin IPs hardcodeadas */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        log_error("identity_auth: getaddrinfo(%s:%s) -> %s",
                  host, port_str, gai_strerror(err));
        return -2;
    }

    /* Intentar conectar con cada resultado de DNS */
    int sock = -1;
    for (struct addrinfo *r = res; r != NULL; r = r->ai_next) {
        sock = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, r->ai_addr, r->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        log_error("identity_auth: no se pudo conectar a %s:%s", host, port_str);
        return -2;
    }

    /* Enviar peticion de autenticacion */
    char msg[512];
    snprintf(msg, sizeof(msg), "AUTH %s %s\n", username, password);
    if (send(sock, msg, strlen(msg), 0) < 0) {
        log_error("identity_auth: send() failed: %s", strerror(errno));
        close(sock);
        return -2;
    }

    /* Leer respuesta */
    char buf[256] = {0};
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);

    if (n <= 0) {
        log_error("identity_auth: sin respuesta del servicio de identidad");
        return -2;
    }

    /* Parsear respuesta: "OK ATTACKER\n" o "ERROR 401 ...\n" */
    if (strncmp(buf, "OK ", 3) == 0) {
        char *r = buf + 3;
        char *nl = strpbrk(r, "\r\n");
        if (nl) *nl = '\0';
        strncpy(role, r, role_sz - 1);
        role[role_sz - 1] = '\0';
        return 0;
    }

    return -1; /* credenciales invalidas */
}
