<script>
  // other picoplug testers on this network, found with mDNS (_picoplug._tcp); tap one to read it
  import ProtoCard from './ProtoCard.svelte';
  import Ln from '../lib/Ln.svelte';
  import Icon from '../lib/Icon.svelte';
  import ProbeBadge from '../lib/ProbeBadge.svelte';
  import Parts from '../lib/Parts.svelte';
  import { sg } from '../lib/parts.js';
  import { waiting, stripKind, isP, RES } from '../lib/format.js';
  import { viewRemote } from '../lib/status.svelte.js';
  let { ps, cur, up } = $props();

  let list = $derived((ps && ps.list) || []);
  let w = $derived(waiting(up, 5));
  let myCh = $derived(stripKind((cur && cur.chassis_id) || ''));

  function where(p) {
    if (p.chassis && myCh && stripKind(p.chassis) === myCh)
      return ['same switch' + (cur.sys_name ? ' (' + cur.sys_name + ')' : ''), p.port ? [', port ', sg(stripKind(p.port))] : ''];
    const sw = p.sys_name || (p.chassis && stripKind(p.chassis));
    if (sw) return ['on ', sg(sw), p.port ? [' port ', sg(stripKind(p.port))] : ''];
    return 'no switch heard';
  }
</script>

{#if !list.length}
  <ProtoCard key="peer" icon="probes" title="Other probes" state={w ? 'listen' : 'none'} summary={w || 'none found'}/>
{:else}
  <ProtoCard key="peer" icon="probes" title="Other probes" state="ok" summary={list.length + ' found'}>
    {#each list as p (p.name)}
      <button class="kv wide peer" onclick={e => { e.currentTarget.blur(); viewRemote(p.name); }}>
        <div class="k">{isP(p.probe) ? 'Probe ' + p.probe + ' · ' + RES[p.probe] : 'Probe'}</div>
        <div class="v"><ProbeBadge n={p.probe}/><Parts p={sg(p.name)}/>
          <Ln p={[where(p), p.pvid !== undefined ? 'PVID ' + p.pvid : '', p.ip4 ? sg(p.ip4) : '']}/></div>
        <Icon name="chev-r"/>
      </button>
    {/each}
  </ProtoCard>
{/if}
