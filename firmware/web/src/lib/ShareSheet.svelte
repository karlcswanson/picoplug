<script>
  // The Share button's sheet: an optional note, then open the share link, copy it, copy the card
  // as text, or the phone's own share menu where the browser offers one (only on https, so not on
  // the tester's own page). link(note) makes the link; the note rides along in it and in the text.
  import Icon from './Icon.svelte';
  import { copyText } from './copy.svelte.js';
  import { NOTE_MAX } from './share.js';
  let { link, text, title, onclose } = $props();
  let dlg, note = $state('');
  let href = $derived(link(note));
  let body = $derived(text + (note.trim() ? '\nNote: ' + note.trim() : ''));
  let host = $derived((() => { try { return new URL(href).host; } catch (e) { return ''; } })());
  const native = typeof navigator !== 'undefined' && !!navigator.share && window.isSecureContext;

  $effect(() => { dlg.showModal(); return () => { if (dlg.open) dlg.close(); }; });
  function done(fn) { return () => { fn(); dlg.close(); }; }
  function shareNative() { navigator.share({ title, text: body, url: href || undefined }).catch(() => {}); }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_noninteractive_element_interactions -->
<dialog class="sheet" bind:this={dlg} onclose={onclose} onclick={e => { if (e.target === dlg) dlg.close(); }} aria-label="Share">
  <div class="shead">
    <span class="sec"><Icon name="share" size="s"/>Share</span>
    <button class="q x" aria-label="Close" onclick={() => dlg.close()}><Icon name="x"/></button>
  </div>
  <input class="in snote" placeholder="Note (optional)" maxlength={NOTE_MAX} autocomplete="off" bind:value={note} aria-label="Note">
  <div class="slist">
    {#if href}
      <a class="btn p" {href} target="_blank" rel="noopener" onclick={() => dlg.close()}><Icon name="share"/>Open on {host || 'the web'}</a>
      <button class="q" onclick={done(() => copyText(href))}><Icon name="link"/>Copy link</button>
    {/if}
    <button class="q" onclick={done(() => copyText(body))}><Icon name="copy"/>Copy as text</button>
    {#if native}<button class="q" onclick={done(shareNative)}><Icon name="share"/>More…</button>{/if}
  </div>
</dialog>
