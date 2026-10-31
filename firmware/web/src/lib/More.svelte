<script>
  // The Details / Less button under a card; the open state is remembered.
  import Icon from './Icon.svelte';
  import { ui, toggle } from './ui.svelte.js';
  let { key, hint } = $props();
  let open = $derived(!!ui[key]);
</script>

<button class="disc" aria-expanded={open} onclick={() => toggle(key)}>
  <Icon name={open ? 'chev-u' : 'chev-d'}/><span>{open ? 'Less' : 'Details'}</span>{#if !open}<span class="hint">{hint}</span>{/if}
</button>
