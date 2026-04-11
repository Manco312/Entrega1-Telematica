# CGSP — CyberGame Simulation Protocol
## Especificación del Protocolo — Versión 1.0

---

## Tabla de contenidos

1. [Visión General del Protocolo](#1-visión-general-del-protocolo)
2. [Terminología](#2-terminología)
3. [Arquitectura del Sistema](#3-arquitectura-del-sistema)
4. [Especificación del Servicio](#4-especificación-del-servicio)
5. [Formato de Mensajes](#5-formato-de-mensajes)
6. [Reglas de Procedimiento — Máquina de Estados](#6-reglas-de-procedimiento--máquina-de-estados)
7. [Manejo de Errores](#7-manejo-de-errores)
8. [Ejemplos de Implementación](#8-ejemplos-de-implementación)
9. [Decisión de Transporte](#9-decisión-de-transporte)

---

## 1. Visión General del Protocolo

### 1.1 Propósito

CGSP (CyberGame Simulation Protocol) es un protocolo de capa de aplicación diseñado para coordinar la interacción en tiempo real entre múltiples jugadores en un simulador de ciberseguridad distribuido. Permite que jugadores con roles diferenciados (Atacante / Defensor) interactúen sobre un espacio de juego compartido, ejecutando acciones y recibiendo notificaciones de eventos.

### 1.2 Modelo de funcionamiento

CGSP opera bajo el modelo **cliente-servidor**:

- El **servidor** mantiene el estado global del juego, autentica usuarios (delegando a un servicio de identidad externo), gestiona salas (rooms) y distribuye eventos a los clientes.
- Los **clientes** envían comandos al servidor y reciben respuestas síncronas y notificaciones asíncronas (broadcast).

### 1.3 Capa de operación

CGSP opera en la **capa de aplicación** del modelo TCP/IP, sobre la capa de transporte TCP. Los mensajes son cadenas de texto codificadas en ASCII/UTF-8, terminadas en salto de línea (`\n`).

### 1.4 Tipo de socket utilizado

Se utiliza **SOCK_STREAM** (TCP) de la API de Sockets Berkeley. La justificación se detalla en la [sección 9](#9-decisión-de-transporte).

---

## 2. Terminología

| Término | Definición |
|---------|------------|
| **Cliente** | Proceso que se conecta al servidor para participar en el juego |
| **Servidor** | Proceso en C que acepta conexiones, gestiona el estado y enruta mensajes |
| **Sala (Room)** | Instancia de partida con su propio mapa y jugadores. Identificada por un entero `room_id` |
| **Jugador (Player)** | Entidad autenticada con un rol asignado dentro de una sala |
| **Atacante (ATTACKER)** | Rol ofensivo: explora el mapa para encontrar y atacar recursos críticos |
| **Defensor (DEFENDER)** | Rol defensivo: conoce la posición de los recursos y debe mitigar ataques |
| **Recurso crítico** | Servidor o activo en el mapa que puede ser atacado. Identificado por `resource_id` |
| **Broadcast** | Mensaje enviado por el servidor a todos los jugadores de una sala |
| **Timeout de mitigación** | Tiempo límite (30 segundos) para que un defensor mitigue un ataque antes de que el recurso sea destruido |

---

## 3. Arquitectura del Sistema

```
 ┌────────────────────┐       ┌──────────────────────┐
 │  Cliente Python    │       │   Cliente Java        │
 │  (asyncio + WS)    │       │   (raw sockets)       │
 └────────┬───────────┘       └──────────┬────────────┘
          │  TCP / CGSP                  │  TCP / CGSP
          └────────────────┬─────────────┘
                           │
                  ┌────────┴─────────┐
                  │  Servidor (C)    │  puerto P
                  │  Berkeley Sockets│
                  │  + pthreads      │
                  └────────┬─────────┘
                           │  TCP
                  ┌────────┴─────────┐
                  │ Servicio de      │  puerto 9090
                  │ Identidad (Py)   │
                  └──────────────────┘
```

- El servidor escucha en el puerto `P` (parámetro de línea de comandos).
- El servidor HTTP auxiliar escucha en el puerto `P+1`.
- El servicio de identidad es un proceso independiente; el servidor de juego se conecta a él en cada autenticación.
- No se almacenan usuarios ni contraseñas en el servidor de juego.
- Los servidores se identifican por nombre de dominio, no por dirección IP.

---

## 4. Especificación del Servicio

CGSP define tres estados por conexión. Las primitivas disponibles dependen del estado actual del cliente.

### 4.1 Estado AUTH

El cliente se conecta y debe autenticarse antes de hacer cualquier otra cosa.

| Primitiva | Descripción |
|-----------|-------------|
| `LOGIN` | Autentica al usuario contra el servicio de identidad externo. Si tiene éxito, el cliente avanza al estado LOBBY y recibe su rol asignado. |

### 4.2 Estado LOBBY

El cliente está autenticado y puede gestionar partidas.

| Primitiva | Descripción |
|-----------|-------------|
| `LIST_GAMES` | Solicita la lista de partidas activas o en espera. |
| `CREATE_GAME` | Crea una nueva sala y une al cliente a ella. |
| `JOIN_GAME` | Une al cliente a una sala existente por su identificador. |
| `START_GAME` | Inicia la partida de la sala en que se encuentra el cliente. Requiere al menos 1 atacante y 1 defensor. Todos los jugadores de la sala avanzan al estado GAME. |
| `QUIT` | Cierra la conexión. |

### 4.3 Estado GAME

El cliente está dentro de una partida activa.

| Primitiva | Descripción |
|-----------|-------------|
| `MOVE` | Mueve al jugador una celda en la dirección indicada (NORTH / SOUTH / EAST / WEST). |
| `SCAN` | Detecta si hay un recurso crítico en la celda actual del jugador. |
| `ATTACK` | (Solo ATTACKER) Inicia un ataque sobre el recurso en la celda actual. Inicia el contador de timeout de mitigación y notifica a todos los jugadores de la sala. |
| `MITIGATE` | (Solo DEFENDER) Mitiga un ataque activo sobre el recurso en la celda actual. Restaura el recurso al estado SAFE y notifica a todos. |
| `STATUS` | Solicita el estado actual de la sala: jugadores y recursos. |
| `QUIT` | Abandona la partida y cierra la conexión. |

---

## 5. Formato de Mensajes

### 5.1 Reglas generales

- Todos los mensajes son texto plano en ASCII/UTF-8.
- Cada mensaje termina con el carácter de nueva línea `\n`.
- Los caracteres `\r` son ignorados si están presentes.
- Los campos dentro de un mensaje se separan por **un espacio** (` `).
- Los pares clave-valor usan el formato `clave:valor` (sin espacios alrededor de `:`).
- Los mensajes distinguen mayúsculas y minúsculas.

### 5.2 Mensajes del cliente al servidor

#### LOGIN
```
LOGIN <username> <password>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `username` | string | Nombre de usuario (sin espacios) |
| `password` | string | Contraseña (sin espacios) |

#### LIST_GAMES
```
LIST_GAMES\n
```
Sin parámetros.

#### CREATE_GAME
```
CREATE_GAME\n
```
Sin parámetros.

#### JOIN_GAME
```
JOIN_GAME <room_id>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `room_id` | entero ≥ 0 | Identificador de la sala |

#### START_GAME
```
START_GAME\n
```
Sin parámetros. El cliente debe estar dentro de una sala.

#### MOVE
```
MOVE <direction>\n
```
| Campo | Valores posibles | Descripción |
|-------|-----------------|-------------|
| `direction` | `NORTH` \| `SOUTH` \| `EAST` \| `WEST` | Dirección del movimiento |

#### SCAN
```
SCAN\n
```
Sin parámetros. Detecta recursos en la celda actual del jugador.

#### ATTACK
```
ATTACK <resource_id>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `resource_id` | entero ≥ 0 | ID del recurso a atacar (debe estar en la misma celda) |

#### MITIGATE
```
MITIGATE <resource_id>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `resource_id` | entero ≥ 0 | ID del recurso a mitigar (debe estar en la misma celda y bajo ataque) |

#### STATUS
```
STATUS\n
```
Sin parámetros.

#### QUIT
```
QUIT\n
```
Sin parámetros.

---

### 5.3 Mensajes del servidor al cliente

#### OK LOGIN
```
OK LOGIN role:<role>\n
```
| Campo | Valores | Descripción |
|-------|---------|-------------|
| `role` | `ATTACKER` \| `DEFENDER` | Rol asignado al usuario en la base de datos de identidad |

#### GAMES
```
GAMES <count> [id:<room_id> players:<n> status:<state>]*\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `count` | entero | Cantidad de salas activas listadas |
| `id` | entero | Identificador de la sala |
| `players` | entero | Número de jugadores actualmente en la sala |
| `status` | `WAITING` \| `ACTIVE` | Estado de la sala |

Ejemplo:
```
GAMES 2 id:0 players:2 status:WAITING id:1 players:3 status:ACTIVE
```

#### OK CREATE
```
OK CREATE room:<room_id>\n
```

#### OK JOIN
```
OK JOIN room:<room_id> role:<role>\n
```

#### GAME_START
Enviado a cada jugador cuando la partida inicia. El contenido difiere según el rol:

- **Defensor** — recibe las coordenadas reales de todos los recursos:
```
GAME_START map:<W>x<H> resources:<x0>,<y0>;<x1>,<y1>;... pos:<px>,<py>\n
```

- **Atacante** — recibe `?` en lugar de coordenadas:
```
GAME_START map:<W>x<H> resources:? pos:<px>,<py>\n
```

| Campo | Tipo | Descripción |
|-------|------|-------------|
| `map` | `AxB` | Dimensiones del mapa (ancho x alto en celdas) |
| `resources` | lista de `x,y` separados por `;` o `?` | Posiciones de recursos (solo defensores) |
| `pos` | `x,y` | Posición inicial del jugador en el mapa |

#### OK MOVE
```
OK MOVE pos:<x>,<y>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `pos` | `x,y` | Nueva posición del jugador tras el movimiento |

#### SCAN_RESULT
```
SCAN_RESULT FOUND resource_id:<id> pos:<x>,<y>\n
SCAN_RESULT EMPTY\n
```

#### OK ATTACK
```
OK ATTACK resource_id:<id>\n
```

#### OK MITIGATE
```
OK MITIGATE resource_id:<id>\n
```

#### OK QUIT
```
OK QUIT\n
```

#### STATUS
```
STATUS room:<room_id> players:<n> resources:id:<id>,state:<state>[;id:<id>,state:<state>]*\n
```
| Valor de `state` | Significado |
|-----------------|-------------|
| `SAFE` | Recurso intacto |
| `ATTACKED` | Bajo ataque, contador de timeout activo |
| `DESTROYED` | Destruido, no recuperable |

---

### 5.4 Notificaciones broadcast (servidor → todos en la sala)

#### NOTIFY_ATTACK
Enviado a **todos** los jugadores de la sala cuando un atacante inicia un ataque.
```
NOTIFY_ATTACK resource_id:<id> pos:<x>,<y> attacker:<username> timeout:<seconds>\n
```
| Campo | Tipo | Descripción |
|-------|------|-------------|
| `resource_id` | entero | ID del recurso bajo ataque |
| `pos` | `x,y` | Coordenadas del recurso |
| `attacker` | string | Username del jugador atacante |
| `timeout` | entero | Segundos disponibles para mitigar (siempre 30) |

#### NOTIFY_MITIGATED
Enviado a **todos** cuando un defensor mitiga un ataque exitosamente.
```
NOTIFY_MITIGATED resource_id:<id> defender:<username>\n
```

#### NOTIFY_DESTROYED
Enviado a **todos** cuando el timeout de mitigación expira y el recurso es destruido.
```
NOTIFY_DESTROYED resource_id:<id>\n
```

#### PLAYER_MOVED
Enviado a **todos los demás** jugadores de la sala cuando un jugador se mueve.
```
PLAYER_MOVED player:<username> role:<role> pos:<x>,<y>\n
```

#### NOTIFY_DISCONNECT
Enviado a **todos** cuando un jugador se desconecta.
```
NOTIFY_DISCONNECT player:<username>\n
```

#### GAME_OVER
Enviado a **todos** cuando la partida termina.
```
GAME_OVER winner:<team>\n
```
| Valor de `winner` | Condición |
|-------------------|-----------|
| `ATTACKERS` | Todos los recursos fueron destruidos |
| `DEFENDERS` | El tiempo máximo de partida (300 s) expiró con al menos un recurso intacto |

---

### 5.5 Mensajes de error

```
ERROR <code> <descripción>\n
```

| Código | Significado | Contexto |
|--------|-------------|----------|
| `400` | Formato de mensaje incorrecto o comando desconocido | Cualquier estado |
| `401` | Credenciales inválidas | AUTH |
| `403` | Acción no permitida para el rol del cliente | GAME |
| `404` | Recurso o sala no encontrada | LOBBY / GAME |
| `409` | Conflicto de estado (sala llena, recurso ya atacado, etc.) | LOBBY / GAME |
| `410` | La partida ya terminó | LOBBY |
| `412` | Precondición no cumplida (faltan roles, no está en sala) | LOBBY |
| `500` | Error interno del servidor | Cualquier estado |
| `503` | Servicio de identidad no disponible | AUTH |

---

## 6. Reglas de Procedimiento — Máquina de Estados

### 6.1 Diagrama de estados por conexión

```
                  ┌───────────┐
   TCP connect    │           │
  ──────────────► │   AUTH    │
                  │           │
                  └─────┬─────┘
                        │ LOGIN (éxito)
                        ▼
                  ┌───────────┐
                  │           │◄──────────────────┐
                  │   LOBBY   │                   │
                  │           │                   │
                  └─────┬─────┘                   │
                        │ START_GAME              │ QUIT
                        │ (al menos 1 ATK         │
                        │  y 1 DEF en sala)       │
                        ▼                         │
                  ┌───────────┐                   │
                  │           │                   │
                  │   GAME    ├───────────────────┘
                  │           │
                  └─────┬─────┘
                        │ GAME_OVER / QUIT
                        ▼
                  [ conexión cerrada ]
```

### 6.2 Reglas por estado

**Estado AUTH:**
- Solo se acepta el comando `LOGIN`.
- Cualquier otro comando retorna `ERROR 403 Debe autenticarse primero`.
- Tras LOGIN exitoso → estado LOBBY.
- Tras LOGIN fallido (401 o 503) → permanece en AUTH.

**Estado LOBBY:**
- `CREATE_GAME`: crea sala y une al cliente a ella automáticamente.
- `JOIN_GAME <id>`: el cliente se une a la sala indicada. La sala debe existir y no estar finalizada (`ROOM_DONE`) ni llena.
- `START_GAME`: solo si el cliente está en una sala y hay al menos 1 atacante y 1 defensor. Todos los jugadores de la sala pasan a estado GAME simultáneamente y reciben `GAME_START`.
- `LIST_GAMES`: solo lectura, no cambia estado.
- `QUIT`: cierra conexión.

**Estado GAME:**
- `MOVE <dir>`: válido si la celda de destino está dentro de los límites del mapa (`0 ≤ x < MAP_WIDTH`, `0 ≤ y < MAP_HEIGHT`).
- `SCAN`: retorna `FOUND` si hay un recurso no destruido en la celda actual, `EMPTY` en caso contrario.
- `ATTACK <id>`:
  1. El cliente debe ser ATTACKER.
  2. El recurso `id` debe existir y no estar destruido.
  3. El cliente debe estar en la misma celda que el recurso.
  4. El recurso no debe estar ya bajo ataque.
  5. Si se cumplen: recurso pasa a `ATTACKED`, se inicia el contador de 30 s, se envía `NOTIFY_ATTACK` a todos.
- `MITIGATE <id>`:
  1. El cliente debe ser DEFENDER.
  2. El recurso `id` debe existir.
  3. El cliente debe estar en la misma celda que el recurso.
  4. El recurso debe estar en estado `ATTACKED`.
  5. Si se cumplen: recurso pasa a `SAFE`, se envía `NOTIFY_MITIGATED` a todos.
- `STATUS`: retorna estado de la sala.
- `QUIT`: notifica a compañeros (`NOTIFY_DISCONNECT`), cierra conexión.

### 6.3 Lógica del loop del juego (servidor, ejecutado cada segundo)

El servidor mantiene un hilo dedicado (`game_loop_thread`) que ejecuta `game_tick()` cada segundo:

```
para cada sala ACTIVE:
    para cada recurso ATTACKED:
        si (tiempo_actual - tiempo_inicio_ataque) >= 30s:
            recurso → DESTROYED
            broadcast NOTIFY_DESTROYED
    si todos los recursos están DESTROYED:
        sala → DONE
        broadcast GAME_OVER winner:ATTACKERS
    si (tiempo_actual - tiempo_inicio_sala) >= 300s:
        sala → DONE
        broadcast GAME_OVER winner:DEFENDERS
```

---

## 7. Manejo de Errores

### 7.1 Conexión fallida al servicio de identidad

Si `getaddrinfo()` falla (nombre no resuelto) o `connect()` no puede establecer conexión con el servicio de identidad, el servidor retorna `ERROR 503` al cliente **sin terminar su ejecución**. El servidor continúa aceptando otras conexiones.

### 7.2 Desconexión inesperada del cliente

Si `recv()` retorna 0 o un valor negativo, el servidor:
1. Sale del loop de lectura de comandos.
2. Envía `NOTIFY_DISCONNECT player:<username>` a todos los compañeros de sala.
3. Libera el slot del jugador mediante `game_remove_player()`.

### 7.3 Mensajes con formato incorrecto

El servidor parsea los mensajes con `sscanf` o comparaciones de prefijo. Si el formato no se reconoce, retorna `ERROR 400` y continúa esperando el siguiente mensaje. La conexión no se cierra.

### 7.4 Pipe roto (cliente desconectado durante escritura)

El servidor configura `signal(SIGPIPE, SIG_IGN)` al inicio, evitando que un `send()` a un socket cerrado termine el proceso.

### 7.5 Timeout en el cliente (servicio de identidad)

El servicio de identidad configura `settimeout(10.0)` en cada conexión entrante. Si el cliente no envía datos en 10 segundos, la conexión se cierra limpiamente.

---

## 8. Ejemplos de Implementación

### 8.1 Flujo completo — Atacante inicia sesión y ataca un recurso

```
Cliente                          Servidor
  │                                  │
  │─── LOGIN attacker1 pass123 ─────►│
  │◄── OK LOGIN role:ATTACKER ───────│
  │                                  │
  │─── LIST_GAMES ──────────────────►│
  │◄── GAMES 1 id:0 players:1 ───────│
  │        status:WAITING            │
  │                                  │
  │─── JOIN_GAME 0 ─────────────────►│
  │◄── OK JOIN room:0 role:ATTACKER ─│
  │                                  │
  │─── START_GAME ──────────────────►│
  │◄── GAME_START map:20x20 ─────────│
  │        resources:? pos:3,7       │
  │                                  │
  │─── MOVE EAST ───────────────────►│
  │◄── OK MOVE pos:4,7 ─────────────│
  │                                  │
  │─── SCAN ────────────────────────►│
  │◄── SCAN_RESULT EMPTY ───────────│
  │                                  │
  │─── MOVE SOUTH ──────────────────►│
  │◄── OK MOVE pos:4,8 ─────────────│
  │       (... varios movimientos)   │
  │                                  │
  │─── SCAN ────────────────────────►│
  │◄── SCAN_RESULT FOUND ────────────│
  │        resource_id:0 pos:5,5     │
  │                                  │
  │─── ATTACK 0 ────────────────────►│
  │◄── OK ATTACK resource_id:0 ──────│
  │◄── NOTIFY_ATTACK resource_id:0 ──│  (broadcast a toda la sala)
  │        pos:5,5 attacker:attacker1 │
  │        timeout:30                │
```

### 8.2 Flujo completo — Defensor mitiga un ataque

```
Defensor                         Servidor
  │                                  │
  │─── LOGIN defender1 pass456 ─────►│
  │◄── OK LOGIN role:DEFENDER ───────│
  │─── JOIN_GAME 0 ─────────────────►│
  │◄── OK JOIN room:0 role:DEFENDER ─│
  │                                  │
  │  (otro jugador llama START_GAME) │
  │◄── GAME_START map:20x20 ─────────│
  │        resources:5,5;14,14       │  ← defensor recibe coordenadas
  │        pos:12,9                  │
  │                                  │
  │  (atacante lanza ataque)         │
  │◄── NOTIFY_ATTACK resource_id:0 ──│  (broadcast)
  │        pos:5,5 attacker:attacker1│
  │        timeout:30                │
  │                                  │
  │─── MOVE WEST ───────────────────►│  (defensor se dirige al recurso)
  │◄── OK MOVE pos:11,9 ────────────│
  │         (... varios MOVE)        │
  │                                  │
  │─── MITIGATE 0 ──────────────────►│
  │◄── OK MITIGATE resource_id:0 ────│
  │◄── NOTIFY_MITIGATED ─────────────│  (broadcast a toda la sala)
  │        resource_id:0             │
  │        defender:defender1        │
```

### 8.3 Diagrama de secuencia — Fin de partida por destrucción

```
Atacante          Servidor (game_tick)      Defensor
   │                     │                     │
   │ ATTACK 0 ──────────►│                     │
   │◄── OK ATTACK        │                     │
   │◄── NOTIFY_ATTACK ───┼─── NOTIFY_ATTACK ──►│
   │    timeout:30       │                     │
   │                     │                     │
   │              [30 s sin MITIGATE]           │
   │                     │                     │
   │◄── NOTIFY_DESTROYED─┼── NOTIFY_DESTROYED ►│
   │    resource_id:0    │                     │
   │                     │                     │
   │              [todos DESTROYED]             │
   │                     │                     │
   │◄── GAME_OVER ───────┼─── GAME_OVER ──────►│
   │    winner:ATTACKERS │    winner:ATTACKERS  │
```

---

## 9. Decisión de Transporte

### 9.1 Justificación del uso de TCP (SOCK_STREAM)

Se eligió TCP por las siguientes razones, derivadas de los requerimientos del protocolo CGSP:

| Requerimiento CGSP | Por qué TCP y no UDP |
|--------------------|----------------------|
| El protocolo es orientado a estado (AUTH → LOBBY → GAME). El servidor debe saber en qué estado se encuentra cada cliente. | TCP garantiza que los mensajes llegan en orden, sin duplicados y sin pérdidas. Con UDP, un `LOGIN` podría llegar después de un `MOVE`. |
| La conexión es persistente durante toda la sesión del jugador. | TCP establece una conexión duradera. UDP no tiene conexión; habría que reimplementar ese mecanismo. |
| Un `NOTIFY_ATTACK` perdido significa que el defensor nunca sabe que hay un ataque en curso. | TCP garantiza entrega. Con UDP habría que implementar acuses de recibo manuales. |
| El mapa es 20×20 y los mensajes son pequeños (< 256 bytes). La latencia de TCP es aceptable. | El overhead de TCP es despreciable para este tamaño y frecuencia de mensajes. |

### 9.2 Parámetros del socket

```c
// Creación
int srv_fd = socket(AF_INET, SOCK_STREAM, 0);

// Reutilización de puerto tras reinicio
int opt = 1;
setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// Enlace a todas las interfaces, puerto configurable
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;
addr.sin_port        = htons(port);

// Cola de conexiones pendientes
listen(srv_fd, 10);
```

---

*Protocolo CGSP v1.0 — Internet: Arquitectura y Protocolos, 2026-1*
