<script module>
  // stays open across tab switches and link drops, as on the shipping page
  const dopt = $state({ open: false });
</script>

<script>
  // Network: the address DHCP gave, the three reachability checks, lease and options on demand.
  import Icon from '../lib/Icon.svelte';
  import Kv from '../lib/Kv.svelte';
  import Parts from '../lib/Parts.svelte';
  import Ln from '../lib/Ln.svelte';
  import More from '../lib/More.svelte';
  import { sg, nw, ok, yn } from '../lib/parts.js';
  import { fmtAgo, fmtDur } from '../lib/format.js';
  import { dhcpOpts } from '../lib/dhcp.js';
  import { ui } from '../lib/ui.svelte.js';
  import { pickTarget, go, tool } from '../lib/app.svelte.js';
  let { p, stp } = $props();

  let d = $derived(p.dhcp || {});
  let pr = $derived(p.probe || {});
  let open = $derived(!!ui.port);
  let up = $derived((p.stp || stp || {}).link_up_for_s || 0);
  let dhcpFailed = $derived(!d.bound && up >= 30 && !d.offers);
  let opts = $derived(dopt.open && d.raw ? dhcpOpts(d.raw) : []);
  let hasDet = $derived(d.bound || !!pr.gateway);
  let hint = $derived([d.bound ? 'DHCP, lease' : '', pr.gateway ? 'ping details' : ''].filter(Boolean).join(', '));
  let checks = $derived.by(() => {
    if (pr.gateway) {
      const g = pr.gateway, i = pr.internet, n = pr.dns;
      return [['gw', 'Gateway', g.reachable ? 'ok' : 'bad', d.gateway, g.reachable ? (g.rtt_ms || '<1') + ' ms' : 'no reply'],
        ['inet', 'Internet', i.reachable ? 'ok' : 'bad', i.target, i.reachable ? (i.rtt_ms || '<1') + ' ms' : 'no reply'],
        ['dns', 'DNS', n.ok ? 'ok' : 'bad', n.name + (n.custom ? ' via ' + n.server : ''), n.ok ? (n.ms || '<1') + ' ms' : 'failed']];
    }
    if (d.bound) return [['gw', 'Gateway', 'mut', d.gateway, 'checking…'], ['inet', 'Internet', 'mut', pr.internet && pr.internet.target, ''], ['dns', 'DNS', 'mut', '', '']];
    return [['gw', 'Gateway', 'mut', '', 'skipped'], ['inet', 'Internet', 'mut', '', 'skipped'], ['dns', 'DNS', 'mut', '', 'skipped']];
  });
  // each check row opens Tools with that target
  function check(e, key) { e.preventDefault(); pickTarget(key); go('#tools'); }
</script>

{#snippet lines(list)}{#each list as a, i}{#if i}<br>{/if}<span class="nw">{a}</span>{/each}{/snippet}

<div class="card">
  <div class="chead"><span class="sec"><Icon name="port" size="s"/>Network</span>{#if pr.age_s !== undefined}<span class="meta">checked {fmtAgo(pr.age_s)}</span>{/if}</div>
  <div class="grid" style="margin-top:0">
    {#if d.bound}
      <Kv k="Address" wide copy={d.ip}>
        <span class="nw">{d.ip}</span> <span class="c-mut">/ <span class="nw">{d.mask}</span></span>
        <Ln p={[['DHCP from ', sg(d.server || '?')], d.lease_s ? 'lease ' + fmtDur(d.lease_s) : '']}/>
      </Kv>
      {#if (d.servers || []).length > 1}<Kv k="DHCP servers answered" wide>{@render lines(d.servers)}</Kv>{/if}
    {:else}
      <Kv k="DHCP" wide>{dhcpFailed ? 'no reply' : d.state || '…'}<span class="c-mut">{d.offers ? ' · ' + d.offers + ' offer' + (d.offers > 1 ? 's' : '') : up >= 10 ? ' · ' + fmtDur(up) : ''}{d.naks ? ' · NAK' : ''}</span></Kv>
      {#if up >= 10}<div style="grid-column:1/-1"><button class="q" onclick={() => tool('dhcp=1', 'DHCP restarted')}><Icon name="redo"/>Retry DHCP</button></div>{/if}
    {/if}
    <div class="chks">
      {#each checks as [key, label, state, sub, lat] (key)}
        <a class="chk" href="#tools" onclick={e => check(e, key)}>
          {#if state === 'ok'}<Icon name="ok" size="c-ok"/>{:else if state === 'mut'}<Icon name="clock" size="c-mut"/>{/if}
          <span class="lb">{label}{#if sub}<small>{sub}</small>{/if}</span><span class="lat" class:c-mut={state === 'mut'}>{lat || ''}</span><Icon name="chev-r"/>
        </a>
      {/each}
    </div>

    {#if open && d.bound}
      <div class="grp"><Icon name="lease" size="s"/>DHCP</div>
      <Kv k="DNS server" wide copy={(d.dns || []).join('\n')}>{#if (d.dns || []).length}{@render lines(d.dns)}{:else}-{/if}</Kv>
      <Kv k="DHCP server" wide copy={d.server || ''}>
        <Parts p={sg(d.server || '-')}/>{#if d.server_mac}{' '}<Ln p={sg(d.server_mac)}/>{/if}{#if d.relay}<Ln p={['via relay ', sg(d.relay)]} sep=""/>{/if}
      </Kv>
      <Kv k="Lease" wide><Parts p={[sg(fmtDur(d.lease_s)), d.t1_s ? sg(fmtDur(d.t1_s), 'renew') : '',
        d.acquire_ms ? sg(d.acquire_ms < 1000 ? d.acquire_ms + ' ms' : (d.acquire_ms / 1000).toFixed(1) + ' s', 'got it in') : '']}/></Kv>
      {#if d.domain || d.search}<Kv k="Domain" wide>{d.domain || ''}{#if d.search && d.search !== d.domain}<Ln p={'search ' + d.search}/>{/if}</Kv>{/if}
      {#if d.ntp && d.ntp.length}<Kv k="NTP" wide>{@render lines(d.ntp)}</Kv>{/if}
      {#if d.wins && d.wins.length}<Kv k="WINS" wide>{@render lines(d.wins)}</Kv>{/if}
      {#if d.tftp || (d.tftp_ips && d.tftp_ips.length) || d.bootfile}
        <Kv k="TFTP" wide>{#each [d.tftp, ...(d.tftp_ips || [])].filter(Boolean) as t, i}{#if i}<br>{/if}{t}{/each}{#if d.bootfile}<Ln p={'file ' + d.bootfile}/>{/if}</Kv>
      {/if}
      {#if d.wpad}<Kv k="Proxy (WPAD)" wide>{d.wpad}</Kv>{/if}
      {#if d.vendor}<Kv k="Vendor opt 43" wide><span class="c-mut">{d.vendor}{d.vendor_len ? '… (' + d.vendor_len + ' B)' : ''}</span></Kv>{/if}
      {#if d.raw}
        <div class="kv wide">
          <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
          <div class="k optk" onclick={() => dopt.open = !dopt.open}><Icon name={dopt.open ? 'chev-d' : 'chev-r'} size="s"/>DHCP options · {(d.options || []).length}{d.raw_truncated ? ' (first ' + Math.floor(d.raw.length / 2) + ' bytes)' : ''}</div>
          {#if dopt.open}<div class="opts">{#each opts as o}<div class="oc">{o.code} {o.name}</div><div>{o.value}</div>{/each}</div>{/if}
        </div>
      {:else if d.options}
        <Kv k="Options" wide><span class="c-mut">{d.options.join(' ')}</span></Kv>
      {/if}
    {/if}
    {#if open && pr.gateway}
      {@const g = pr.gateway}{@const i = pr.internet}{@const n = pr.dns}
      <div class="grp"><Icon name="reach" size="s"/>Reachability</div>
      <Kv k="Gateway ping"><Parts p={g.reachable ? ok(g.rtt_ms + ' ms (' + g.replies + '/' + g.sent + ')') : 'no reply' + (g.arp ? '' : ' · no ARP')}/></Kv>
      <Kv k="Internet"><Parts p={i.reachable ? ok(i.rtt_ms + ' ms to ' + i.target) : 'unreachable · ' + i.target}/></Kv>
      <!-- fixed two-line shape: the address and time change every run and must not change the row's height -->
      <Kv k="DNS lookup" wide>
        {#if n.ok}<Parts p={yn(true, n.name)}/><Ln p={[nw(n.addr), nw(n.ms + ' ms')]}/>{:else}lookup failed · {n.name}{/if}
        {#if n.custom}<Ln p={'via ' + n.server + ' (setting)'}/>{/if}
      </Kv>
    {/if}
  </div>
  {#if hasDet}<More key="port" {hint}/>{/if}
</div>
