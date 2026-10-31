<script>
  // A value line of its own. Values that change between updates get a line of their own,
  // so a longer value never re-wraps the row and moves the page.
  import Parts from './Parts.svelte';
  import { isEmpty } from './parts.js';
  let { p, sep = ' · ' } = $props();
</script>

{#if !isEmpty(p)}<span class="ln"><Parts {p} {sep}/></span>{/if}
