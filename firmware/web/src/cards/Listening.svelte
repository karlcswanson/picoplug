<script>
  import Mark from '../lib/Mark.svelte';
  import { fmtDur } from '../lib/format.js';
  let { up } = $props();
</script>

<div class="card nb listen">
  <Mark pulse/>
  <div class="t">Listening</div>
  <div class="ln">LLDP · CDP{up >= 30 ? ' · nothing in ' + fmtDur(up) : ''}</div>
  <div class="bar"><i style:width="{Math.min(100, Math.round(up / 30 * 100))}%"></i></div>
  <div class="meta" style="text-align:center">{up} s</div>
</div>
