# Proyecto Telemática - Juego Multijugador Distribuido

Sistema distribuido de juego multijugador basado en arquitectura cliente-servidor,
integrando múltiples protocolos de red y servicios independientes.

Incluye:

* Servidor principal en **C** (manejo de sockets y lógica del juego)
* Servicio de identidad en **Python** (autenticación, microservicio)
* **Interfaz web compartida** en HTML + CSS + JS (`client_web/`)
* Cliente en **Python** (`client_python/`) — sirve la interfaz web, bridge WS→TCP
* Cliente en **Java** (`client_java/`) — sirve la interfaz web, bridge WS→TCP

Ambos clientes eliminan las dependencias de GUI nativas (Tkinter / Swing) y
reutilizan la misma interfaz web, cumpliendo el requisito de implementación en
al menos 2 lenguajes diferentes.

---

## Estructura del proyecto

```
Entrega1-Telematica/
├── server/                        # Servidor de juego en C
│   ├── main.c                     # Entry point, sockets, hilos
│   ├── game.h / game.c            # Estado del juego, lógica
│   ├── protocol.h / protocol.c    # Protocolo CGSP (máquina de estados)
│   ├── http_server.h / .c         # Servidor HTTP integrado (puerto+1)
│   ├── logger.h / logger.c        # Logging hilo-seguro
│   ├── identity_client.h / .c     # Cliente del servicio de identidad
│   └── Makefile
├── identity_service/
│   └── identity_service.py        # Microservicio de autenticación (puerto 9090)
├── client_web/                    # Interfaz web compartida (HTML + CSS + JS)
│   ├── index.html                 # SPA: pantallas Login, Lobby y Juego
│   ├── style.css                  # Tema oscuro / cyberpunk
│   └── game.js                    # Lógica del cliente CGSP vía WebSocket
├── client_python/
│   ├── game_client.py             # Cliente Python: bridge WS→TCP + HTTP server
│   └── requirements.txt
└── client_java/
    └── GameClient.java            # Cliente Java:  bridge WS→TCP + HTTP server
```

---

## Arquitectura de la interfaz web

```
 Navegador (index.html / game.js)
        |
        | WebSocket  (ws://localhost:PUERTO+1)
        |
 ┌──────┴──────────────────┐
 │  Cliente Python          │  ←  python game_client.py localhost 8082
 │    HTTP  :8085           │     sirve client_web/ y abre el navegador
 │    WS    :8086 (bridge)  │
 └──────────────────────────┘
           ó
 ┌──────────────────────────┐
 │  Cliente Java            │  ←  java -cp client_java GameClient localhost 8082
 │    HTTP  :8087           │     sirve client_web/ y abre el navegador
 │    WS    :8088 (bridge)  │
 └──────────────────────────┘
        |
        | TCP / protocolo CGSP
        |
 ┌──────┴──────────────────┐
 │  Servidor de juego (C)  │   puerto 8082
 │  Servidor HTTP (C)      │   puerto 8083
 └──────────────────────────┘
        |
        | TCP
        |
 ┌──────┴──────────────────┐
 │  Servicio de identidad  │   puerto 9090
 │  (Python)               │
 └──────────────────────────┘
```

El JS detecta el puerto WebSocket automáticamente como `HTTP_PORT + 1`.
No hay ninguna IP hardcodeada; todo se resuelve por nombre de dominio.

---

## Cómo ejecutar el proyecto

### 1. Iniciar el servicio de identidad

```bash
python identity_service/identity_service.py 9090
```

### 2. Compilar y ejecutar el servidor de juego (C)

```bash
cd server
make
IDENTITY_HOST=localhost IDENTITY_PORT=9090 ./server 8082 server.log
```

### 3. Ejecutar un cliente (elige uno)

#### Cliente Python

```bash
pip install -r client_python/requirements.txt
python client_python/game_client.py localhost 8082
# Interfaz web disponible en http://localhost:8085
```

#### Cliente Java

```bash
javac client_java/GameClient.java
java -cp client_java GameClient localhost 8082
# Interfaz web disponible en http://localhost:8087
```

El cliente abre el navegador automáticamente.
Si no, visita la URL indicada en la terminal.

---

## Usuarios de prueba

| Usuario   | Contraseña | Rol      |
|-----------|------------|----------|
| attacker1 | pass123    | ATTACKER |
| attacker2 | pass123    | ATTACKER |
| defender1 | pass456    | DEFENDER |
| defender2 | pass456    | DEFENDER |
| admin     | admin123   | DEFENDER |

---

## Protocolo CGSP (Custom Game State Protocol)

Protocolo de texto sobre TCP. Máquina de estados:

```
[AUTH] ──LOGIN──> [LOBBY] ──START_GAME──> [GAME]
                     ^                        |
                     └───────────QUIT─────────┘
```

### Estado AUTH

| Cliente → Servidor         | Servidor → Cliente                       |
|----------------------------|------------------------------------------|
| `LOGIN <user> <pass>`      | `OK LOGIN role:ATTACKER\|DEFENDER`       |
|                            | `ERROR 401 Unauthorized`                 |

### Estado LOBBY

| Cliente → Servidor   | Servidor → Cliente                              |
|----------------------|-------------------------------------------------|
| `LIST_GAMES`         | `GAMES room:0 players:1 state:WAITING …`        |
| `CREATE_GAME`        | `OK CREATE room:<id>`                           |
| `JOIN_GAME <id>`     | `OK JOIN room:<id> role:<role>`                 |
| `START_GAME`         | `GAME_START map:20x20 resources:<coords> pos:x,y` |
| `QUIT`               | `OK QUIT`                                       |

> Los defensores reciben las coordenadas reales de los recursos.
> Los atacantes reciben `resources:?` y deben explorar el mapa.

### Estado GAME

| Comando                  | Respuesta principal                                  |
|--------------------------|------------------------------------------------------|
| `MOVE NORTH\|SOUTH\|EAST\|WEST` | `OK MOVE pos:x,y`                           |
| `SCAN`                   | `SCAN_RESULT FOUND resource_id:<id> pos:x,y`         |
|                          | `SCAN_RESULT EMPTY`                                  |
| `ATTACK <id>`            | `OK ATTACK resource_id:<id>`                         |
| `MITIGATE <id>`          | `OK MITIGATE resource_id:<id>`                       |
| `STATUS`                 | resumen del estado actual                            |
| `QUIT`                   | `OK QUIT`                                            |

**Notificaciones broadcast:**

| Mensaje del servidor                                     | Descripción                      |
|----------------------------------------------------------|----------------------------------|
| `NOTIFY_ATTACK resource_id:<id> pos:x,y attacker:<name> timeout:30` | Ataque iniciado (30 s) |
| `NOTIFY_MITIGATED resource_id:<id> defender:<name>`      | Ataque mitigado                  |
| `NOTIFY_DESTROYED resource_id:<id>`                      | Recurso destruido (timeout)      |
| `PLAYER_MOVED player:<name> role:<role> pos:x,y`         | Otro jugador se movió            |
| `NOTIFY_DISCONNECT player:<name>`                        | Jugador desconectado             |

---

## Mapa del juego

* Tamaño: **20 × 20** celdas
* Recursos críticos (servidores):
  * R0 en (5, 5)
  * R1 en (14, 14)

---

## Decisión técnica: TCP (SOCK_STREAM)

Se utilizó TCP porque:

* El protocolo CGSP es orientado a estado (LOGIN → LOBBY → GAME); requiere orden garantizado.
* La conexión es persistente durante toda la sesión.
* La fiabilidad es crítica: perder un `NOTIFY_ATTACK` haría que el defensor no reaccionara.

---

## Tecnologías

| Componente          | Tecnología                        |
|---------------------|-----------------------------------|
| Servidor de juego   | C + Berkeley Sockets + pthreads   |
| Servidor HTTP (C)   | HTTP/1.0 sobre raw socket         |
| Servicio de identidad | Python 3                        |
| Interfaz web        | HTML5 + CSS3 + JavaScript (vanilla) |
| Cliente Python      | asyncio + websockets              |
| Cliente Java        | Java 11+ (raw sockets, sin libs externas) |
| Transporte          | TCP/IP                            |
