<script>
  import { isP, RES } from './format.js';
  let { n } = $props();
</script>

{#if isP(n)}<span class="pb pb{n}" title="Probe {n}, {RES[n]}">{n}</span>{/if}
