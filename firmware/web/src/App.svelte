<script>
  import { untrack } from 'svelte';
  import Sprite from './lib/Sprite.svelte';
  import Icon from './lib/Icon.svelte';
  import Header from './Header.svelte';
  import NeighborCard from './cards/NeighborCard.svelte';
  import Listening from './cards/Listening.svelte';
  import NoLink from './cards/NoLink.svelte';
  import LastPort from './cards/LastPort.svelte';
  import PortCard from './cards/PortCard.svelte';
  import StpCard from './cards/StpCard.svelte';
  import LacpCard from './cards/LacpCard.svelte';
  import Dot1xCard from './cards/Dot1xCard.svelte';
  import McastCard from './cards/McastCard.svelte';
  import PtpCard from './cards/PtpCard.svelte';
  import PeerCard from './cards/PeerCard.svelte';
  import Tools from './views/Tools.svelte';
  import Log from './views/Log.svelte';
  import Settings from './views/Settings.svelte';
  import { conn, start, remote, scanPeers } from './lib/status.svelte.js';
  import { sel } from './lib/ui.svelte.js';
  import { toast } from './lib/copy.svelte.js';
  import { app, route, go, formSet } from './lib/app.svelte.js';
  import { historyUpsert, runAdd, toolSum } from './lib/history.svelte.js';
  import { pickCurrent, orderNeighbors, portName, isP, hKey } from './lib/format.js';

  let s = $derived(conn.status);
  let neighbors = $derived(orderNeighbors(s && s.neighbors));
  let cur = $derived(pickCurrent(neighbors, sel.key));
  let up = $derived((s && s.stp || {}).link_up_for_s || 0);
  let offline = $derived(!!conn.lastOk && conn.fails > 0);
  let linked = $derived(s && s.eth.link && s.eth.chip_ok !== false);
  let portUp = $derived(s && s.port && (s.port.link ?? s.eth.link));
  // the Port tab shows this tester, or another one read through it (Other probes · View)
  let rs = $derived(remote.name ? remote.status : s);
  let rneighbors = $derived(remote.name ? orderNeighbors(rs && rs.neighbors) : neighbors);
  let rcur = $derived(remote.name ? pickCurrent(rneighbors, sel.key) : cur);
  let rup = $derived(remote.name ? (rs && rs.stp || {}).link_up_for_s || 0 : up);
  let rlinked = $derived(rs && rs.eth.link && rs.eth.chip_ok !== false);
  let rportUp = $derived(rs && rs.port && (rs.port.link ?? rs.eth.link));
  let rprobe = $derived(!remote.name ? null : remote.status && remote.status.probe != null ? remote.status.probe
    : ((s && s.peers && s.peers.list || []).find(p => p.name === remote.name) || {}).probe);
  let rnote = $derived(!remote.name ? '' : remote.error ? remote.error + (remote.status ? ' · showing the last copy' : '')
    : !remote.status ? 'reading…' : remote.age > 5000 ? 'read ' + Math.round(remote.age / 1000) + ' s ago' : 'live');

  // the header status line and the browser tab title carry the answer on every tab
  $effect(() => {
    if (!s) { document.title = conn.fails >= 3 ? 'No device · picoplug' : 'picoplug'; return; }
    const t = conn.fails >= 3 ? 'No device' : s.eth.chip_ok === false ? 'Chip fault' : !s.eth.link ? 'No link' : !cur ? 'Listening'
      : cur.picoplug ? 'No switch' : portName(cur) + (cur.pvid !== undefined ? ' · VLAN ' + cur.pvid : '');
    document.title = t + ' · picoplug' + (s && isP(s.probe) && conn.fails < 3 ? '-' + s.probe : '');
  });
  $effect(() => { document.body.classList.toggle('offline', offline); });
  // record each status in Ports visited; untracked so writing the history doesn't re-run this
  $effect(() => { const st = s; if (st) untrack(() => historyUpsert(st)); });
  // a finished ping or trace goes into Recent whichever tab is open
  $effect(() => {
    const t = s && s.tool;
    if (t && t.kind !== 'idle' && !t.running && !t.error) untrack(() => runAdd(t.kind, t.host, toolSum(t), t.generation));
  });
  // a picked neighbor that has gone is forgotten, so it isn't picked again if it comes back
  $effect(() => { if (s && sel.key && !neighbors.some(n => hKey(n) === sel.key)) untrack(() => sel.key = null); });
  // the splash waits 800 ms so a quick first reply never flashes it
  let splash = $state(false);
  setTimeout(() => splash = true, 800);

  addEventListener('hashchange', route);
  // in-page links switch tabs without navigating (iOS home-screen apps can reload on a link tap)
  document.addEventListener('click', e => {
    const a = e.target.closest('a[href^="#"]');
    if (!a || e.defaultPrevented) return;
    e.preventDefault(); go(a.getAttribute('href'));
  });
  // served from the port side: the device answers 403 to everything but the read-only paths, so only the Port tab is shown
  fetch('/tool').then(r => { if (r.status === 403) { app.readonly = true; route(); } }).catch(() => {});
  fetch('/settings').then(r => r.ok ? r.json() : null).then(j => { if (j && !app.readonly) { formSet(j); scanPeers(); } }).catch(() => {});
  route();
  start();
</script>

<Sprite/>
<Header {s} {cur} {neighbors} {offline} lastOk={conn.lastOk} fails={conn.fails} remote={app.view === 'port' && remote.name ? { name: remote.name, probe: rprobe, note: rnote } : null}/>
<main>
  {#if !s && splash}
    <div class="splash" role="status"><span class="spin"></span><b>picoplug</b><span>Connecting…</span></div>
  {/if}
  {#if app.view === 'port'}
    <section>
    {#if rs}
      {#if rs.eth.chip_ok === false}
        <div class="alert">
          <div class="t"><Icon name="fail"/>Ethernet chip not responding</div>
          <p>VERSIONR {rs.eth.version} · {rs.eth.init_attempts} tries · replug</p>
        </div>
      {:else if !rs.eth.link}
        <NoLink/>{#if !remote.name}<LastPort/>{/if}
      {/if}
      {#if rcur}
        <NeighborCard n={rcur} neighbors={rneighbors}/>
      {:else if rlinked}
        <Listening up={rup}/>
      {/if}
      {#if rlinked}
        {#if rs.port && rportUp}<PortCard p={rs.port} stp={rs.stp}/>{/if}
        <StpCard st={rs.stp} up={rup}/>
        <LacpCard l={rs.lacp} up={rup}/>
        <Dot1xCard x={rs.dot1x} up={rup}/>
        <McastCard s={rs} cur={rcur} up={rup}/>
        <PtpCard pt={rs.ptp} up={rup}/>
        {#if !remote.name}<PeerCard ps={rs.peers} cur={rcur} up={rup}/>{/if}
      {/if}
    {/if}
      {#if app.readonly}
        <div class="card">
          <div class="nrow" style="color:var(--mut);font-size:14px"><Icon name="lock"/><span>Read-only · port side</span></div>
        </div>
      {/if}
    </section>
  {:else if app.view === 'tools'}
    <section><Tools/></section>
  {:else if app.view === 'log'}
    <section><Log/></section>
  {:else}
    <section><Settings/></section>
  {/if}
</main>
<div class="toast" class:on={toast.on} role="status">{toast.msg}</div>
