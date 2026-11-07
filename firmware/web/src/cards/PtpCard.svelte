<script>
  import ProtoCard from './ProtoCard.svelte';
  import Kv from '../lib/Kv.svelte';
  import Parts from '../lib/Parts.svelte';
  import Ln from '../lib/Ln.svelte';
  import { sg, mut } from '../lib/parts.js';
  import { fmtDur, waiting } from '../lib/format.js';
  let { pt, up } = $props();

  let w = $derived(waiting(up, 5));
  let rows = $derived(!pt || !pt.seen ? [] : ['v1', 'v2'].filter(v => pt[v]).map(v => {
    const x = pt[v], gm = x.grandmaster, v1 = v === 'v1';
    // what is on the wire only: version, domain or subdomain, transport
    const what = v1 ? ['PTPv1', x.subdomain ? [' · subdomain ', mut(x.subdomain)] : '']
      : 'PTPv2 · domain ' + x.domain + ' · ' + (x.transport === 'l2' ? 'layer 2' : 'UDP');
    const l2 = [x.master ? sg(x.master, 'master') : '', x.master_ip ? sg(x.master_ip) : ''];
    // measured from message gaps, so it varies: own line
    const sync = x.sync_interval_ms < 1000 ? x.sync_interval_ms + ' ms' : (x.sync_interval_ms / 1000).toFixed(1) + ' s';
    const ls = [x.sync_interval_ms ? 'sync every ' + sync : '',
      x.two_step ? 'two-step' : ''];
    const l3 = [];
    if (gm) {
      l3.push(sg(gm.id, 'grandmaster'), 'class ' + gm.clock_class);
      if (gm.time_source && gm.time_source !== 'unknown') l3.push(gm.time_source);
      l3.push('priority ' + gm.priority1 + '/' + gm.priority2);
      if (gm.steps_removed) l3.push(gm.steps_removed + ' hop' + (gm.steps_removed > 1 ? 's' : '') + ' away');
      if (gm.changes) l3.push('changed ' + gm.changes + '×');
    }
    if (x.other_domains) l3.push(x.other_domains + ' in other domains');
    const sum = (v1 ? 'v1' + (x.subdomain ? ' ' + x.subdomain : '') : 'v2 domain ' + x.domain)
      + (x.master_ip ? ' · ' + x.master_ip : '');
    return { k: v1 ? 'PTP v1' : 'PTP v2', copy: x.master_ip || x.master || '', what, l2, ls, l3, sum };
  }));
</script>

{#if !pt || !pt.seen}
  <ProtoCard key="ptp" icon="clock" title="PTP" state={w ? 'listen' : 'none'} summary={w || 'no clock in ' + fmtDur(up)}/>
{:else if !rows.length}
  <ProtoCard key="ptp" icon="clock" title="PTP" state="ok" summary=""/>
{:else}
  <ProtoCard key="ptp" icon="clock" title="PTP" state="ok" summary={rows.map(r => r.sum).join(' + ')}>
    {#each rows as r}
      <Kv k={r.k} wide copy={r.copy}><Parts sep="" p={r.what}/><Ln p={r.l2}/><Ln p={r.ls}/><Ln p={r.l3}/></Kv>
    {/each}
  </ProtoCard>
{/if}
