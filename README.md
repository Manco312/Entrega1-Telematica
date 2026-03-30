# 🎮 Proyecto Telemática - Juego Multijugador Distribuido

Este proyecto implementa un sistema distribuido de juego multijugador basado en arquitectura cliente-servidor, integrando múltiples protocolos de red y servicios independientes.

Incluye:

* Servidor principal en C (manejo de sockets y lógica del juego)
* Servicio de identidad en Python (autenticación)
* Clientes en Python (Tkinter) y Java (Swing)

---

## 📁 Estructura del Proyecto

```
proyectotelematica/
├── server/                    # Servidor en C
│   ├── main.c                 # Entry point, sockets, hilos
│   ├── game.h / game.c        # Estado del juego, lógica
│   ├── protocol.h / protocol.c# Protocolo CGSP (máquina de estados)
│   ├── http_server.h / .c     # Servidor HTTP (puerto+1)
│   ├── logger.h / logger.c    # Logging hilo-seguro
│   ├── identity_client.h / .c # Cliente del servicio de identidad
│   └── Makefile
├── identity_service/
│   └── identity_service.py    # Servicio de autenticación
├── client_python/
│   └── game_client.py         # Cliente Python (Tkinter)
└── client_java/
    └── GameClient.java        # Cliente Java (Swing)
```

---

## 🚀 Cómo ejecutar el proyecto

### 1️⃣ Iniciar servicio de identidad (Python)

```bash
python identity_service/identity_service.py 9090
```

---

### 2️⃣ Compilar y ejecutar servidor de juego (C)

```bash
cd server
make
IDENTITY_HOST=localhost IDENTITY_PORT=9090 ./server 8082 server.log
```

---

### 3️⃣ Ejecutar clientes

#### Cliente Python

```bash
python client_python/game_client.py localhost 8082
```

#### Cliente Java

```bash
javac client_java/GameClient.java
java -cp client_java GameClient localhost 8082
```

---

## 👤 Usuarios de prueba

| Usuario   | Contraseña | Rol      |
| --------- | ---------- | -------- |
| attacker1 | pass123    | ATTACKER |
| attacker2 | pass123    | ATTACKER |
| defender1 | pass456    | DEFENDER |
| defender2 | pass456    | DEFENDER |

---

## 🔌 Protocolo de comunicación

Se implementa un protocolo personalizado llamado **CGSP (Custom Game State Protocol)** basado en una máquina de estados.

### Flujo del protocolo:

```
LOGIN → LOBBY → GAME
```

### Acciones disponibles:

* `LIST_GAMES`
* `JOIN_GAME <id>`
* `CREATE_GAME`
* `START_GAME`

---

## ⚙️ Decisión técnica: uso de TCP

Se utilizó **TCP (SOCK_STREAM)** debido a:

* Comunicación orientada a estado (login → lobby → juego)
* Necesidad de entrega ordenada de mensajes
* Conexión persistente requerida por la especificación
* Fiabilidad en transmisión de datos

---

## 🎯 Flujo del juego

1. El usuario inicia sesión
2. Consulta partidas disponibles (`LIST_GAMES`)
3. Se une o crea una partida:

   * `JOIN_GAME <id>`
   * `CREATE_GAME`
4. Cuando hay:

   * ≥1 atacante
   * ≥1 defensor
     → Se inicia el juego (`START_GAME`)

---

## 🕹️ Mecánicas del juego

### 🔴 Atacantes

* `MOVE` → desplazarse por el mapa
* `SCAN` → buscar servidores
* `ATTACK <id>` → atacar un servidor

### 🔵 Defensores

* Se desplazan hacia servidores atacados
* `MITIGATE <id>` → mitigar ataque (30 segundos)

---

## 🗺️ Mapa del juego

* Tamaño: **20 × 20**
* Recursos ubicados en:

  * 📍 (5,5)
  * 📍 (14,14)

---

## 🧩 Componentes del sistema

### 🧠 Servidor principal (C)

* Manejo de múltiples clientes con hilos
* Control del estado del juego
* Implementación del protocolo CGSP

### 🔐 Servicio de identidad (Python)

* Autenticación de usuarios
* Separación de responsabilidades (microservicio)

### 🌐 Servidor HTTP

* Exposición de estado del juego (puerto +1)

### 🧾 Logger

* Registro de eventos thread-safe

---

## 🧪 Tecnologías utilizadas

* **C** → servidor principal
* **Python** → servicio de identidad y cliente GUI
* **Java** → cliente alternativo
* **Sockets TCP/IP**
* **Multithreading**
* **Tkinter / Swing**
