#ifndef IDENTITY_CLIENT_H
#define IDENTITY_CLIENT_H

/*
 * identity_client.h - Cliente del servicio de identidad externo
 *
 * El servidor de juego NO almacena usuarios localmente.
 * Para autenticar, contacta al servicio de identidad por red usando DNS.
 *
 * Variables de entorno:
 *   IDENTITY_HOST  - hostname del servicio (default: "localhost")
 *   IDENTITY_PORT  - puerto del servicio  (default: "9090")
 *
 * Protocolo con el servicio de identidad:
 *   Cliente -> Servicio: "AUTH <username> <password>\n"
 *   Servicio -> Cliente: "OK <ROLE>\n"  |  "ERROR 401 Unauthorized\n"
 *
 * Retorna:
 *    0  exito (role contiene "ATTACKER" o "DEFENDER")
 *   -1  credenciales invalidas
 *   -2  error de conexion/DNS (el servicio no responde)
 */
int identity_auth(const char *username, const char *password,
                  char *role, int role_sz);

#endif /* IDENTITY_CLIENT_H */
