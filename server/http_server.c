#include "http_server.h"
#include "game.h"
#include "logger.h"
#include "identity_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define HTTP_BUF 8192

/* =====================================================================
 * Paginas HTML embebidas
 * ===================================================================== */

static const char *HTML_LOGIN =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><title>CyberSim Login</title>"
    "<style>"
    "* { box-sizing: border-box; margin: 0; padding: 0; }"
    "body { font-family: 'Courier New', monospace; background: #0a0a0a;"
    "       color: #00ff41; display: flex; justify-content: center;"
    "       align-items: center; height: 100vh; }"
    ".card { background: #111; border: 1px solid #00ff41; padding: 40px;"
    "        min-width: 340px; border-radius: 4px; }"
    "h1 { text-align: center; font-size: 24px; margin-bottom: 8px; }"
    ".sub { text-align: center; color: #888; font-size: 12px; margin-bottom: 28px; }"
    "label { display: block; margin-bottom: 4px; font-size: 12px; color: #aaa; }"
    "input { display: block; width: 100%; padding: 10px; margin-bottom: 14px;"
    "        background: #0a0a0a; color: #00ff41; border: 1px solid #00ff41;"
    "        border-radius: 2px; font-family: monospace; font-size: 14px; }"
    "button { width: 100%; padding: 12px; background: #00ff41; color: #0a0a0a;"
    "         border: none; cursor: pointer; font-weight: bold; font-size: 14px;"
    "         border-radius: 2px; font-family: monospace; }"
    "button:hover { background: #00cc33; }"
    "</style></head>"
    "<body><div class='card'>"
    "<h1>[ CYBERSIM ]</h1>"
    "<p class='sub'>Sistema de Simulacion de Ciberseguridad</p>"
    "<form method='POST' action='/login'>"
    "<label>USUARIO</label>"
    "<input type='text' name='username' placeholder='attacker1 / defender1' required>"
    "<label>CONTRASENA</label>"
    "<input type='password' name='password' placeholder='pass123 / pass456' required>"
    "<button type='submit'>>> ACCEDER</button>"
    "</form></div></body></html>\r\n";

static const char *HTML_401 =
    "HTTP/1.0 401 Unauthorized\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><title>Acceso Denegado</title>"
    "<style>body{font-family:monospace;background:#0a0a0a;color:#ff4444;"
    "display:flex;justify-content:center;align-items:center;height:100vh;}"
    ".c{text-align:center}a{color:#00ff41}</style></head>"
    "<body><div class='c'><h1>[ ACCESO DENEGADO ]</h1>"
    "<p>Credenciales invalidas.</p><br><a href='/'>Reintentar</a></div></body></html>\r\n";

static const char *HTML_503 =
    "HTTP/1.0 503 Service Unavailable\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><body style='font-family:monospace;background:#0a0a0a;color:#ff9900'>"
    "<h1>Servicio de identidad no disponible</h1>"
    "<a href='/' style='color:#00ff41'>Reintentar</a></body></html>\r\n";

static const char *HTML_404 =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body style='font-family:monospace;background:#0a0a0a;color:#ff4444'>"
    "<h1>404 Not Found</h1></body></html>\r\n";

/* =====================================================================
 * Utilitarios HTTP
 * ===================================================================== */

/* Decodifica URL-encoding en un string (in-place) */
static void url_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            char hex[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' '; r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/* Extrae el valor de un campo "key=val&..." del body */
static int extract_field(const char *body, const char *key, char *out, int out_sz) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return 0;
    p += strlen(search);
    const char *end = strchr(p, '&');
    int len = end ? (int)(end - p) : (int)strlen(p);
    if (len >= out_sz) len = out_sz - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    url_decode(out);
    return 1;
}

/* Obtener el valor de un header en la request */
static int get_header_value(const char *request, const char *header_name,
                             char *out, int out_sz) {
    char needle[128];
    snprintf(needle, sizeof(needle), "%s:", header_name);
    const char *p = strstr(request, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ') p++;
    const char *end = strpbrk(p, "\r\n");
    int len = end ? (int)(end - p) : (int)strlen(p);
    if (len >= out_sz) len = out_sz - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return 1;
}

/* =====================================================================
 * Construir pagina de partidas activas (tras login exitoso)
 * ===================================================================== */
static void send_games_page(int fd, const char *username, const char *role,
                             const char *ip, int port) {
    char games_buf[1024];
    game_list(games_buf, sizeof(games_buf));

    char resp[HTTP_BUF];
    snprintf(resp, sizeof(resp),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!DOCTYPE html><html><head><title>CyberSim - Partidas</title>"
        "<style>"
        "body{font-family:monospace;background:#0a0a0a;color:#00ff41;padding:20px;}"
        "h1{color:#00ff41;} h2{color:#00cc33;font-size:16px;}"
        ".info{color:#888;font-size:12px;margin-bottom:20px;}"
        ".card{background:#111;border:1px solid #00ff41;padding:20px;margin:10px 0;"
        "      border-radius:4px;max-width:600px;}"
        "pre{background:#0a0a0a;border:1px solid #333;padding:12px;color:#aaffaa;"
        "    border-radius:2px;overflow-x:auto;}"
        "a{color:#00ff41;} .role-A{color:#ff4400;} .role-D{color:#0066ff;}"
        ".warn{color:#ffaa00;font-size:13px;}"
        "</style></head><body>"
        "<h1>[ CYBERSIM ] Bienvenido, <span class='role-%c'>%s</span></h1>"
        "<p class='info'>Rol: <strong>%s</strong> | "
        "Conectate con el cliente de juego para participar.</p>"
        "<div class='card'>"
        "<h2>Partidas Activas</h2>"
        "<pre>%s</pre>"
        "</div>"
        "<div class='card'>"
        "<h2>Como conectarse</h2>"
        "<pre># Cliente Python:\npython game_client.py &lt;servidor&gt; &lt;puerto_juego&gt;\n\n"
        "# Cliente Java:\njava GameClient &lt;servidor&gt; &lt;puerto_juego&gt;</pre>"
        "<p class='warn'>El puerto del juego es el del servidor HTTP menos 1.</p>"
        "</div>"
        "<br><a href='/'>Cerrar sesion</a></body></html>\r\n",
        role[0], username, role, games_buf);

    send(fd, resp, strlen(resp), 0);
    log_response(ip, port, "200 OK games page");
}

/* =====================================================================
 * Manejar una conexion HTTP
 * ===================================================================== */
static void handle_http_conn(int fd, const char *ip, int port) {
    char buf[HTTP_BUF] = {0};
    int  n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    /* Parsear linea de request */
    char method[16] = {0}, path[256] = {0}, proto[16] = {0};
    if (sscanf(buf, "%15s %255s %15s", method, path, proto) < 2) {
        send(fd, HTML_404, strlen(HTML_404), 0);
        return;
    }

    /* Log de la primera linea de la peticion */
    char req_line[300];
    snprintf(req_line, sizeof(req_line), "%s %s %s", method, path, proto);
    log_request(ip, port, req_line);

    /* ---- GET / ---- */
    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0))
    {
        send(fd, HTML_LOGIN, strlen(HTML_LOGIN), 0);
        log_response(ip, port, "200 OK login page");
        return;
    }

    /* ---- GET /api/games ---- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/games") == 0) {
        char games_buf[1024];
        game_list(games_buf, sizeof(games_buf));
        /* Remover newline final para JSON */
        char *nl = strpbrk(games_buf, "\r\n");
        if (nl) *nl = '\0';

        char resp[2048];
        snprintf(resp, sizeof(resp),
                 "HTTP/1.0 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "{\"games_raw\":\"%s\"}\r\n", games_buf);
        send(fd, resp, strlen(resp), 0);
        log_response(ip, port, "200 OK /api/games");
        return;
    }

    /* ---- POST /login ---- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/login") == 0) {
        /* Obtener Content-Length */
        char cl_str[32] = {0};
        int  content_len = 0;
        if (get_header_value(buf, "Content-Length", cl_str, sizeof(cl_str))) {
            content_len = atoi(cl_str);
        }

        /* Cuerpo esta despues de \r\n\r\n */
        char *body = strstr(buf, "\r\n\r\n");
        if (!body) {
            send(fd, HTML_401, strlen(HTML_401), 0);
            return;
        }
        body += 4;

        /* Si el body no llego completo, leer mas */
        int body_received = (int)(n - (body - buf));
        if (content_len > body_received && content_len < HTTP_BUF - 1) {
            int extra = recv(fd, body + body_received,
                             content_len - body_received, 0);
            if (extra > 0) body[body_received + extra] = '\0';
        }

        char username[64] = {0}, password[64] = {0};
        extract_field(body, "username", username, sizeof(username));
        extract_field(body, "password", password, sizeof(password));

        if (!username[0] || !password[0]) {
            send(fd, HTML_401, strlen(HTML_401), 0);
            log_response(ip, port, "401 Empty credentials");
            return;
        }

        char role[32];
        int  rc = identity_auth(username, password, role, sizeof(role));
        if      (rc == -2) { send(fd, HTML_503, strlen(HTML_503), 0); log_response(ip, port, "503"); }
        else if (rc == -1) { send(fd, HTML_401, strlen(HTML_401), 0); log_response(ip, port, "401 Auth fail"); }
        else               { send_games_page(fd, username, role, ip, port); }
        return;
    }

    /* ---- Cualquier otra ruta ---- */
    send(fd, HTML_404, strlen(HTML_404), 0);
    log_response(ip, port, "404 Not Found");
}

/* =====================================================================
 * Loop principal del servidor HTTP
 * ===================================================================== */
void run_http_server(int port) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { log_error("HTTP socket() failed"); return; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("HTTP bind() failed en puerto %d", port);
        close(srv);
        return;
    }
    if (listen(srv, 10) < 0) {
        log_error("HTTP listen() failed");
        close(srv);
        return;
    }
    log_info("HTTP server escuchando en puerto %d", port);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr *)&cli, &clen);
        if (fd < 0) continue;

        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        int cport = ntohs(cli.sin_port);

        handle_http_conn(fd, ip, cport);
        close(fd);
    }
    close(srv);
}
