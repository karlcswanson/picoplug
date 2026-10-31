<script>
  import Parts from './Parts.svelte';
  import { copyable } from './copy.svelte.js';
  import { isEmpty } from './parts.js';
  let { p, sep = ' · ' } = $props();
  let list = $derived((Array.isArray(p) ? p : [p]).filter(x => !isEmpty(x)));
</script>

{#each list as x, i}{#if i && sep}{sep}{/if}{#if typeof x === 'string' || typeof x === 'number'}{x}{:else if Array.isArray(x)}<Parts p={x} sep=""/>{:else if x.sg !== undefined}<span class="sg" use:copyable={x.sg}>{x.label ? x.label + ' ' : ''}{x.sg}</span>{:else}<span class={x.cls}>{x.t}</span>{/if}{/each}
