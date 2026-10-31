<script>
  // Ping and trace (results come with every status poll), recent runs, and the port blinker.
  import Icon from '../lib/Icon.svelte';
  import Kv from '../lib/Kv.svelte';
  import { conn } from '../lib/status.svelte.js';
  import { app, targets, pickTarget, tool } from '../lib/app.svelte.js';
  import { runs, fmtTime } from '../lib/history.svelte.js';
  import { say } from '../lib/copy.svelte.js';
  import { fmtAgo } from '../lib/format.js';

  let s = $derived(conn.status);
  let t = $derived(s && s.tool);
  let blink = $derived(s && s.blink);
  let running = $derived(t && t.running ? t.kind : null);
  let blinking = $derived(!!(blink && blink.active));
  let tg = $derived(conn.status ? targets() : {});
  let hostEl, cardEl, cardW = $state(0);
  // the ping chart is drawn to the card's width, measured on every update and on resize
  const measure = () => cardW = cardEl.clientWidth;
  $effect(() => { conn.status; measure(); });
  $effect(() => { addEventListener('resize', measure); return () => removeEventListener('resize', measure); });
  const QT = [['gw', 'Gateway'], ['inet', 'Internet'], ['dns', 'DNS'], ['sw', 'Switch']];

  // the gateway is the default target until something else is typed
  $effect(() => {
    if (!app.host && document.activeElement !== hostEl && tg.gw) { app.host = tg.gw; app.qt = 'gw'; }
  });

  let ping = $derived.by(() => {
    if (!t || t.kind !== 'ping') return null;
    const rt = t.rtts || [];
    const infl = t.running && rt.length && rt[rt.length - 1] === null ? 1 : 0;
    const ns = t.sent - infl, lost = ns - t.recv;
    return { ns, lost };
  });
  let hops = $derived((t && t.hops) || []);

  // bar per probe over the last 40: height = RTT, full-height grey bar = no reply
  let plot = $derived.by(() => {
    if (!ping) return null;
    const n = 40;
    let rt = t.rtts || [], fs = t.first_seq || 1;
    if (rt.length > n) { fs += rt.length - n; rt = rt.slice(-n); }
    const W = Math.max(240, (cardW || 320) - 34), H = 70, PL = 30, PB = 12, PT = 4, bw = (W - PL) / n;
    const vals = rt.filter(r => r !== null), mx = Math.max(1, ...vals);
    const top = [5, 10, 15, 20, 30, 50, 75, 100, 150, 200, 300, 500, 750, 1000].find(v => mx <= v) || Math.ceil(mx / 1000) * 1000;
    const y = v => PT + (H - PT - PB) * (1 - v / top);
    const off = n - rt.length, bars = [];
    rt.forEach((r, i) => {
      const x = PL + (off + i) * bw + 1, seq = fs + i, w = Math.max(1, bw - 2);
      if (r === null) { if (!(t.running && i === rt.length - 1)) bars.push({ to: true, x, y: PT, w, h: H - PT - PB, tip: '#' + seq + ': no reply' }); }
      else { const h = Math.max(1.5, y(0) - y(r)); bars.push({ x, y: y(0) - h, w, h, tip: '#' + seq + ': ' + (r === 0 ? '<1' : r) + ' ms' }); }
    });
    return { W, H, PL, PT, top, y, bars, n };
  });

  function run(kind) {
    if (running === kind) { fetch('/tool?stop=1').catch(() => {}); return; }
    const h = app.host.trim();
    if (!h) { say('Enter a host'); return; }
    hostEl.blur();
    tool(kind + '=' + encodeURIComponent(h) + (kind === 'ping' ? '&count=0' : ''));
  }
  function again(x) { app.host = x.h; app.qt = ''; run(x.k); }
</script>

<div class="card" id="tcard" bind:this={cardEl}>
  <div class="chead"><span class="sec"><Icon name="ping" size="s"/>Ping and trace</span></div>
  <label class="fl" for="t_host">Host or IP</label>
  <input id="t_host" class="in" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" inputmode="url"
    bind:this={hostEl} bind:value={app.host} onkeydown={e => { if (e.key === 'Enter') { e.preventDefault(); run('ping'); } }}>
  <div class="qt">
    {#each QT as [k, label] (k)}
      <button class:sel={app.qt === k && app.host === tg[k]} disabled={!tg[k]} onclick={() => pickTarget(k)}>{label}</button>
    {/each}
  </div>
  <div class="row2" style="margin-top:8px">
    <button class={running === 'ping' ? 'stop' : 'p'} disabled={running === 'trace'} onclick={() => run('ping')}><Icon name={running === 'ping' ? 'stop' : 'ping'}/><span>{running === 'ping' ? 'Stop' : 'Ping'}</span></button>
    <button class={running === 'trace' ? 'stop' : ''} disabled={running === 'ping'} onclick={() => run('trace')}><Icon name={running === 'trace' ? 'stop' : 'trace'}/><span>{running === 'trace' ? 'Stop' : 'Trace'}</span></button>
  </div>
  {#if t && t.kind !== 'idle'}
    <div class="tout">
      <div class="thd"><Icon name={t.kind}/><span>{t.kind} {t.host}{#if t.addr && t.addr !== t.host}{' '}<span class="c-mut">{t.addr}</span>{/if}</span>
        {#if t.running}<span class="live on"><i></i>{t.kind === 'ping' && t.count === 0 ? 'continuous' : 'running'}</span>
        {:else}<span class="live"><i></i>done{t.age_s >= 60 ? ', ' + fmtAgo(t.age_s) : ''}</span>{/if}
      </div>
      {#if ping}
        <svg class="tplot" viewBox="0 0 {plot.W} {plot.H}" role="img" aria-label="round-trip time, last {plot.n} pings">
          {#each [0, plot.top / 2, plot.top] as v}<line x1={plot.PL} y1={plot.y(v)} x2={plot.W} y2={plot.y(v)}/>{/each}
          <text x="0" y={plot.PT + 9}>{plot.top}</text><text x="0" y={plot.y(plot.top / 2) + 4}>{plot.top / 2}</text><text x="0" y={plot.y(0) + 3}>ms</text>
          {#each plot.bars as b}<rect class:to={b.to} x={b.x} y={b.y} width={b.w} height={b.h}><title>{b.tip}</title></rect>{/each}
        </svg>
        <div class="legend"><span><i></i>reply</span><span><i class="to"></i>no reply</span></div>
        <div class="grid">
          <Kv k="Sent">{String(ping.ns)}</Kv>
          <Kv k="Lost">{ping.ns ? `${ping.lost} (${Math.round(100 * ping.lost / ping.ns)}%)` : '0'}</Kv>
          <Kv k="Avg">{t.recv ? (t.avg_ms || '<1') + ' ms' : '-'}</Kv>
          <Kv k="Min / max">{t.recv ? (t.min_ms || '<1') + '–' + (t.max_ms || '<1') + ' ms' : '-'}</Kv>
        </div>
        <div class="ln">{ping.ns} sent, {t.recv} received, {ping.ns ? Math.round(100 * ping.lost / ping.ns) : 0}% loss</div>
      {:else}
        <table class="trace"><tbody>
          {#each hops as h}
            <tr><td>{h.ttl}</td><td>{#if h.addr}{h.addr}{:else if h.timeout}<span class="c-mut">* no reply</span>{:else}…{/if}{#if h.reached}{' '}<Icon name="ok" size="s c-ok"/>{/if}{h.unreachable ? ' unreachable' : ''}</td><td>{h.addr ? (h.ms === 0 ? '<1' : h.ms) + ' ms' : ''}</td></tr>
          {/each}
        </tbody></table>
        {#if t.reached}<div class="tmsg c-ok">Reached in {hops.length} hop{hops.length === 1 ? '' : 's'}</div>
        {:else if !t.running && !t.error}<div class="tmsg">Target not reached</div>{/if}
      {/if}
      {#if t.error}<div class="tmsg">{t.error}</div>{/if}
    </div>
  {/if}
</div>
{#if runs.list.length}
  <div class="card">
    <div class="chead"><span class="sec"><Icon name="clock" size="s"/>Recent runs</span></div>
    <div class="runs">
      {#each runs.list as x}
        <div class="run"><Icon name={x.k}/><div class="lb"><b>{x.h}</b><small>{x.k} · {x.s || ''} · {fmtTime(x.t)}</small></div>
          <button class="q" aria-label="Run {x.k} to {x.h} again" disabled={!!running} onclick={() => again(x)}><Icon name="redo"/></button></div>
      {/each}
    </div>
  </div>
{/if}
<div class="card">
  <div class="chead"><span class="sec"><Icon name="flash" size="s"/>Find this port</span></div>
  <div class="ln" style="margin:0 0 10px">Blinks the switch port LED for 32 s</div>
  <div class="crow">
    <button class={blinking ? 'stop' : ''} onclick={() => tool('blink=' + (blinking ? '0' : '1'))}><Icon name={blinking ? 'stop' : 'flash'}/><span>{blinking ? 'Stop blinking' : 'Blink port'}</span></button>
    <span class="meta" style="text-align:left">{blinking && blink.pulse ? 'path test · link down' : blinking ? (blink.remaining_s ? 'blinking · ' + blink.remaining_s + ' s left' : 'waiting for link…') : ''}</span>
  </div>
</div>
