<script>
  import Parts from './Parts.svelte';
  import { copyable } from './copy.svelte.js';
  import { isEmpty } from './parts.js';
  // Renders a "parts" value from parts.js: strings and numbers as text, arrays inline with no
  // separator, sg() as a value that copies alone when tapped, anything else as a styled span.
  let { p, sep = ' · ' } = $props();
  let list = $derived((Array.isArray(p) ? p : [p]).filter(x => !isEmpty(x)));
</script>

<!-- no whitespace between the blocks: it would show up between the parts -->
{#each list as x, i}{#if i && sep}{sep}{/if}{#if typeof x === 'string' || typeof x === 'number'}
    {x}
  {:else if Array.isArray(x)}
    <Parts p={x} sep=""/>
  {:else if x.sg !== undefined}
    <span class="sg" use:copyable={x.sg}>{x.label ? x.label + ' ' : ''}{x.sg}</span>
  {:else}
    <span class={x.cls}>{x.t}</span>
  {/if}{/each}
