<script>
  import ProtoCard from './ProtoCard.svelte';
  import Kv from '../lib/Kv.svelte';
  import Ln from '../lib/Ln.svelte';
  import { sg } from '../lib/parts.js';
  import { fmtAgo, waiting } from '../lib/format.js';
  let { l, up } = $props();

  let w = $derived(waiting(up, 32));
  let a = $derived(l && l.actor);
  let fwd = $derived(a && a.collecting && a.distributing);
  let every = $derived(!l ? '' : l.interval_ms ? 'every ' + (l.interval_ms < 3000 ? '1 s' : Math.round(l.interval_ms / 1000) + ' s')
    : l.partner && l.partner.fast ? 'every 1 s' : 'every 30 s');
</script>

{#if !l || !l.seen}
  <ProtoCard key="lacp" icon="link" title="Port channel" state={w ? 'listen' : 'none'} summary={w || 'not in one'}/>
{:else}
  <ProtoCard key="lacp" icon="link" title="Port channel" state={fwd ? 'ok' : 'heard'} summary={fwd ? 'forwarding' : 'not forwarding'}>
    <Kv k="LACP" wide copy={String(a.key)}>
      <span class="say">{fwd ? 'LACP · forwarding' : 'LACP · not forwarding'}</span>
      <Ln p={[a.active ? 'active' : 'passive', every, sg(a.key, 'key'), sg(a.port, 'port'), sg(a.system, 'switch'), l.changes ? 'changed ' + l.changes + '×' : '']}/>
      <Ln p={'last ' + fmtAgo(l.last_s)}/>
    </Kv>
  </ProtoCard>
{/if}
