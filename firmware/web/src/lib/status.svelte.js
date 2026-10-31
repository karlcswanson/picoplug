// Polls /status.json every 2 s. Components read `conn.status` and Svelte
// updates only what changed, in place.

export const conn = $state({ status: null, lastOk: 0, fails: 0 });

let timer = 0, inFlight = false;

export async function tick() {
  clearTimeout(timer);
  if (inFlight) return;
  inFlight = true;
  const ac = new AbortController(), to = setTimeout(() => ac.abort(), 4000);
  try {
    const r = await fetch('/status.json', { cache: 'no-store', signal: ac.signal });
    if (!r.ok) throw new Error(r.status);
    conn.status = await r.json();
    conn.lastOk = Date.now(); conn.fails = 0;
  } catch (e) {
    conn.fails++;
  }
  clearTimeout(to); inFlight = false;
  timer = setTimeout(tick, conn.fails ? Math.min(1000 * conn.fails, 3000) : 2000);
}

export function start() {
  // check at once when the app comes back to the foreground or the network returns
  for (const ev of ['online', 'pageshow', 'focus']) window.addEventListener(ev, () => tick());
  document.addEventListener('visibilitychange', () => { if (!document.hidden) tick(); });
  tick();
}
