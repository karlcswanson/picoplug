<script>
  import Icon from '../lib/Icon.svelte';
  import { hist, fmtTime } from '../lib/history.svelte.js';
  let e = $derived(hist.list[0]);
</script>

{#if e}
  <div class="card">
    <div class="chead"><span class="sec"><Icon name="clock" size="s"/>Last port</span><span class="meta">{fmtTime(e.last)}</span></div>
    <div class="nrow">
      <span class="mono" style="font-weight:700;font-size:18px">{e.port}</span>
      <span style="flex:1;min-width:0;color:var(--mut)">on {e.sys_name || e.chassis}{e.pvid !== undefined ? ' · VLAN ' + e.pvid : ''}</span>
    </div>
    <div style="margin-top:10px"><a class="btn" href="#log"><Icon name="log"/>Ports visited</a></div>
  </div>
{/if}
