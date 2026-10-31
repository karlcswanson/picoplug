<script>
  import ProtoCard from './ProtoCard.svelte';
  import Kv from '../lib/Kv.svelte';
  import Parts from '../lib/Parts.svelte';
  import Ln from '../lib/Ln.svelte';
  import { sg, mut } from '../lib/parts.js';
  import { fmtAgo, fmtDur, waiting } from '../lib/format.js';
  let { s, cur, up } = $props();

  let ig = $derived(s.igmp || {});
  let pt = $derived(s.ptp || {});
  let title = $derived(ig.mrouter ? 'IGMP and PIM' : 'IGMP');
  let w = $derived(waiting(up, 130));
  let inUse = $derived(pt.seen || ig.reports || ig.mrouter);
  let noip = $derived(ig.querier === '0.0.0.0');
  let qwho = $derived(ig.seen ? whoIs(noip ? '' : ig.querier, ig.querier_mac) : '');
  let rwho = $derived(ig.mrouter ? whoIs(ig.mrouter.ip, ig.mrouter.mac) : '');
  let reports = $derived(ig.reports ? ig.reports + ' report' + (ig.reports > 1 ? 's' : '') + ' from others' : '');
  let waitSum = $derived(w ? w + ' of 2 min' : 'no querier in ' + fmtDur(up));

  // who sent it: match an address against what the port already told us
  function whoIs(ip, mac) {
    const d = (s.port && s.port.dhcp) || {}, c = cur || {};
    const cmac = (c.chassis_id || '').replace(/^mac:/, '');
    if (ip && (c.mgmt_addr || []).includes(ip) || mac && cmac && cmac === mac) return 'this switch' + (c.sys_name ? ' (' + c.sys_name + ')' : '');
    if (ip && ip === d.gateway) return 'the gateway';
    if (mac && mac === d.server_mac) return 'the DHCP server';
    if (ig.mrouter && ip === ig.mrouter.ip) return 'the multicast router';
    return '';
  }
</script>

{#snippet router()}
  {#if ig.mrouter}
    {@const r = ig.mrouter}
    <Kv k="Multicast router" wide copy={r.ip}>
      <Parts sep="" p={[sg(r.ip), rwho && rwho !== 'the multicast router' ? [' ', mut('· ' + rwho)] : '']}/>
      <Ln p={['PIM hello', sg(r.mac), 'DR priority ' + r.dr_priority, 'hold ' + r.holdtime_s + ' s']}/>
      <Ln p={'last ' + fmtAgo(r.last_s)}/>
    </Kv>
  {/if}
{/snippet}

{#if ig.seen}
  <ProtoCard key="mcast" icon="wave" {title} state="ok" summary={(noip ? 'querier, no IP' : 'querier ' + ig.querier) + ' · v' + ig.version}>
    <Kv k="IGMP querier" wide copy={noip ? ig.querier_mac : ig.querier}>
      <Parts sep="" p={[noip ? ['no IP address ', sg(ig.querier_mac)] : sg(ig.querier), qwho ? [' ', mut('· ' + qwho)] : '']}/>
      <Ln p={['IGMPv' + ig.version, (ig.interval_s || ig.qqi_s) ? 'every ' + (ig.interval_s || ig.qqi_s) + ' s' : '',
        ig.max_resp_ds ? 'answer within ' + (ig.max_resp_ds / 10) + ' s' : '', ig.qrv ? 'robustness ' + ig.qrv : '', ig.s_flag ? 'S flag' : '']}/>
      <Ln p={'last ' + fmtAgo(ig.last_s)}/>
      <Ln p={[!noip && !qwho ? sg(ig.querier_mac) : '',
        ig.querier_changes ? ['querier changed ' + ig.querier_changes + '×', ig.prev_querier ? [', was ', sg(ig.prev_querier)] : ''] : '']}/>
      <Ln p={reports}/>
    </Kv>
    {@render router()}
  </ProtoCard>
{:else if !inUse}
  <ProtoCard key="mcast" icon="wave" {title} state={w ? 'listen' : 'none'} summary={waitSum}/>
{:else}
  <ProtoCard key="mcast" icon="wave" {title} state={w ? 'listen' : 'heard'} summary={waitSum}>
    <Kv k="IGMP querier" wide><span class="say">none seen{up ? ' in ' + fmtDur(up) : ''}</span><Ln p={reports}/></Kv>
    {@render router()}
  </ProtoCard>
{/if}
