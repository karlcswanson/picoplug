<script>
  // The switch card: who is on the other end of the cable, port, VLANs, details on demand.
  import Icon from '../lib/Icon.svelte';
  import Kv from '../lib/Kv.svelte';
  import { copyable, copyText, say } from '../lib/copy.svelte.js';
  import Parts from '../lib/Parts.svelte';
  import { sg } from '../lib/parts.js';
  import { ui, sel } from '../lib/ui.svelte.js';
  import More from '../lib/More.svelte';
  import { app, formFill, formVals } from '../lib/app.svelte.js';
  import { conn } from '../lib/status.svelte.js';
  import { stripKind, portName, isHost, isStale, v4first, hKey, fmtAgo, fmtDur, portFit, summary } from '../lib/format.js';

  // detail: the copy shown in Ports visited (everything open, no controls); foot replaces the TTL line
  let { n, neighbors = [], detail = false, foot } = $props();

  const MAU = { 16: '100M FD', 15: '100M HD', 30: '1G FD', 29: '1G HD', 10: '10M FD' };
  const join = a => a.filter(Boolean).join(' · ');

  let open = $derived(detail || !!ui.nb);
  let stale = $derived(!detail && isStale(n));
  let form = $derived(!detail && !n.picoplug && app.form && !app.readonly ? formFill(app.form, formVals(conn.status, n)) : '');
  let nm = $derived(n.sys_name || stripKind(n.chassis_id));
  let port = $derived(portName(n));
  let mg = $derived(v4first(n.mgmt_addr));
  let bt = $derived((n.poe && n.poe.bt) || {});
  let poe = $derived(n.poe ? join([
    bt.class !== undefined ? 'Class ' + bt.class : n.poe.class !== undefined ? 'Class ' + n.poe.class : '',
    n.poe.alloc_w !== undefined ? n.poe.alloc_w + ' W alloc' : '',
    n.poe.req_w !== undefined ? n.poe.req_w + ' W req' : '',
    /^4-pair/.test(bt.powering || '') || (n.upoe && n.upoe.spare_on) ? '4-pair' : '',
    n.poe.enabled === false ? 'disabled' : '',
  ]) || (n.poe.port_class === 'pse' ? 'PSE' : 'PD') : '');
  let cpow = $derived(n.cdp_power ? join([
    n.cdp_power.available_w !== undefined ? n.cdp_power.available_w + ' W available' : '',
    n.cdp_power.requested_w !== undefined ? n.cdp_power.requested_w + ' W requested' : '',
    n.cdp_power.consumption_w !== undefined ? n.cdp_power.consumption_w + ' W' : '',
  ]) : '');
  let voice = $derived(n.voice_vlan ? `${n.voice_vlan.id}${n.voice_vlan.tagged ? ' tagged' : ''}${n.voice_vlan.dscp !== undefined ? ' · DSCP ' + n.voice_vlan.dscp : ''}` : '');
  let speed = $derived(n.mac_phy && n.mac_phy.mau_type ? MAU[n.mac_phy.mau_type] || 'MAU ' + n.mac_phy.mau_type : '');
  let inv = $derived(n.inventory || {});
  const has = v => v !== undefined && v !== null && v !== '';
  // the heading shows only when at least one row under it does
  let hasDev = $derived(!!(speed || inv.serial || poe || cpow || n.platform || n.vtp_domain || n.duplex || n.mtu || inv.manufacturer || inv.model
    || inv.hw || inv.fw || inv.sw || inv.asset || has(n.mgmt_vid) || (n.ppvids && n.ppvids.length) || n.protocols || n.eee || n.location));
  let extra = $derived([n.vlans ? 'VLANs' : '', poe || cpow ? 'PoE' : '', n.platform ? 'platform' : '',
    n.caps ? 'caps' : '', n.link_agg ? 'LAG' : '', 'chassis'].filter(Boolean).join(', '));
  let fit = $derived(portFit(port));

  function relisten() {
    fetch('/tool?relisten=1').then(r => r.json()).then(j => say(j.ok ? 'Listening again' : (j.error || 'Rejected'))).catch(() => say('No reply'));
  }
</script>

{#snippet nws(s)}{#each s.split(' · ') as p, i}{#if i}{' · '}{/if}<span class="nw">{p}</span>{/each}{/snippet}
{#snippet mut()}<span class="c-mut">--</span>{/snippet}

<div class="card nb" class:stale>
  {#if !detail && neighbors.length > 1}
    <div class="seg" class:two={neighbors.length > 2}>
      {#each neighbors as x}
        <button class:on={x === n} onclick={() => sel.key = hKey(x)}><span class="el">{(x.proto || 'lldp').toUpperCase()} {portName(x)}</span></button>
      {/each}
    </div>
  {/if}
  <div class="nrow">
    <Icon name={n.picoplug ? 'probes' : isHost(n) ? 'laptop' : 'switch'} size="l"/>
    <div class="name copy" title="tap to copy" use:copyable={nm}>{nm}</div>
    <span class="ntag"><span class="ptag">{(n.proto || 'lldp').toUpperCase()}{n.cdp_version ? ' v' + n.cdp_version : ''}</span>{#if !detail}<span class="meta" class:warn={stale}>{fmtAgo(n.last_seen_s)}</span>{/if}</span>
  </div>
  <div class="port copy" title="tap to copy" style:font-size={fit} use:copyable={port}>{port}</div>
  {#if port !== stripKind(n.port_id)}<div class="desc">{stripKind(n.port_id)}</div>
  {:else if n.port_desc}<div class="desc">{n.port_desc}</div>{/if}

  <div class="grid">
    {#if n.picoplug}<div class="note"><div>Another picoplug</div></div>{/if}
    {#if n.pvid !== null}<Kv k="PVID">{#if n.pvid !== undefined}{n.pvid}{:else}{@render mut()}{/if}</Kv>{/if}
    <Kv k="Voice VLAN">{#if voice}{voice}{:else}{@render mut()}{/if}</Kv>
    <Kv k="Mgmt IP" wide copy={mg[0]}>
      {#each mg as a, i}{#if i}<br>{/if}{#if a.includes(':')}<span class="brk">{a}</span>{:else}<span class="nw">{a}</span>{/if}{:else}{@render mut()}{/each}
    </Kv>
    {#if stale}
      <div class="note"><div>Last heard {fmtDur(n.last_seen_s)} ago · expires in {n.ttl_remaining_s} s</div></div>
      <div style="grid-column:1/-1"><button class="q" onclick={relisten}><Icon name="redo"/>Relisten</button></div>
    {/if}

    {#if open}
      {#if n.vlans}
        <div class="grp"><Icon name="tag" size="s"/>VLANs</div>
        <div class="kv wide"><div class="tags">{#each n.vlans as v}<span class="tag"><b>{v.id}</b> {v.name}</span>{/each}</div></div>
      {/if}
      {#if hasDev}<div class="grp"><Icon name="port" size="s"/>Port and device</div>{/if}
      {#if speed}<Kv k="Link">{speed}</Kv>{/if}
      {#if inv.serial}<Kv k="Serial" copy={inv.serial}>{inv.serial}</Kv>{/if}
      {#if poe || cpow}<Kv k="PoE">{@render nws(poe || cpow)}</Kv>{/if}
      {#if n.platform}<Kv k="Platform">{n.platform}</Kv>{/if}
      {#if n.vtp_domain}<Kv k="VTP domain">{n.vtp_domain}</Kv>{/if}
      {#if n.duplex || n.mtu}<Kv k="Duplex / MTU"><Parts p={[sg(n.duplex), sg(n.mtu, 'MTU')]}/></Kv>{/if}
      {#if inv.manufacturer || inv.model}<Kv k="Model"><Parts sep=" " p={[sg(inv.manufacturer), sg(inv.model)]}/></Kv>{/if}
      {#if inv.hw || inv.fw || inv.sw}<Kv k="Revisions"><Parts p={[sg(inv.hw, 'hw'), sg(inv.fw, 'fw'), sg(inv.sw, 'sw')]}/></Kv>{/if}
      {#if inv.asset}<Kv k="Asset ID">{inv.asset}</Kv>{/if}
      {#if has(n.mgmt_vid)}<Kv k="Mgmt VLAN">{n.mgmt_vid}</Kv>{/if}
      {#if n.ppvids && n.ppvids.length}<Kv k="Protocol VLANs">{n.ppvids.map(p => p.id).join(' ')}</Kv>{/if}
      {#if n.protocols}<Kv k="Protocols">{n.protocols}</Kv>{/if}
      {#if n.eee}<Kv k="EEE"><Parts p={[sg(n.eee.tx_us + ' µs', 'tx'), sg(n.eee.rx_us + ' µs', 'rx')]}/></Kv>{/if}
      {#if n.location}<Kv k="Location" wide copy={n.location.text}>{n.location.text}</Kv>{/if}

      <div class="grp"><Icon name="switch" size="s"/>Chassis</div>
      {#if n.caps && n.caps.length}<Kv k="Caps" wide>{n.caps.join(', ')}</Kv>{/if}
      {#if n.link_agg}<Kv k="LAG" wide>{n.link_agg.active ? 'member of aggregate ' + n.link_agg.port_id : n.link_agg.capable ? 'capable, not aggregated' : 'not capable'}</Kv>{/if}
      <Kv k="Chassis ID" wide copy={stripKind(n.chassis_id)}>{n.chassis_id}</Kv>
      {#if n.mud_url}<Kv k="MUD URL" wide copy={n.mud_url}><span class="brk">{n.mud_url}</span></Kv>{/if}
      {#if n.sys_desc}<div class="kv wide"><div class="k">System</div><div class="sys">{n.sys_desc}</div></div>{/if}
    {/if}
  </div>

  {#if !detail}<More key="nb" hint={extra}/>{/if}
  <div class="foot">
    <span>{foot !== undefined ? foot : 'TTL ' + n.ttl_remaining_s + ' s'}{n.unknown_tlvs ? ' · ' + n.unknown_tlvs + ' unknown TLV' : ''}</span>
    <span class="crow">{#if form}<a class="btn q" href={form} target="_blank" rel="noopener"><Icon name="form"/>Form</a>{/if}<button class="q" onclick={() => copyText(summary(n))}><Icon name="copy"/>Copy</button></span>
  </div>
</div>
