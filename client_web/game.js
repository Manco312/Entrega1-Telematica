'use strict';

/* ═══════════════════════════════════════════════════════════
   CONFIGURACIÓN
   El cliente Python/Java sirve HTTP en el puerto P y
   WebSocket en el puerto P+1.  El JS lo detecta
   automáticamente con window.location.port.
   ═══════════════════════════════════════════════════════════ */
const _httpPort = parseInt(window.location.port || '80');
const _wsPort   = _httpPort + 1;
const WS_URL    = `ws://${window.location.hostname}:${_wsPort}`;

/* ── Canvas ─────────────────────────────────────────────── */
const CELL = 30;
const GRID = 20;

/* ── Paleta de colores ──────────────────────────────────── */
const C = {
  bg:       '#0a0a0a',
  grid:     '#0f1f0f',
  me:       '#00ff41',
  atk:      '#ff6600',
  def:      '#3399ff',
  safe:     '#ffff00',
  attacked: '#ff2200',
  dest:     '#333333',
};

/* ── Estado del juego ───────────────────────────────────── */
let ws        = null;
let myName    = '';
let myRole    = '';
let myPos     = { x: -1, y: -1 };
let myRoom    = -1;
let players   = {};   // nombre → { x, y, role }
let resources = {};   // id    → { x, y, state: 'SAFE'|'ATTACKED'|'DESTROYED' }
let nearRes   = null; // id del recurso en la posición actual (o null)

/* ── Helpers DOM ────────────────────────────────────────── */
const $ = id => document.getElementById(id);

function showScreen(name) {
  document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
  $(`screen-${name}`).classList.add('active');
  if (name === 'game') redraw();
}

function appendLog(boxId, text, cls = '') {
  const box  = $(boxId);
  const line = document.createElement('div');
  if (cls) line.className = cls;
  line.textContent = text;
  box.appendChild(line);
  box.scrollTop = box.scrollHeight;
}

const llog = (t, c) => appendLog('lobby-log', t, c);
const glog = (t, c) => appendLog('game-log',  t, c);

function setStatus(msg, cls = '') {
  const el = $('login-status');
  el.textContent = msg;
  el.className   = 'status-msg' + (cls ? ' ' + cls : '');
}

/* ── Modal ──────────────────────────────────────────────── */
function showModal(title, def = '') {
  return new Promise(resolve => {
    $('modal-title').textContent = title;
    $('modal-input').value       = def;
    $('modal').classList.remove('hidden');
    $('modal-input').focus();

    const ok = () => {
      const v = $('modal-input').value.trim();
      $('modal').classList.add('hidden');
      cleanup();
      resolve(v || null);
    };
    const cancel = () => {
      $('modal').classList.add('hidden');
      cleanup();
      resolve(null);
    };
    const onKey = e => {
      if (e.key === 'Enter')  ok();
      if (e.key === 'Escape') cancel();
    };
    function cleanup() {
      $('modal-ok').removeEventListener('click', ok);
      $('modal-cancel').removeEventListener('click', cancel);
      document.removeEventListener('keydown', onKey);
    }
    $('modal-ok').addEventListener('click', ok);
    $('modal-cancel').addEventListener('click', cancel);
    document.addEventListener('keydown', onKey);
  });
}

/* ═══════════════════════════════════════════════════════════
   WEBSOCKET  (conecta al bridge local del cliente Python/Java)
   ═══════════════════════════════════════════════════════════ */
function connectWS() {
  $('ws-url-hint').textContent = WS_URL;
  setStatus('Connecting to bridge…');

  ws = new WebSocket(WS_URL);

  ws.onopen = () => {
    setStatus('Bridge connected — enter credentials', 'ok');
    $('btn-login').disabled = false;
  };

  ws.onmessage = e => handleMsg(e.data);

  ws.onerror = () => {
    setStatus('Bridge error — ¿está corriendo el cliente Python/Java?', 'err');
    $('btn-login').disabled = true;
  };

  ws.onclose = () => {
    $('btn-login').disabled = true;
    if ($('screen-game').classList.contains('active')) {
      glog('[conexión cerrada]', 'c-err');
    } else if ($('screen-lobby').classList.contains('active')) {
      llog('[conexión cerrada]', 'c-err');
    } else {
      setStatus('Bridge desconectado — recarga la página', 'err');
    }
  };
}

function send(msg) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(msg);
}

/* ═══════════════════════════════════════════════════════════
   PARSEO DE MENSAJES CGSP
   ═══════════════════════════════════════════════════════════ */
function parseKV(parts) {
  const out = {};
  for (const p of parts) {
    const i = p.indexOf(':');
    if (i > 0) out[p.slice(0, i)] = p.slice(i + 1);
  }
  return out;
}

function handleMsg(raw) {
  const msg   = raw.trim();
  const parts = msg.split(' ');
  const cmd   = parts[0];

  /* Errores del bridge local */
  if (cmd === '__PROXY_ERROR__') {
    setStatus(parts.slice(1).join(' '), 'err');
    return;
  }

  /* ── AUTH → LOBBY ─────────────────────────────────────── */
  if (cmd === 'OK' && parts[1] === 'LOGIN') {
    const kv = parseKV(parts.slice(2));
    myRole   = kv.role || 'UNKNOWN';
    myName   = $('inp-user').value.trim();
    $('lobby-badge').textContent = `${myName} · ${myRole}`;
    showScreen('lobby');
    llog(`Sesión iniciada: ${myName} [${myRole}]`, 'c-ok');
    return;
  }

  /* ── LOBBY: lista de partidas ──────────────────────────── */
  if (cmd === 'GAMES') {
    $('games-box').innerHTML = '';
    const matches = [...msg.matchAll(/room:(\d+)\s+players:(\d+)\s+state:(\w+)/g)];
    if (matches.length === 0) {
      appendLog('games-box', '(sin partidas activas)', 'c-sys');
    } else {
      for (const m of matches) {
        appendLog('games-box', `[${m[1]}]  jugadores:${m[2]}  estado:${m[3]}`, 'c-info');
      }
    }
    llog(`${matches.length} partida(s) encontrada(s)`, 'c-sys');
    return;
  }

  if (cmd === 'OK' && parts[1] === 'CREATE') {
    const kv = parseKV(parts.slice(2));
    myRoom   = parseInt(kv.room ?? -1);
    llog(`Sala ${myRoom} creada`, 'c-ok');
    $('lobby-badge').textContent = `${myName} · ${myRole} · sala ${myRoom}`;
    return;
  }

  if (cmd === 'OK' && parts[1] === 'JOIN') {
    const kv = parseKV(parts.slice(2));
    myRoom   = parseInt(kv.room ?? -1);
    myRole   = kv.role || myRole;
    $('lobby-badge').textContent = `${myName} · ${myRole} · sala ${myRoom}`;
    llog(`Unido a sala ${myRoom} como ${myRole}`, 'c-ok');
    return;
  }

  /* ── Inicio de partida ─────────────────────────────────── */
  if (cmd === 'GAME_START') {
    handleGameStart(parts);
    return;
  }

  /* ── QUIT (lobby o juego) ──────────────────────────────── */
  if (cmd === 'OK' && parts[1] === 'QUIT') {
    resetGame();
    showScreen('lobby');
    llog('Saliste de la partida.', 'c-sys');
    return;
  }

  /* ── GAME: movimiento ──────────────────────────────────── */
  if (cmd === 'OK' && parts[1] === 'MOVE') {
    const kv = parseKV(parts.slice(2));
    if (kv.pos) {
      const [x, y] = kv.pos.split(',').map(Number);
      myPos = { x, y };
      players[myName] = { x, y, role: myRole };
      nearRes = findResAt(x, y);
      updateHUD();
      updateActions();
      redraw();
    }
    return;
  }

  /* ── GAME: scan ────────────────────────────────────────── */
  if (cmd === 'SCAN_RESULT') {
    if (parts[1] === 'FOUND') {
      const kv = parseKV(parts.slice(2));
      const id = kv.resource_id;
      if (id !== undefined) {
        const [rx, ry] = (kv.pos || '0,0').split(',').map(Number);
        resources[id]  = Object.assign(resources[id] || { state: 'SAFE' }, { x: rx, y: ry });
        nearRes = id;
        updateActions();
        redraw();
      }
      glog(`SCAN: recurso ${kv.resource_id} en (${kv.pos})`, 'c-warn');
    } else {
      nearRes = null;
      updateActions();
      glog('SCAN: nada encontrado aquí.', 'c-sys');
    }
    return;
  }

  /* ── GAME: ataque / mitigación ─────────────────────────── */
  if (cmd === 'OK' && parts[1] === 'ATTACK') {
    const kv = parseKV(parts.slice(2));
    if (resources[kv.resource_id]) resources[kv.resource_id].state = 'ATTACKED';
    redraw();
    glog(`ATAQUE al recurso ${kv.resource_id} confirmado.`, 'c-warn');
    return;
  }

  if (cmd === 'OK' && parts[1] === 'MITIGATE') {
    const kv = parseKV(parts.slice(2));
    if (resources[kv.resource_id]) resources[kv.resource_id].state = 'SAFE';
    redraw();
    glog(`Recurso ${kv.resource_id} mitigado.`, 'c-ok');
    return;
  }

  /* ── GAME: notificaciones ──────────────────────────────── */
  if (cmd === 'NOTIFY_ATTACK') {
    const kv = parseKV(parts.slice(1));
    const id = kv.resource_id;
    const [rx, ry] = (kv.pos || '0,0').split(',').map(Number);
    resources[id]  = Object.assign(resources[id] || {}, { x: rx, y: ry, state: 'ATTACKED' });
    redraw();
    glog(`⚠ ATAQUE! Recurso ${id} por ${kv.attacker} — ${kv.timeout}s para mitigar!`, 'c-err');
    return;
  }

  if (cmd === 'NOTIFY_MITIGATED') {
    const kv = parseKV(parts.slice(1));
    if (resources[kv.resource_id]) resources[kv.resource_id].state = 'SAFE';
    redraw();
    glog(`✓ Recurso ${kv.resource_id} mitigado por ${kv.defender}`, 'c-ok');
    return;
  }

  if (cmd === 'NOTIFY_DESTROYED') {
    const kv = parseKV(parts.slice(1));
    const id = kv.resource_id;
    if (resources[id]) resources[id].state = 'DESTROYED';
    if (nearRes === id) { nearRes = null; updateActions(); }
    redraw();
    glog(`✗ Recurso ${id} DESTRUIDO`, 'c-err');
    return;
  }

  /* ── GAME: otros jugadores ─────────────────────────────── */
  if (cmd === 'PLAYER_MOVED') {
    const kv = parseKV(parts.slice(1));
    if (kv.player && kv.player !== myName) {
      const [px, py] = (kv.pos || '0,0').split(',').map(Number);
      players[kv.player] = { x: px, y: py, role: kv.role || 'UNKNOWN' };
      redraw();
    }
    return;
  }

  if (cmd === 'NOTIFY_DISCONNECT') {
    const kv = parseKV(parts.slice(1));
    if (kv.player) delete players[kv.player];
    redraw();
    glog(`[${kv.player} desconectado]`, 'c-sys');
    return;
  }

  /* ── Errores del servidor ──────────────────────────────── */
  if (cmd === 'ERROR') {
    const errText = parts.slice(1).join(' ');
    if ($('screen-login').classList.contains('active')) {
      setStatus(errText, 'err');
    } else if ($('screen-game').classList.contains('active')) {
      glog(`ERROR: ${errText}`, 'c-err');
    } else {
      llog(`ERROR: ${errText}`, 'c-err');
    }
    return;
  }

  /* ── Catch-all: mostrar en log ─────────────────────────── */
  if ($('screen-game').classList.contains('active')) {
    glog(msg, 'c-sys');
  } else if ($('screen-lobby').classList.contains('active')) {
    llog(msg, 'c-sys');
  }
}

/* ═══════════════════════════════════════════════════════════
   INICIO DE PARTIDA
   ═══════════════════════════════════════════════════════════ */
function handleGameStart(parts) {
  const kv = parseKV(parts.slice(1));

  if (kv.pos) {
    const [x, y] = kv.pos.split(',').map(Number);
    myPos = { x, y };
    players[myName] = { x, y, role: myRole };
  }

  /* Defensores reciben coordenadas de recursos; atacantes reciben '?' */
  const resStr = kv.resources || '';
  if (resStr && resStr !== '?') {
    resStr.split(';').forEach((r, i) => {
      const [rx, ry] = r.split(',').map(Number);
      resources[String(i)] = { x: rx, y: ry, state: 'SAFE' };
    });
  }

  nearRes = findResAt(myPos.x, myPos.y);
  showScreen('game');
  updateHUD();
  updateActions();
  redraw();
  glog(`Partida iniciada! Rol: ${myRole}  Pos: (${myPos.x},${myPos.y})`, 'c-ok');
  if (myRole === 'ATTACKER') {
    glog('Muévete, usa SCAN para encontrar recursos y luego ATTACK.', 'c-sys');
  } else {
    glog('Espera alertas NOTIFY_ATTACK y usa MITIGATE en los recursos atacados.', 'c-sys');
  }
}

function findResAt(x, y) {
  for (const [id, r] of Object.entries(resources)) {
    if (r.x === x && r.y === y && r.state !== 'DESTROYED') return id;
  }
  return null;
}

function resetGame() {
  players = {}; resources = {}; nearRes = null;
  myPos = { x: -1, y: -1 }; myRoom = -1;
}

/* ═══════════════════════════════════════════════════════════
   HUD y BOTONES DE ACCIÓN
   ═══════════════════════════════════════════════════════════ */
function updateHUD() {
  $('hud').innerHTML =
    `<span class="c-sys">USER  </span>${myName}<br>` +
    `<span class="c-sys">ROL   </span>` +
    `<span class="${myRole === 'ATTACKER' ? 'c-warn' : 'c-info'}">${myRole}</span><br>` +
    `<span class="c-sys">POS   </span>(${myPos.x}, ${myPos.y})<br>` +
    `<span class="c-sys">SALA  </span>${myRoom}`;
}

function updateActions() {
  const res   = nearRes !== null ? resources[nearRes] : null;
  const alive = res && res.state !== 'DESTROYED';
  if (myRole === 'ATTACKER') {
    $('btn-attack').classList.toggle('hide', !(alive && res.state === 'SAFE'));
    $('btn-mitigate').classList.add('hide');
  } else {
    $('btn-mitigate').classList.toggle('hide', !(alive && res.state === 'ATTACKED'));
    $('btn-attack').classList.add('hide');
  }
}

/* ═══════════════════════════════════════════════════════════
   CANVAS — MAPA 20×20
   ═══════════════════════════════════════════════════════════ */
function redraw() {
  const canvas = $('map');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W   = CELL * GRID;

  /* Fondo */
  ctx.fillStyle = C.bg;
  ctx.fillRect(0, 0, W, W);

  /* Cuadrícula */
  ctx.strokeStyle = C.grid;
  ctx.lineWidth   = 0.5;
  for (let i = 0; i <= GRID; i++) {
    ctx.beginPath(); ctx.moveTo(i * CELL, 0); ctx.lineTo(i * CELL, W); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, i * CELL); ctx.lineTo(W, i * CELL); ctx.stroke();
  }

  /* Etiquetas de eje cada 5 celdas */
  ctx.fillStyle    = '#2a4a2a';
  ctx.font         = '8px monospace';
  ctx.textAlign    = 'center';
  ctx.textBaseline = 'top';
  for (let i = 0; i < GRID; i += 5) ctx.fillText(String(i), i * CELL + CELL / 2, 1);
  ctx.textAlign    = 'right';
  ctx.textBaseline = 'middle';
  for (let i = 0; i < GRID; i += 5) ctx.fillText(String(i), CELL - 2, i * CELL + CELL / 2);

  /* Recursos */
  for (const [id, r] of Object.entries(resources)) {
    const color = r.state === 'SAFE'     ? C.safe
                : r.state === 'ATTACKED' ? C.attacked
                :                          C.dest;
    const px = r.x * CELL + 4, py = r.y * CELL + 4, sz = CELL - 8;
    ctx.fillStyle = color + '22';
    ctx.fillRect(px, py, sz, sz);
    ctx.strokeStyle = color;
    ctx.lineWidth   = 1.5;
    ctx.strokeRect(px, py, sz, sz);
    ctx.fillStyle    = color;
    ctx.font         = 'bold 9px monospace';
    ctx.textAlign    = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(`R${id}`, r.x * CELL + CELL / 2, r.y * CELL + CELL / 2);
  }

  /* Otros jugadores */
  for (const [name, p] of Object.entries(players)) {
    if (name === myName) continue;
    drawDot(ctx, p.x, p.y, p.role === 'ATTACKER' ? C.atk : C.def, name[0], false);
  }

  /* Jugador propio (encima de todo) */
  if (myPos.x >= 0) drawDot(ctx, myPos.x, myPos.y, C.me, myName[0] || '?', true);
}

function drawDot(ctx, gx, gy, color, label, isMe) {
  const cx = gx * CELL + CELL / 2;
  const cy = gy * CELL + CELL / 2;
  const r  = isMe ? 10 : 8;
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.fill();
  if (isMe) {
    ctx.strokeStyle = '#ffffff66';
    ctx.lineWidth   = 2;
    ctx.stroke();
  }
  ctx.fillStyle    = '#000';
  ctx.font         = `bold ${isMe ? 10 : 9}px monospace`;
  ctx.textAlign    = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(label.toUpperCase(), cx, cy);
}

/* ═══════════════════════════════════════════════════════════
   TECLADO  (WASD + flechas)
   ═══════════════════════════════════════════════════════════ */
const DIR_KEYS = {
  ArrowUp: 'NORTH', ArrowDown: 'SOUTH', ArrowLeft: 'WEST', ArrowRight: 'EAST',
  w: 'NORTH', s: 'SOUTH', a: 'WEST', d: 'EAST',
  W: 'NORTH', S: 'SOUTH', A: 'WEST', D: 'EAST',
};

document.addEventListener('keydown', e => {
  if (e.target.tagName === 'INPUT') return;
  if (!$('screen-game').classList.contains('active')) return;
  const dir = DIR_KEYS[e.key];
  if (dir) { e.preventDefault(); send(`MOVE ${dir}`); }
});

/* ═══════════════════════════════════════════════════════════
   BINDINGS DE EVENTOS
   ═══════════════════════════════════════════════════════════ */
document.addEventListener('DOMContentLoaded', () => {

  /* Login */
  $('inp-pass').addEventListener('keydown', e => { if (e.key === 'Enter') $('btn-login').click(); });
  $('btn-login').addEventListener('click', () => {
    const user = $('inp-user').value.trim();
    const pass = $('inp-pass').value.trim();
    if (!user || !pass) { setStatus('Ingresa usuario y contraseña.', 'err'); return; }
    send(`LOGIN ${user} ${pass}`);
    setStatus('Autenticando…');
  });

  /* Lobby */
  $('btn-list').addEventListener('click',   () => send('LIST_GAMES'));
  $('btn-create').addEventListener('click', () => send('CREATE_GAME'));
  $('btn-start').addEventListener('click',  () => send('START_GAME'));
  $('btn-lquit').addEventListener('click',  () => send('QUIT'));
  $('btn-join').addEventListener('click', async () => {
    const id = await showModal('ID de la sala a unirse:');
    if (id) send(`JOIN_GAME ${id}`);
  });

  /* D-pad */
  document.querySelectorAll('.dpad-btn').forEach(btn => {
    btn.addEventListener('click', () => send(`MOVE ${btn.dataset.dir}`));
  });

  /* Acciones de juego */
  $('btn-scan').addEventListener('click',     () => send('SCAN'));
  $('btn-gstatus').addEventListener('click',  () => send('STATUS'));
  $('btn-gquit').addEventListener('click',    () => send('QUIT'));
  $('btn-attack').addEventListener('click',   () => { if (nearRes !== null) send(`ATTACK ${nearRes}`); });
  $('btn-mitigate').addEventListener('click', () => { if (nearRes !== null) send(`MITIGATE ${nearRes}`); });

  /* Iniciar conexión al bridge local */
  connectWS();
});
