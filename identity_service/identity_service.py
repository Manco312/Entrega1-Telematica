#!/usr/bin/env python3
"""
Servicio de Identidad - CyberGame
===================================
Servicio separado de autenticacion. El servidor de juego NO almacena
usuarios localmente: delega toda autenticacion a este servicio.

Protocolo:
  Cliente -> Servicio: "AUTH <username> <password>\\n"
  Servicio -> Cliente: "OK <ROLE>\\n"          (ROLE = ATTACKER | DEFENDER)
                     | "ERROR 401 Unauthorized\\n"
                     | "ERROR 400 Bad Request\\n"

Uso:
  python identity_service.py [puerto]
  Ejemplo: python identity_service.py 9090
"""

import socket
import threading
import logging
import sys
import os

HOST = '0.0.0.0'
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9090

# ===========================================================================
# Base de datos de usuarios (en memoria)
# En produccion esto deberia ser una base de datos real.
# El campo 'role' puede ser ATTACKER o DEFENDER.
# ===========================================================================
USERS = {
    "attacker1": {"password": "pass123", "role": "ATTACKER"},
    "attacker2": {"password": "pass123", "role": "ATTACKER"},
    "defender1": {"password": "pass456", "role": "DEFENDER"},
    "defender2": {"password": "pass456", "role": "DEFENDER"},
    "admin":     {"password": "admin123", "role": "DEFENDER"},
}

# ===========================================================================
# Logging
# ===========================================================================
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s][IDENTITY][%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S',
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler("identity_service.log", mode='a')
    ]
)

# ===========================================================================
# Manejo de conexiones
# ===========================================================================

def handle_client(conn: socket.socket, addr: tuple):
    """Maneja una peticion de autenticacion de un cliente."""
    ip, port = addr
    try:
        conn.settimeout(10.0)
        data = conn.recv(1024).decode('utf-8').strip()

        logging.info(f"REQUEST  {ip}:{port} - {data}")

        parts = data.split()
        if len(parts) == 3 and parts[0] == 'AUTH':
            username = parts[1]
            password = parts[2]

            if username in USERS and USERS[username]['password'] == password:
                role     = USERS[username]['role']
                response = f"OK {role}\n"
                logging.info(f"RESPONSE {ip}:{port} - Auth OK: {username} -> {role}")
            else:
                response = "ERROR 401 Unauthorized\n"
                logging.warning(f"RESPONSE {ip}:{port} - Auth FAIL: '{username}'")
        else:
            response = "ERROR 400 Bad Request. Uso: AUTH <usuario> <contrasena>\n"
            logging.warning(f"RESPONSE {ip}:{port} - Bad request: '{data}'")

        conn.sendall(response.encode('utf-8'))

    except socket.timeout:
        logging.error(f"Timeout esperando datos de {ip}:{port}")
    except UnicodeDecodeError:
        logging.error(f"Error de decodificacion desde {ip}:{port}")
        try:
            conn.sendall(b"ERROR 400 Encoding error\n")
        except Exception:
            pass
    except Exception as e:
        logging.error(f"Error inesperado con {ip}:{port}: {e}")
    finally:
        conn.close()


def main():
    logging.info("=" * 50)
    logging.info("Servicio de Identidad CyberGame iniciando")
    logging.info(f"Escuchando en {HOST}:{PORT}")
    logging.info(f"Usuarios registrados: {list(USERS.keys())}")
    logging.info("=" * 50)

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((HOST, PORT))
            server.listen(20)
            logging.info(f"Servidor listo en {HOST}:{PORT}")

            while True:
                try:
                    conn, addr = server.accept()
                    t = threading.Thread(
                        target=handle_client,
                        args=(conn, addr),
                        daemon=True
                    )
                    t.start()
                except Exception as e:
                    logging.error(f"Error aceptando conexion: {e}")

    except OSError as e:
        logging.error(f"No se pudo iniciar el servidor: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        logging.info("Servicio de identidad detenido.")


if __name__ == '__main__':
    main()
