<script>
  // Black header band: logo, one-line port summary, link lights, tabs.
  import Icon from './lib/Icon.svelte';
  import Mark from './lib/Mark.svelte';
  import ProbeBadge from './lib/ProbeBadge.svelte';
  import { portName, isStale, fmtDur } from './lib/format.js';
  import { app } from './lib/app.svelte.js';
  const TABS = [['port', 'port', 'Port'], ['tools', 'ping', 'Tools'], ['log', 'log', 'Log'], ['setup', 'sliders', 'Setup']];

  let { s, cur, neighbors, offline, lastOk, fails } = $props();

  let up = $derived((s && s.stp || {}).link_up_for_s || 0);
  let dhcp = $derived((s && s.port && s.port.dhcp) || {});
  let nn = $derived(neighbors.length + ((s && s.neighbors_omitted) || 0));
  let lost = $derived(fails >= 3);
  // the rest of the summary line after the port name
  let parts = $derived(!cur ? [] : [
    cur.pvid !== undefined ? ' · VLAN ' + cur.pvid : '',
    nn > 1 ? ` · ${nn} neighbors` + (s.neighbors_omitted ? ` (${s.neighbors_omitted} not shown)` : '') : '',
    !dhcp.bound && up >= 30 && !dhcp.offers ? ' · no DHCP reply' : '',
    isStale(cur) ? ' · stale' : '',
  ]);
  let ago = $state(0);
  $effect(() => {
    if (!offline) return;
    const t = setInterval(() => ago = Math.round((Date.now() - lastOk) / 1000), 1000);
    ago = Math.round((Date.now() - lastOk) / 1000);
    return () => clearInterval(t);
  });
</script>

<header>
  <div class="hbar">
    <div class="brand">
      <span class="logo"><Mark/>picoplug{#if s}<ProbeBadge n={s.probe}/>{/if}</span>
      <a class="hstat" href="#port">
        {#if lost}<span class="bad">No device</span>
        {:else if !s}Connecting…
        {:else if s.eth.chip_ok === false}<span class="bad">Ethernet chip fault</span>
        {:else if !s.eth.link}No link
        {:else if !cur}Listening… {up} s
        {:else if cur.picoplug}No switch · {cur.sys_name || 'picoplug'}
        {:else}<b>{portName(cur)}</b>{parts.join('')}
        {/if}
      </a>
    </div>
    <div class="chips">
      <span class="chip" class:on={s && s.eth.link && !lost} class:off={!s || !s.eth.link || lost}><Icon name="port" size="s"/><span>{s && s.eth.link ? `${s.eth.speed}M ${s.eth.duplex === 'full' ? 'FD' : 'HD'}` : 'no link'}</span><i class="dot"></i></span>
      <span class="chip" class:on={s && !lost} class:off={lost}><Icon name="usb" size="s"/>USB<i class="dot"></i></span>
    </div>
  </div>
  <nav class="tabs" aria-label="Sections" hidden={app.readonly}>
    {#each TABS as [v, icon, label] (v)}
      <a href="#{v}" class:on={app.view === v} aria-current={app.view === v ? 'page' : undefined}><Icon name={icon}/>{label}</a>
    {/each}
  </nav>
  {#if offline}
    <div class="conn" role="status"><span class="spin"></span><span>Reconnecting <small>· last reply {fmtDur(ago)} ago{fails >= 3 ? ' · check USB' : ''}</small></span></div>
  {/if}
</header>
