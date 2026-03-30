#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

/*
 * http_server.h - Servidor HTTP basico para la interfaz web
 *
 * Corre en un hilo separado en (game_port + 1).
 * Implementa HTTP/1.0 con las siguientes rutas:
 *
 *   GET  /           -> Pagina de login HTML
 *   POST /login      -> Autenticar via servicio de identidad, mostrar partidas
 *   GET  /api/games  -> Lista de partidas en JSON
 *   *                -> 404
 *
 * Interpreta correctamente cabeceras HTTP y devuelve codigos de estado.
 */
void run_http_server(int port);

#endif /* HTTP_SERVER_H */
