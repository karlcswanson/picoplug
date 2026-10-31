<script>
  // Black header band: logo, one-line port summary, link lights, tabs.
  import Icon from './lib/Icon.svelte';
  import { isP, probeTitle, portName, isStale, fmtDur } from './lib/format.js';
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
      <span class="logo"><svg class="mark" viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="#f2f0e8" stroke-linecap="round" stroke-linejoin="round" stroke-width="1.75"><path d="M6.25 8.5 2.75 12l3.5 3.5"/><path d="M17.75 8.5 21.25 12l-3.5 3.5"/></g><circle cx="8" cy="12" r="1.3" fill="#2fd46f"/><circle cx="12" cy="12" r="1.3" fill="#ffcf00"/><circle cx="16" cy="12" r="1.3" fill="#ff4d4d"/></svg>picoplug{#if s && isP(s.probe)}<span class="pb pb{s.probe}" title={probeTitle(s.probe)}>{s.probe}</span>{/if}</span>
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
