<script>
  // One labelled value; tapping copies `copy`, or the value's text when `copy` is empty.
  import { copyText } from './copy.svelte.js';
  let { k, wide = false, copy, children } = $props();
  let v;
  function tap(e) {
    if (e.target.closest('button,a,input,label')) return;
    copyText(copy ? String(copy) : v.innerText.trim());
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
<div class="kv copy" class:wide title="tap to copy" onclick={tap}>
  <div class="k">{k}</div>
  <div class="v" bind:this={v}>{@render children()}</div>
</div>
