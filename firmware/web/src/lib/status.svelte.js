// Live status. The firmware pushes every change over a WebSocket at /ws (and a heartbeat
// every 2 s); while that is down, /status.json is polled every 2 s instead. Components read
// `conn.status` and Svelte updates only what changed, in place.

export const conn = $state({ status: null, lastOk: 0, fails: 0, live: false });

const QUIET_MS = 5000;          // no push for this long (heartbeat is 2 s): treat the socket as dead
let timer = 0, inFlight = false;
let ws = null, wsTries = 0, wsTimer = 0, quiet = 0;

function got(s) {
  conn.status = s;
  conn.lastOk = Date.now(); conn.fails = 0;
}

export async function tick() {
  clearTimeout(timer);
  if (conn.live) return;        // the socket is delivering
  if (inFlight) return;
  inFlight = true;
  const ac = new AbortController(), to = setTimeout(() => ac.abort(), 4000);
  try {
    const r = await fetch('/status.json', { cache: 'no-store', signal: ac.signal });
    if (!r.ok) throw new Error(r.status);
    got(await r.json());
  } catch (e) {
    conn.fails++;
  }
  clearTimeout(to); inFlight = false;
  if (!conn.live) timer = setTimeout(tick, conn.fails ? Math.min(1000 * conn.fails, 3000) : 2000);
}

function dropSocket() {
  clearTimeout(quiet);
  if (ws) { ws.onopen = ws.onmessage = ws.onclose = ws.onerror = null; try { ws.close(); } catch (e) {} }
  ws = null;
  if (conn.live) { conn.live = false; tick(); }
  // retry soon at first, then back off to 15 s (a board without /ws, or one with its slots full)
  clearTimeout(wsTimer);
  wsTimer = setTimeout(openSocket, Math.min(1000 * 2 ** wsTries++, 15000));
}

function openSocket() {
  clearTimeout(wsTimer);
  if (ws || !/^https?:$/.test(location.protocol) || !('WebSocket' in window)) return;
  try { ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws'); }
  catch (e) { ws = null; return; }
  ws.onmessage = e => {
    let s; try { s = JSON.parse(e.data); } catch (x) { return; }
    wsTries = 0;
    if (!conn.live) { conn.live = true; clearTimeout(timer); }
    got(s);
    clearTimeout(quiet); quiet = setTimeout(dropSocket, QUIET_MS);
  };
  ws.onclose = ws.onerror = dropSocket;
}

// back in the foreground or back online: a socket that went quiet meanwhile is dropped, and
// the page asks right away instead of waiting
function wake() {
  if (conn.live && Date.now() - conn.lastOk > QUIET_MS) dropSocket();
  if (!ws) { wsTries = 0; openSocket(); }
  tick();
}

export function start() {
  for (const ev of ['online', 'pageshow', 'focus']) window.addEventListener(ev, wake);
  document.addEventListener('visibilitychange', () => { if (!document.hidden) wake(); });
  tick();
  openSocket();
}
