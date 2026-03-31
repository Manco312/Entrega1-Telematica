#!/usr/bin/env python3
"""
CyberGame Client — Python (interfaz web compartida)
====================================================
Reemplaza la GUI Tkinter por una interfaz web alojada en client_web/.

Este script:
  1. Sirve los archivos estáticos de client_web/ en HTTP (puerto --http-port, por defecto 8085).
  2. Abre un servidor WebSocket en (HTTP+1) que actúa de puente hacia el servidor de juego TCP.
  3. Abre el navegador automáticamente.

El frontend JS (game.js) detecta el puerto WebSocket como HTTP+1 y se conecta sin
necesidad de configuración adicional.

Uso:
  pip install websockets
  python3 game_client.py <host> <puerto>
  python3 game_client.py localhost 8082 --http-port 8085

Usuarios de prueba:
  attacker1 / pass123   (rol ATTACKER)
  defender1 / pass456   (rol DEFENDER)
"""

import asyncio
import threading
import http.server
import os
import sys
import webbrowser
import argparse

try:
    import websockets
except ImportError:
    print("[!] Falta la dependencia.  Ejecuta:  pip install websockets")
    sys.exit(1)

# ── Rutas ────────────────────────────────────────────────────────────────────
_HERE    = os.path.dirname(os.path.abspath(__file__))
WEB_ROOT = os.path.normpath(os.path.join(_HERE, '..', 'client_web'))


# ── Servidor HTTP (archivos estáticos) ───────────────────────────────────────

class _Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_ROOT, **kwargs)

    def log_message(self, fmt, *args):
        pass  # silenciar log de acceso


def _run_http(port: int) -> None:
    with http.server.ThreadingHTTPServer(('', port), _Handler) as srv:
        print(f"[HTTP]  http://localhost:{port}")
        srv.serve_forever()


# ── Bridge WebSocket → TCP (servidor de juego) ───────────────────────────────

async def _bridge(websocket, game_host: str, game_port: int, path=None):
    """Cada conexión WebSocket del navegador abre una conexión TCP al juego."""
    try:
        reader, writer = await asyncio.open_connection(game_host, game_port)
    except Exception as exc:
        try:
            await websocket.send(f"__PROXY_ERROR__ {exc}")
            await websocket.close()
        except Exception:
            pass
        return

    async def ws_to_tcp():
        try:
            async for msg in websocket:
                line = msg if msg.endswith('\n') else msg + '\n'
                writer.write(line.encode())
                await writer.drain()
        except Exception:
            pass
        finally:
            writer.close()

    async def tcp_to_ws():
        try:
            while True:
                raw = await reader.readline()
                if not raw:
                    break
                await websocket.send(raw.decode(errors='replace').rstrip('\r\n'))
        except Exception:
            pass
        finally:
            try:
                await websocket.close()
            except Exception:
                pass

    await asyncio.gather(ws_to_tcp(), tcp_to_ws(), return_exceptions=True)


# ── Bucle principal ──────────────────────────────────────────────────────────

async def _main(game_host: str, game_port: int, http_port: int) -> None:
    ws_port = http_port + 1

    http_thread = threading.Thread(target=_run_http, args=(http_port,), daemon=True)
    http_thread.start()

    # handler compatible con websockets v9 (path como arg) y v10+ (websocket.path)
    handler = lambda ws, path=None: _bridge(ws, game_host, game_port, path)

    print(f"[WS]    ws://localhost:{ws_port}")
    print(f"[GAME]  {game_host}:{game_port}")

    url = f"http://localhost:{http_port}"
    threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    print(f"[*]     Abriendo {url} en el navegador...")

    try:
        async with websockets.serve(handler, '', ws_port):
            await asyncio.Future()   # ejecutar indefinidamente
    except OSError as exc:
        print(f"[!] No se pudo iniciar WebSocket en puerto {ws_port}: {exc}")
        sys.exit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='CyberGame — cliente Python (interfaz web)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument('host', nargs='?', default='localhost',
                        help='Hostname del servidor de juego (default: localhost)')
    parser.add_argument('port', nargs='?', type=int, default=8082,
                        help='Puerto TCP del servidor de juego (default: 8082)')
    parser.add_argument('--http-port', type=int, default=8085, metavar='PUERTO',
                        help='Puerto HTTP local para la interfaz web (default: 8085)')
    args = parser.parse_args()

    try:
        asyncio.run(_main(args.host, args.port, args.http_port))
    except KeyboardInterrupt:
        print('\n[*] Cerrando.')
