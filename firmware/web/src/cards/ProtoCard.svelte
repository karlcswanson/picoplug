<script>
  // One card per protocol heard on the wire. With data it opens to its detail (the person can
  // fold it, remembered); with nothing heard it is one muted line that says how long the
  // tester has listened. state: listen | none | heard | ok. ok (heard normally) gets the
  // green tick; heard is a plain state with no mark. Nothing is ever flagged as a problem.
  import Icon from '../lib/Icon.svelte';
  import { ui, toggle } from '../lib/ui.svelte.js';
  let { key, icon, title, state, summary, children } = $props();
  let has = $derived(!!children);
  let open = $derived(has && !ui['c_' + key]);
  let muted = $derived(state === 'none' || state === 'listen');
</script>

{#snippet head()}
  <span class="sec"><Icon name={icon} size="s"/>{title}</span>
  <span class="psum" class:c-mut={muted} title={state === 'none' ? summary : undefined}>
    {#if state === 'none'}<Icon name="fail" size="s c-mut"/><span class="sr">{summary}</span>
    {:else if state === 'ok'}<Icon name="ok" size="s c-ok"/><span>{summary}</span>
    {:else}<span>{summary}</span>{/if}
  </span>
  {#if has}<Icon name={open ? 'chev-u' : 'chev-d'} size="s c-mut"/>{/if}
{/snippet}

<div class="card pc" class:empty={!has} id="pc-{key}">
  {#if has}
    <button class="phead" aria-expanded={open} onclick={() => toggle('c_' + key)}>{@render head()}</button>
  {:else}
    <div class="phead">{@render head()}</div>
  {/if}
  {#if open}<div class="grid">{@render children()}</div>{/if}
</div>
