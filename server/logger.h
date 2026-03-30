#ifndef LOGGER_H
#define LOGGER_H

/*
 * logger.h - Logging thread-safe para el servidor de juego
 * Escribe a consola y a archivo simultaneamente.
 * Cada entrada incluye timestamp, nivel, y (si aplica) IP:puerto del cliente.
 */

void logger_init(const char *filename);
void logger_close(void);

/* Loguea una peticion entrante de un cliente */
void log_request(const char *ip, int port, const char *msg);

/* Loguea una respuesta enviada a un cliente */
void log_response(const char *ip, int port, const char *msg);

/* Mensajes informativos del servidor */
void log_info(const char *fmt, ...);

/* Mensajes de error del servidor */
void log_error(const char *fmt, ...);

#endif /* LOGGER_H */
