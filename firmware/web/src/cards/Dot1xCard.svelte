<script>
  import ProtoCard from './ProtoCard.svelte';
  import Kv from '../lib/Kv.svelte';
  import Ln from '../lib/Ln.svelte';
  import { sg } from '../lib/parts.js';
  import { fmtAgo, waiting } from '../lib/format.js';
  let { x, up } = $props();
  let w = $derived(waiting(up, 35));
  let detail = $derived(!x ? '' : (x.eap ? ' · EAP ' + x.eap : '') + (x.failures ? ' · ' + x.failures + ' failure' + (x.failures > 1 ? 's' : '') : ''));
  let requests = $derived(!x ? [] : [x.requests + ' request' + (x.requests === 1 ? '' : 's'),
    x.interval_ms ? 'every ' + Math.round(x.interval_ms / 1000) + ' s' : '']);
</script>

{#if !x || !x.seen}
  <ProtoCard key="dot1x" icon="lock" title="802.1X" state={w ? 'listen' : 'none'} summary={w || 'not asked'}/>
{:else}
  <ProtoCard key="dot1x" icon="lock" title="802.1X" state="heard" summary="auth requested">
    <Kv k="Authenticator" wide copy={x.authenticator || ''}>
      <span class="say">auth requested</span>{detail}
      <Ln p={requests}/>
      <Ln p={'last ' + fmtAgo(x.last_s)}/>
      <Ln p={x.authenticator ? sg(x.authenticator, 'from') : ''}/>
    </Kv>
  </ProtoCard>
{/if}
