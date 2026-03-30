#!/usr/bin/env python3
"""
CyberGame Client - Python / tkinter
=====================================
Cliente del juego con interfaz grafica. Muestra el mapa 20x20 en tiempo
real, la posicion de los jugadores y los recursos criticos.

Uso:
  python game_client.py <hostname> <puerto>
  Ejemplo: python game_client.py localhost 8082

Usuarios de prueba:
  attacker1 / pass123  (rol ATTACKER)
  defender1 / pass456  (rol DEFENDER)
"""

import socket
import threading
import tkinter as tk
from tkinter import messagebox, simpledialog
import sys
import queue

# ===========================================================================
# Constantes visuales
# ===========================================================================
MAP_W = 20
MAP_H = 20
CELL  = 30   # pixeles por celda

BG          = "#0a0a0a"
GRID_CLR    = "#1a3a1a"
MY_CLR      = "#00ff41"
ATK_CLR     = "#ff4400"
DEF_CLR     = "#3399ff"
RES_SAFE    = "#ffff00"
RES_ATK     = "#ff2200"
RES_DEST    = "#444444"
TEXT_CLR    = "#00ff41"
LOG_CLR     = "#99ffaa"
PANEL_BG    = "#111111"


class GameClient:
    def __init__(self, root: tk.Tk, hostname: str, port: int):
        self.root     = root
        self.hostname = hostname
        self.port     = port
        self.sock     = None
        self.connected= False
        self.role     = None          # "ATTACKER" | "DEFENDER"
        self.pos      = (0, 0)        # mi posicion
        self.room_id  = None
        self.in_game  = False
        self.players  = {}            # {name: {role, x, y}}
        self.resources= {}            # {id: {x, y, state}}
        self.msg_queue= queue.Queue()

        root.title("CyberGame Simulator")
        root.configure(bg=BG)
        root.resizable(False, False)
        self._show_login()

    # =======================================================================
    # UI: Login
    # =======================================================================
    def _show_login(self):
        self._clear()
        f = tk.Frame(self.root, bg=BG, padx=50, pady=40)
        f.pack(expand=True)

        tk.Label(f, text="[ CYBERSIM ]", bg=BG, fg=TEXT_CLR,
                 font=("Courier", 22, "bold")).pack()
        tk.Label(f, text="Simulador de Ciberseguridad",
                 bg=BG, fg="#666", font=("Courier", 10)).pack(pady=(0,20))

        tk.Label(f, text=f"Servidor: {self.hostname}:{self.port}",
                 bg=BG, fg="#555", font=("Courier", 9)).pack(anchor="w")

        tk.Label(f, text="Usuario:", bg=BG, fg="#aaa",
                 font=("Courier", 10)).pack(anchor="w", pady=(10,0))
        self.e_user = tk.Entry(f, bg="#1a1a1a", fg=TEXT_CLR,
                               font=("Courier", 12), insertbackground=TEXT_CLR,
                               relief="flat", bd=2, width=28)
        self.e_user.pack(pady=2)

        tk.Label(f, text="Contraseña:", bg=BG, fg="#aaa",
                 font=("Courier", 10)).pack(anchor="w")
        self.e_pass = tk.Entry(f, show="*", bg="#1a1a1a", fg=TEXT_CLR,
                               font=("Courier", 12), insertbackground=TEXT_CLR,
                               relief="flat", bd=2, width=28)
        self.e_pass.pack(pady=2)

        tk.Button(f, text=">> CONECTAR", command=self._do_login,
                  bg=TEXT_CLR, fg=BG, font=("Courier", 12, "bold"),
                  relief="flat", cursor="hand2", width=26).pack(pady=(14,4))

        self.status_var = tk.StringVar(value="")
        tk.Label(f, textvariable=self.status_var, bg=BG, fg="#888",
                 font=("Courier", 9)).pack()

        self.root.bind("<Return>", lambda e: self._do_login())
        self.e_user.focus()

    # =======================================================================
    # UI: Lobby
    # =======================================================================
    def _show_lobby(self):
        self._clear()
        role_clr = ATK_CLR if self.role == "ATTACKER" else DEF_CLR

        f = tk.Frame(self.root, bg=BG, padx=20, pady=16)
        f.pack(fill="both", expand=True)

        tk.Label(f, text=f"[ CYBERSIM ] — {self.role}",
                 bg=BG, fg=role_clr, font=("Courier", 16, "bold")).pack()

        self.room_var = tk.StringVar(value="Sala: —")
        tk.Label(f, textvariable=self.room_var, bg=BG, fg="#666",
                 font=("Courier", 9)).pack(anchor="w")

        # Botones
        bf = tk.Frame(f, bg=BG)
        bf.pack(pady=8)
        for txt, cmd in [
            ("Listar partidas",  self._cmd_list),
            ("Crear partida",    self._cmd_create),
            ("Unirse a partida", self._cmd_join_dialog),
            ("Iniciar partida",  self._cmd_start),
        ]:
            tk.Button(bf, text=txt, command=cmd,
                      bg=PANEL_BG, fg=TEXT_CLR, font=("Courier", 9),
                      relief="flat", cursor="hand2").pack(side="left", padx=3)

        # Lista de partidas
        tk.Label(f, text="Partidas activas:", bg=BG, fg="#aaa",
                 font=("Courier", 9)).pack(anchor="w", pady=(6,0))
        self.games_box = tk.Text(f, height=6, bg=PANEL_BG, fg=TEXT_CLR,
                                 font=("Courier", 9), relief="flat", state="disabled")
        self.games_box.pack(fill="x")

        # Log
        tk.Label(f, text="Log:", bg=BG, fg="#aaa",
                 font=("Courier", 9)).pack(anchor="w", pady=(8,0))
        self.log_box = tk.Text(f, height=8, bg="#050505", fg=LOG_CLR,
                               font=("Courier", 9), relief="flat", state="disabled")
        self.log_box.pack(fill="both", expand=True)

        self._cmd_list()

    # =======================================================================
    # UI: Juego
    # =======================================================================
    def _show_game(self):
        self._clear()
        role_clr = ATK_CLR if self.role == "ATTACKER" else DEF_CLR
        self.root.title(f"CyberGame — {self.role} — Sala {self.room_id}")

        outer = tk.Frame(self.root, bg=BG)
        outer.pack(fill="both", expand=True, padx=8, pady=8)

        # ---- Panel izquierdo: mapa ----
        left = tk.Frame(outer, bg=BG)
        left.pack(side="left")

        cw = MAP_W * CELL + 1
        ch = MAP_H * CELL + 1
        self.canvas = tk.Canvas(left, width=cw, height=ch,
                                bg="#050505", highlightthickness=1,
                                highlightbackground=TEXT_CLR)
        self.canvas.pack()

        info_f = tk.Frame(left, bg=BG)
        info_f.pack(fill="x", pady=4)
        tk.Label(info_f, text="Rol:", bg=BG, fg="#aaa",
                 font=("Courier", 9)).pack(side="left")
        tk.Label(info_f, text=self.role, bg=BG, fg=role_clr,
                 font=("Courier", 9, "bold")).pack(side="left", padx=4)
        self.pos_lbl = tk.Label(info_f, text="Pos: (0,0)", bg=BG, fg="#aaa",
                                font=("Courier", 9))
        self.pos_lbl.pack(side="right")

        # ---- Panel derecho: controles + log ----
        right = tk.Frame(outer, bg=BG, padx=10)
        right.pack(side="right", fill="y")

        # Movimiento
        tk.Label(right, text="MOVIMIENTO", bg=BG, fg=TEXT_CLR,
                 font=("Courier", 9, "bold")).pack(pady=(0,2))
        mf = tk.Frame(right, bg=BG)
        mf.pack()

        def mbtn(text, direction, row, col):
            tk.Button(mf, text=text, width=5,
                      command=lambda d=direction: self._send_move(d),
                      bg=PANEL_BG, fg=TEXT_CLR, font=("Courier", 9),
                      relief="flat").grid(row=row, column=col, padx=1, pady=1)

        mbtn("▲ N", "NORTH", 0, 1)
        mbtn("◄ W", "WEST",  1, 0)
        mbtn("E ►", "EAST",  1, 2)
        mbtn("▼ S", "SOUTH", 2, 1)

        # Teclas de movimiento
        for key, d in [("<Up>","NORTH"),("<Down>","SOUTH"),
                        ("<Left>","WEST"),("<Right>","EAST"),
                        ("<w>","NORTH"),("<s>","SOUTH"),
                        ("<a>","WEST"), ("<d>","EAST")]:
            self.root.bind(key, lambda e, dir=d: self._send_move(dir))

        # Acciones
        tk.Label(right, text="ACCIONES", bg=BG, fg=TEXT_CLR,
                 font=("Courier", 9, "bold")).pack(pady=(10,2))

        tk.Button(right, text="SCAN", width=18,
                  command=self._cmd_scan,
                  bg=PANEL_BG, fg=TEXT_CLR,
                  font=("Courier", 9), relief="flat").pack(pady=1)

        if self.role == "ATTACKER":
            tk.Button(right, text="ATTACK", width=18,
                      command=self._cmd_attack,
                      bg="#330000", fg="#ff6666",
                      font=("Courier", 9, "bold"), relief="flat").pack(pady=1)
        else:
            tk.Button(right, text="MITIGATE", width=18,
                      command=self._cmd_mitigate,
                      bg="#001133", fg="#6699ff",
                      font=("Courier", 9, "bold"), relief="flat").pack(pady=1)

        tk.Button(right, text="STATUS", width=18,
                  command=self._cmd_status,
                  bg=PANEL_BG, fg=TEXT_CLR,
                  font=("Courier", 9), relief="flat").pack(pady=1)
        tk.Button(right, text="QUIT", width=18,
                  command=self._cmd_quit,
                  bg="#220000", fg="#cc4444",
                  font=("Courier", 9), relief="flat").pack(pady=1)

        # Leyenda
        tk.Label(right, text="LEYENDA", bg=BG, fg=TEXT_CLR,
                 font=("Courier", 9, "bold")).pack(pady=(10,2))
        for sym, clr, label in [
            ("●", MY_CLR,   "Tu jugador"),
            ("●", ATK_CLR,  "Atacante"),
            ("●", DEF_CLR,  "Defensor"),
            ("■", RES_SAFE, "Servidor seguro"),
            ("■", RES_ATK,  "Servidor atacado"),
            ("■", RES_DEST, "Servidor destruido"),
        ]:
            lf = tk.Frame(right, bg=BG)
            lf.pack(anchor="w")
            tk.Label(lf, text=sym, bg=BG, fg=clr,
                     font=("Courier", 11)).pack(side="left")
            tk.Label(lf, text=f" {label}", bg=BG, fg="#666",
                     font=("Courier", 8)).pack(side="left")

        # Log del juego
        tk.Label(right, text="LOG", bg=BG, fg=TEXT_CLR,
                 font=("Courier", 9, "bold")).pack(pady=(10,2))
        self.log_box = tk.Text(right, width=30, height=14,
                               bg="#050505", fg=LOG_CLR,
                               font=("Courier", 8), relief="flat",
                               state="disabled")
        self.log_box.pack()

        self._draw_map()

    # =======================================================================
    # Dibujo del mapa
    # =======================================================================
    def _draw_map(self):
        if not hasattr(self, 'canvas'): return
        self.canvas.delete("all")

        # Grid
        for i in range(MAP_W + 1):
            x = i * CELL
            self.canvas.create_line(x, 0, x, MAP_H*CELL, fill=GRID_CLR)
        for j in range(MAP_H + 1):
            y = j * CELL
            self.canvas.create_line(0, y, MAP_W*CELL, y, fill=GRID_CLR)

        # Recursos
        for rid, r in self.resources.items():
            x1 = r['x'] * CELL + 4;  y1 = r['y'] * CELL + 4
            x2 = x1 + CELL - 8;      y2 = y1 + CELL - 8
            clr = RES_SAFE if r['state']=='SAFE' else \
                  RES_ATK  if r['state']=='ATTACKED' else RES_DEST
            self.canvas.create_rectangle(x1, y1, x2, y2, fill=clr, outline="")
            self.canvas.create_text(
                r['x']*CELL + CELL//2, r['y']*CELL + CELL//2,
                text="S", fill=BG, font=("Courier", 8, "bold"))

        # Otros jugadores
        for name, info in self.players.items():
            clr = ATK_CLR if info.get('role')=='ATTACKER' else DEF_CLR
            cx = info['x']*CELL + CELL//2
            cy = info['y']*CELL + CELL//2
            r2 = CELL//2 - 4
            self.canvas.create_oval(cx-r2, cy-r2, cx+r2, cy+r2,
                                    fill=clr, outline="")

        # Mi jugador
        mx, my = self.pos
        cx = mx*CELL + CELL//2;  cy = my*CELL + CELL//2
        r2 = CELL//2 - 3
        self.canvas.create_oval(cx-r2, cy-r2, cx+r2, cy+r2,
                                fill=MY_CLR, outline="white", width=2)
        self.canvas.create_text(cx, cy, text="U",
                                fill=BG, font=("Courier", 8, "bold"))

        if hasattr(self, 'pos_lbl'):
            self.pos_lbl.config(text=f"Pos: {self.pos}")

    def _log(self, msg: str):
        if hasattr(self, 'log_box') and self.log_box:
            self.log_box.config(state="normal")
            self.log_box.insert("end", f"» {msg}\n")
            self.log_box.see("end")
            self.log_box.config(state="disabled")

    def _clear(self):
        for w in self.root.winfo_children():
            w.destroy()

    # =======================================================================
    # Red: conexion y envio
    # =======================================================================
    def _connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(6.0)
            # Resolucion DNS - sin IPs hardcodeadas
            self.sock.connect((self.hostname, self.port))
            self.sock.settimeout(None)
            self.connected = True
            threading.Thread(target=self._recv_loop, daemon=True).start()
            return True
        except socket.gaierror as e:
            messagebox.showerror("Error DNS",
                f"No se pudo resolver '{self.hostname}':\n{e}")
            return False
        except Exception as e:
            messagebox.showerror("Error de conexion", str(e))
            return False

    def _send(self, msg: str):
        if not self.connected or not self.sock: return
        try:
            self.sock.sendall((msg + "\n").encode('utf-8'))
        except Exception as e:
            self._log(f"Error enviando: {e}")
            self.connected = False

    def _recv_loop(self):
        buf = ""
        try:
            while self.connected:
                data = self.sock.recv(4096)
                if not data:
                    self.msg_queue.put("__DISCONNECTED__")
                    break
                buf += data.decode('utf-8', errors='replace')
                while '\n' in buf:
                    line, buf = buf.split('\n', 1)
                    line = line.strip()
                    if line:
                        self.msg_queue.put(line)
        except Exception:
            if self.connected:
                self.msg_queue.put("__DISCONNECTED__")
        self.connected = False

    def _poll_messages(self):
        try:
            while True:
                msg = self.msg_queue.get_nowait()
                self._handle(msg)
        except queue.Empty:
            pass
        self.root.after(80, self._poll_messages)

    # =======================================================================
    # Procesamiento de mensajes del servidor
    # =======================================================================
    def _handle(self, msg: str):
        self._log(f"S: {msg}")

        if msg == "__DISCONNECTED__":
            messagebox.showwarning("Desconectado",
                                   "Se perdio la conexion con el servidor.")
            self._show_login()
            return

        # --- Autenticacion ---
        if msg.startswith("OK LOGIN role:"):
            self.role = msg.split("role:")[1].strip()
            self._show_lobby()

        elif msg.startswith("ERROR"):
            messagebox.showerror("Error del servidor", msg)

        # --- Lobby ---
        elif msg.startswith("GAMES "):
            if hasattr(self, 'games_box'):
                self.games_box.config(state="normal")
                self.games_box.delete("1.0", "end")
                self.games_box.insert("end", msg)
                self.games_box.config(state="disabled")

        elif msg.startswith("OK CREATE room:"):
            self.room_id = int(msg.split("room:")[1])
            if hasattr(self, 'room_var'):
                self.room_var.set(f"Sala: {self.room_id}")
            self._log(f"Sala {self.room_id} creada.")

        elif msg.startswith("OK JOIN room:"):
            for part in msg.split():
                if part.startswith("room:"): self.room_id = int(part.split(":")[1])
            if hasattr(self, 'room_var'):
                self.room_var.set(f"Sala: {self.room_id}")
            self._log(f"Unido a sala {self.room_id}.")

        # --- Inicio de partida ---
        elif msg.startswith("GAME_START"):
            self._handle_game_start(msg)

        # --- Durante el juego ---
        elif msg.startswith("OK MOVE pos:"):
            coords = msg.split("pos:")[1].strip().split(",")
            self.pos = (int(coords[0]), int(coords[1]))
            self._draw_map()

        elif msg.startswith("PLAYER_MOVED"):
            self._handle_player_moved(msg)
            self._draw_map()

        elif msg.startswith("SCAN_RESULT"):
            if "FOUND" in msg:
                rid    = int(msg.split("resource_id:")[1].split()[0])
                coords = msg.split("pos:")[1].strip().split(",")
                self.resources[rid] = {'x': int(coords[0]),
                                       'y': int(coords[1]),
                                       'state': 'SAFE'}
                self._draw_map()
                self._log(f"[SCAN] Recurso {rid} en {self.pos}")
            else:
                self._log("[SCAN] Celda vacia.")

        elif msg.startswith("NOTIFY_ATTACK"):
            self._handle_notify_attack(msg)
            self._draw_map()

        elif msg.startswith("NOTIFY_MITIGATED"):
            rid = int(msg.split("resource_id:")[1].split()[0])
            if rid in self.resources:
                self.resources[rid]['state'] = 'SAFE'
            self._log(f"[✓] Recurso {rid} mitigado.")
            self._draw_map()

        elif msg.startswith("NOTIFY_DESTROYED"):
            rid = int(msg.split("resource_id:")[1].strip())
            if rid in self.resources:
                self.resources[rid]['state'] = 'DESTROYED'
            self._log(f"[!!!] Recurso {rid} DESTRUIDO.")
            self._draw_map()

        elif msg.startswith("NOTIFY_DISCONNECT"):
            name = msg.split("player:")[1].strip()
            self.players.pop(name, None)
            self._log(f"Jugador '{name}' se desconecto.")
            self._draw_map()

        elif msg.startswith("GAME_OVER"):
            winner = msg.split("winner:")[1].strip()
            messagebox.showinfo("FIN DEL JUEGO",
                                f"Partida terminada.\nGanador: {winner}")
            self.in_game = False

        elif msg.startswith("STATUS"):
            self._log(msg)

    def _handle_game_start(self, msg: str):
        self.in_game = True
        self.resources = {}
        self.players   = {}

        # Parsear: GAME_START map:20x20 resources:5,5;14,14 pos:x,y
        for part in msg.split():
            if part.startswith("pos:"):
                c = part.split(":")[1].split(",")
                self.pos = (int(c[0]), int(c[1]))
            elif part.startswith("resources:") and "?" not in part:
                res_str = part.split(":", 1)[1]
                for i, pair in enumerate(res_str.split(";")):
                    coords = pair.split(",")
                    if len(coords) == 2:
                        self.resources[i] = {
                            'x': int(coords[0]), 'y': int(coords[1]),
                            'state': 'SAFE'
                        }

        self._show_game()
        self._log(">>> PARTIDA INICIADA <<<")
        if self.role == "ATTACKER":
            self._log("Muevete y usa SCAN para encontrar servidores criticos.")
        else:
            self._log("Defiende los servidores. Corre a ellos cuando sean atacados!")

    def _handle_player_moved(self, msg: str):
        try:
            name = role = pos_s = None
            for part in msg.split():
                if part.startswith("player:"): name  = part.split(":")[1]
                elif part.startswith("role:"): role  = part.split(":")[1]
                elif part.startswith("pos:"):  pos_s = part.split(":")[1]
            if name and pos_s:
                c = pos_s.split(",")
                self.players[name] = {'role': role,
                                      'x': int(c[0]), 'y': int(c[1])}
        except Exception:
            pass

    def _handle_notify_attack(self, msg: str):
        try:
            rid     = int(msg.split("resource_id:")[1].split()[0])
            coords  = msg.split("pos:")[1].split()[0].split(",")
            rx, ry  = int(coords[0]), int(coords[1])
            attacker= msg.split("attacker:")[1].split()[0]
            timeout = msg.split("timeout:")[1].strip()
            self.resources[rid] = {'x': rx, 'y': ry, 'state': 'ATTACKED'}
            self._log(f"[!! ATAQUE] Recurso {rid} por {attacker}. {timeout}s para mitigar!")
            if self.role == "DEFENDER":
                messagebox.showwarning(
                    "¡ATAQUE!",
                    f"Recurso {rid} bajo ataque por '{attacker}'!\n"
                    f"Ubicacion: ({rx},{ry})\n"
                    f"Tienes {timeout} segundos para mitigar."
                )
        except Exception:
            pass

    # =======================================================================
    # Comandos
    # =======================================================================
    def _do_login(self):
        user = self.e_user.get().strip()
        pwd  = self.e_pass.get().strip()
        if not user or not pwd:
            self.status_var.set("Ingresa usuario y contraseña.")
            return
        self.status_var.set("Conectando...")
        self.root.update()
        if not self._connect():
            self.status_var.set("Fallo la conexion.")
            return
        self._send(f"LOGIN {user} {pwd}")
        self.root.after(80, self._poll_messages)

    def _cmd_list(self):
        self._send("LIST_GAMES")

    def _cmd_create(self):
        self._send("CREATE_GAME")

    def _cmd_join_dialog(self):
        rid = simpledialog.askinteger("Unirse a sala",
                                      "Ingresa el ID de la sala:",
                                      parent=self.root)
        if rid is not None:
            self._send(f"JOIN_GAME {rid}")

    def _cmd_start(self):
        self._send("START_GAME")

    def _send_move(self, direction: str):
        if self.in_game:
            self._send(f"MOVE {direction}")

    def _cmd_scan(self):
        if self.in_game:
            self._send("SCAN")

    def _cmd_attack(self):
        if not self.in_game: return
        # Auto-detectar si estoy sobre un recurso
        for rid, r in self.resources.items():
            if r['x'] == self.pos[0] and r['y'] == self.pos[1]:
                if messagebox.askyesno("Atacar",
                        f"Atacar recurso {rid} en {self.pos}?"):
                    self._send(f"ATTACK {rid}")
                return
        rid = simpledialog.askinteger("Atacar", "ID del recurso a atacar:",
                                      parent=self.root)
        if rid is not None:
            self._send(f"ATTACK {rid}")

    def _cmd_mitigate(self):
        if not self.in_game: return
        # Auto-detectar recurso bajo ataque en mi posicion
        for rid, r in self.resources.items():
            if (r['x'] == self.pos[0] and r['y'] == self.pos[1]
                    and r['state'] == 'ATTACKED'):
                if messagebox.askyesno("Mitigar",
                        f"Mitigar ataque en recurso {rid}?"):
                    self._send(f"MITIGATE {rid}")
                return
        rid = simpledialog.askinteger("Mitigar", "ID del recurso a mitigar:",
                                      parent=self.root)
        if rid is not None:
            self._send(f"MITIGATE {rid}")

    def _cmd_status(self):
        if self.connected:
            self._send("STATUS")

    def _cmd_quit(self):
        if messagebox.askyesno("Salir", "¿Salir del juego?"):
            self._send("QUIT")
            if self.sock:
                try: self.sock.close()
                except Exception: pass
            self.root.quit()


# ===========================================================================
# Entry point
# ===========================================================================
def main():
    if len(sys.argv) < 3:
        print(f"Uso: python {sys.argv[0]} <hostname> <puerto>")
        print("Ejemplo: python game_client.py localhost 8082")
        sys.exit(1)

    hostname = sys.argv[1]
    try:
        port = int(sys.argv[2])
    except ValueError:
        print("Error: el puerto debe ser un numero entero.")
        sys.exit(1)

    root = tk.Tk()
    app  = GameClient(root, hostname, port)
    root.mainloop()


if __name__ == "__main__":
    main()
