<script>
  // Ports visited, kept in this browser. Tap a row for the full switch card and a note field.
  import Icon from '../lib/Icon.svelte';
  import Kv from '../lib/Kv.svelte';
  import NeighborCard from '../cards/NeighborCard.svelte';
  import { hist, setNote, hDel, hClear, histCsv, fmtTime } from '../lib/history.svelte.js';
  import { app } from '../lib/app.svelte.js';
  import { copyable, copyText, say } from '../lib/copy.svelte.js';
  import { stripKind, portFit } from '../lib/format.js';

  let fb = $state();
  // focus the copy box once per failed copy, not on every visit to Log
  $effect(() => { if (app.fbFocus && fb) { fb.focus(); fb.select(); app.fbFocus = false; } });

  const when = e => `first ${fmtTime(e.first)}${e.last - e.first > 60000 ? ' · last ' + fmtTime(e.last) : ''}${e.sessions > 1 ? ' · ' + e.sessions + ' visits' : ''}${e.dhcp ? ' · DHCP ' + e.dhcp : ''}`;

  function rowClick(e, k) {
    if (e.target.closest('input,.hdet,.copy,.sg,button')) return;
    hist.open = hist.open === k ? null : k;
  }
  function clear() { if (confirm('Clear all ports?')) hClear(); }
  function csv() {
    const c = histCsv();
    try {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(new Blob([c], { type: 'text/csv' }));
      a.download = 'picoplug-ports-' + new Date().toISOString().slice(0, 10) + '.csv';
      document.body.appendChild(a); a.click();
      setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
      say('CSV saved');
    } catch (err) { copyText(c); }
  }
</script>

<div class="card" id="hcard">
  <div class="chead">
    <span class="sec"><Icon name="log" size="s"/>Ports visited <span class="meta">{hist.list.length || ''}</span></span>
    <span class="crow"><button class="q" onclick={csv}><Icon name="download"/>CSV</button><button class="q" onclick={clear}><Icon name="trash"/>Clear</button></span>
  </div>
  <div class="hist">
    {#each hist.list as e}
      {@const open = e.key === hist.open}
      <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
      <div class="hrow" class:open onclick={ev => rowClick(ev, e.key)}>
        <div>
          <span class="p">{e.port}</span> <span>on {e.sys_name || e.chassis}{e.pvid !== undefined ? ' · VLAN ' + e.pvid : ''}{e.voice ? ' · voice ' + e.voice : ''}</span>
          <div class="t">{fmtTime(e.first)}{e.last - e.first > 60000 ? ' to ' + fmtTime(e.last) : ''}{e.sessions > 1 ? ' · ' + e.sessions + ' visits' : ''}</div>
          {#if open}<input placeholder="note (room, jack, ticket…)" aria-label="Note" value={e.note || ''} onchange={ev => setNote(e.key, ev.target.value)}>
          {:else if e.note}<div class="t">{e.note}</div>{/if}
        </div>
        <button class="x" aria-label="Remove {e.port}" onclick={() => hDel(e.key)}><Icon name="x"/></button>
        {#if open}
          <div class="hdet">
            {#if e.raw}<NeighborCard n={e.raw} detail foot={when(e)}/>
            {:else}
              <div class="card nb">
                <div class="nrow"><Icon name="switch" size="l"/><div class="name">{e.sys_name || e.chassis}</div><span class="ptag">{e.proto.toUpperCase()}</span></div>
                <div class="port copy" style:font-size={portFit(e.port)} use:copyable={e.port}>{e.port}</div>
                <div class="desc">{e.port_desc}</div>
                <div class="grid">
                  {#if e.pvid !== undefined && e.pvid !== null}<Kv k="PVID">{e.pvid}</Kv>{/if}
                  {#if e.voice !== undefined && e.voice !== null && e.voice !== ''}<Kv k="Voice VLAN">{e.voice}</Kv>{/if}
                  {#if e.mgmt}<Kv k="Mgmt IP" copy={e.mgmt.split(' ')[0]}>{e.mgmt}</Kv>{/if}
                  {#if e.platform}<Kv k="Platform">{e.platform}</Kv>{/if}
                  {#if e.poe !== undefined && e.poe !== null}<Kv k="PoE">{e.poe} W alloc</Kv>{/if}
                  {#if e.vlans}<Kv k="VLANs" wide>{e.vlans}</Kv>{/if}
                  <Kv k="Chassis" wide copy={stripKind(e.chassis)}>{e.chassis}</Kv>
                </div>
                <div class="foot"><span>{when(e)}</span></div>
              </div>
            {/if}
          </div>
        {/if}
      </div>
    {:else}
      <div class="empty">No ports yet</div>
    {/each}
  </div>
  {#if app.fallback}
    <div><div class="fl">Select all and copy:</div><textarea class="fb" readonly bind:this={fb} value={app.fallback}></textarea></div>
  {/if}
</div>
