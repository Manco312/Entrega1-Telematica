#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * protocol.h - Manejador del protocolo CGSP (CyberGame Simulation Protocol)
 *
 * Cada conexion de cliente ejecuta handle_client() en su propio hilo.
 * La funcion implementa la maquina de estados:
 *
 *   [STATE_AUTH]  -> LOGIN -> [STATE_LOBBY]
 *   [STATE_LOBBY] -> JOIN_GAME / CREATE_GAME / START_GAME -> [STATE_GAME]
 *   [STATE_GAME]  -> MOVE / SCAN / ATTACK / MITIGATE / STATUS / QUIT
 *
 * Todos los mensajes son texto terminado en '\n'.
 */
void handle_client(int sockfd, const char *ip, int port);

#endif /* PROTOCOL_H */
