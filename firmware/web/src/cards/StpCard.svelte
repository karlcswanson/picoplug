<script>
  import ProtoCard from './ProtoCard.svelte';
  import Kv from '../lib/Kv.svelte';
  import Parts from '../lib/Parts.svelte';
  import Ln from '../lib/Ln.svelte';
  import { sg } from '../lib/parts.js';
  import { fmtAgo, fmtDur, waiting } from '../lib/format.js';
  let { st, up } = $props();

  let w = $derived(waiting(up, 6));
  let isroot = $derived(st && st.root && st.root.is_neighbor);
  let kind = $derived(st ? (st.proto || '').toUpperCase()
    + (st.pvst ? ' (PVST+' + (st.vlan ? ' VLAN ' + st.vlan : '') + ')' : st.vlan ? ' VLAN ' + st.vlan : '')
    + (st.role ? ' · ' + st.role : '') + (st.state && st.state !== 'unknown' ? ' · ' + st.state : '')
    + (st.tc ? ' · topology change' : '') : '');
  let sum = $derived(st ? [(st.proto || '').toUpperCase(), st.role, st.state && st.state !== 'unknown' ? st.state : ''].filter(Boolean).join(' · ')
    + (st.tc ? ' · topology change' : st.root_changes ? ' · root changed' : '') : '');
</script>

{#if !st || !st.seen}
  <ProtoCard key="stp" icon="tree" title="Spanning tree" state={w ? 'listen' : 'none'} summary={w || 'no BPDUs in ' + fmtDur(up)}/>
{:else}
  <ProtoCard key="stp" icon="tree" title="Spanning tree" state="ok" summary={sum}>
    <Kv k="Spanning tree" wide>{kind}</Kv>
    {#if st.root}
      <Kv k="Root bridge" wide>
        {#if isroot}<Parts sep="" p={['this switch (', sg(st.root.priority), ' / ', sg(st.root.mac), ')']}/>
        {:else}<Parts p={[[sg(st.root.priority), ' / ', sg(st.root.mac)], sg(st.root.cost, 'cost')]}/>{/if}
      </Kv>
    {/if}
    {#if st.bridge && !isroot}
      <Kv k="Bridge" wide><Parts sep="" p={[sg(st.bridge.priority), ' / ', sg(st.bridge.mac), st.port_number ? [' ', sg(st.port_number, 'port')] : '']}/></Kv>
    {/if}
    {#if st.mst}
      <Kv k="MST region" wide>
        <Parts p={[[sg(st.mst.region || '(none)'), ' ', sg(st.mst.revision, 'rev')], st.mst.instances + ' instance' + (st.mst.instances === 1 ? '' : 's'), st.mst.hops + ' hops left']}/>
        {#if st.mst.regional_root && !st.mst.regional_root.is_neighbor}<Ln p={[sg(st.mst.regional_root.mac, 'regional root'), sg(st.mst.internal_cost, 'internal cost')]}/>{/if}
      </Kv>
    {/if}
    {#if st.hello_s !== undefined}
      <Kv k="Timers" wide><Parts p={[[sg(st.hello_s, 'hello'), ' s'], [sg(st.max_age_s, 'max age'), ' s'], [sg(st.fwd_delay_s, 'fwd delay'), ' s'], st.msg_age_s ? [sg(st.msg_age_s, 'age'), ' s'] : '']}/></Kv>
    {/if}
    <Kv k="BPDUs heard" wide>
      <Parts sep="" p={[sg(st.count), st.interval_ms ? [' · every ', sg(Math.round(st.interval_ms / 100) / 10), ' s'] : '']}/>
      <Ln p={'last ' + fmtAgo(st.last_s)}/>
      <Ln p={[st.tc_count ? st.tc_count + ' TC' : '', st.root_changes ? 'root changed ' + st.root_changes + '×' : '']}/>
      <Ln p="listen only"/>
    </Kv>
  </ProtoCard>
{/if}
