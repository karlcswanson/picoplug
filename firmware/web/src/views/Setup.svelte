<script module>
  // kept across tab switches
  const f = $state({ probe: '', probeSaved: '', lldp: false, cdp: false, ping: '', name: '', dns: '', usb: '', defaults: {}, reboot: false, usbSaved: '' });
  const m = $state({ msg: '', fmsg: '' });
</script>

<script>
  // Probe number, what the tester advertises, theme, device facts, port-check settings, Form link.
  import Icon from '../lib/Icon.svelte';
  import Kv from '../lib/Kv.svelte';
  import { conn } from '../lib/status.svelte.js';
  import { app, formSet, formFill, formVals, current } from '../lib/app.svelte.js';
  import { say } from '../lib/copy.svelte.js';
  import { isP, RES, fmtDur } from '../lib/format.js';

  let s = $derived(conn.status);
  let tx = $derived((s && s.tx) || {});
  let formText = $state(app.form), formEl;
  let theme = $state((() => { try { return localStorage.getItem('picolldp.theme') || 'auto'; } catch (e) { return 'auto'; } })());
  let preview = $derived(formText.trim() ? formFill(formText.trim(), formVals(s, current())) : 'no button');

  function fill(j) {
    f.probe = f.probeSaved = isP(j.probe) ? String(j.probe) : ''; f.lldp = !!j.lldp; f.cdp = !!j.cdp;
    f.ping = j.ping_target || ''; f.name = j.dns_name || ''; f.dns = j.dns_server || ''; f.usb = j.usb_ip || '';
    f.defaults = j.defaults || {}; f.reboot = !!j.reboot_needed; f.usbSaved = j.usb_ip || '';
    formSet(j);
    if (document.activeElement !== formEl) formText = app.form;
  }
  fetch('/settings').then(r => r.json()).then(fill).catch(() => { m.msg = 'Could not read settings'; });

  function send(q, quiet) {
    if (!quiet) m.msg = 'Saving…';
    fetch('/settings?' + q).then(r => r.json()).then(j => {
      fill(j);
      if (!quiet) m.msg = j.ok ? (j.saved ? 'Saved' : 'No change') : (j.error || 'Rejected');
      if (j.ok) say(j.saved ? 'Saved' : 'Nothing changed'); else if (quiet) say(j.error || 'Rejected');
    }).catch(() => say('No reply'));
  }
  const save = () => send('ping_target=' + encodeURIComponent(f.ping.trim()) + '&dns_name=' + encodeURIComponent(f.name.trim())
    + '&dns_server=' + encodeURIComponent(f.dns.trim()) + '&usb_ip=' + encodeURIComponent(f.usb.trim()));
  const adv = () => send('lldp=' + (f.lldp ? 1 : 0) + '&cdp=' + (f.cdp ? 1 : 0), true);
  function reboot() { app.rebooting = f.usb.trim(); fetch('/reboot').catch(() => {}); }
  function formSave() {
    m.fmsg = 'Saving…';
    fetch('/settings?form_url=' + encodeURIComponent(formText.trim())).then(r => r.json()).then(j => {
      formEl.blur(); fill(j);
      m.fmsg = j.ok ? (j.saved ? 'Saved' : 'No change') : (j.error || 'Rejected');
      if (j.ok) say(j.saved ? 'Saved' : 'Nothing changed');
    }).catch(() => say('No reply'));
  }
  // Auto follows the OS; Light and Dark set data-theme on <html>, kept in this browser
  function setTheme(t) {
    try { if (t === 'auto') localStorage.removeItem('picolldp.theme'); else localStorage.setItem('picolldp.theme', t); } catch (e) {}
    if (t === 'auto') document.documentElement.removeAttribute('data-theme'); else document.documentElement.setAttribute('data-theme', t);
    theme = t;
  }
  const enter = (e, fn) => { if (e.key === 'Enter') { e.preventDefault(); fn(); } };
</script>

<div class="card">
  <div class="chead"><span class="sec"><Icon name="tag" size="s"/>Probe number</span></div>
  <div class="ln" style="margin:0 0 8px">Switch sees <span class="mono">picoplug{f.probeSaved !== '' ? '-' + f.probeSaved : ''}</span></div>
  <select class="in" aria-label="Probe number" bind:value={f.probe} onchange={() => send('probe=' + f.probe, true)}>
    <option value="">None</option>
    {#each RES as c, i}<option value={String(i)}>{i} · {c}</option>{/each}
  </select>
</div>
<div class="card">
  <div class="chead"><span class="sec"><Icon name="cast" size="s"/>Advertise</span></div>
  <label class="cb"><input type="checkbox" bind:checked={f.lldp} onchange={adv}> LLDP <span class="meta">{tx.lldp ? tx.lldp.frames + ' sent' : ''}</span></label>
  <label class="cb"><input type="checkbox" bind:checked={f.cdp} onchange={adv}> CDP (Cisco) <span class="meta">{tx.cdp ? tx.cdp.frames + ' sent' + (tx.cdp.enabled && !tx.cdp.peer ? ' · no CDP switch' : '') : ''}</span></label>
</div>
<div class="card">
  <div class="chead"><span class="sec"><Icon name="sun" size="s"/>Appearance</span></div>
  <div class="seg" style="margin:0">
    {#each [['auto', 'Auto'], ['light', 'Light'], ['dark', 'Dark']] as [t, label] (t)}
      <button class:on={theme === t} aria-pressed={theme === t} onclick={() => setTheme(t)}>{label}</button>
    {/each}
  </div>
</div>
<div class="card">
  <div class="chead"><span class="sec"><Icon name="chip" size="s"/>Device</span></div>
  <div class="grid" style="margin:0">
    {#if s}
      {#if s.fw}<Kv k="Firmware">{s.fw}</Kv>{/if}
      <Kv k="Uptime">{fmtDur(s.uptime_s)}</Kv>
      <Kv k="Hardware" wide>{s.board || 'W55RP20-EVB-Pico'}</Kv>
      {#if s.eth.mac}<Kv k="Ethernet MAC" wide copy={s.eth.mac}>{s.eth.mac}</Kv>{/if}
    {/if}
  </div>
</div>
<div class="card">
  <div class="chead"><span class="sec"><Icon name="reach" size="s"/>Port checks and USB</span></div>
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div onkeydown={e => enter(e, save)}>
    <label class="fl" for="s_ping">Ping target</label><input class="in" id="s_ping" inputmode="decimal" autocomplete="off" placeholder={f.defaults.ping_target || ''} bind:value={f.ping}>
    <label class="fl" for="s_name">DNS name</label><input class="in" id="s_name" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder={f.defaults.dns_name || ''} bind:value={f.name}>
    <label class="fl" for="s_dns">DNS server · blank = DHCP</label><input class="in" id="s_dns" inputmode="decimal" autocomplete="off" bind:value={f.dns}>
    <label class="fl" for="s_usb">USB address</label><input class="in" id="s_usb" inputmode="decimal" autocomplete="off" placeholder={f.defaults.usb_ip || ''} bind:value={f.usb}>
    {#if f.reboot}
      <div class="note" style="margin-top:12px;display:block">
        {#if app.rebooting}<b>Rebooting</b> · <span class="mono">http://{app.rebooting}/</span> in ~10 s
        {:else}<b>Applies after reboot</b> · <span class="mono">http://{f.usbSaved}/</span> <div style="margin-top:8px"><button onclick={reboot}>Reboot now</button></div>{/if}
      </div>
    {/if}
    <div class="crow" style="margin-top:12px"><button class="p" onclick={save}>Save</button><button class="q" onclick={() => send('reset=1')}>Defaults</button><span class="meta">{m.msg}</span></div>
  </div>
</div>
<div class="card">
  <div class="chead"><span class="sec"><Icon name="form" size="s"/>Form</span></div>
  <div class="ln" style="margin:0 0 8px">Form button link · blank = no button</div>
  <label class="fl" for="s_form">Link</label>
  <input class="in" id="s_form" inputmode="url" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false"
    bind:this={formEl} bind:value={formText} onkeydown={e => enter(e, formSave)}>
  <div class="ln" style="margin-top:8px">Fill-ins: <span class="mono">{'{switch} {switch_ip} {port} {port_desc} {vlan} {voice_vlan} {ip} {gateway} {probe}'}</span></div>
  <div class="ln" style="margin-top:8px">Now: <span class="mono" style="word-break:break-all">{preview}</span></div>
  <div class="crow" style="margin-top:12px"><button class="p" onclick={formSave}>Save</button><button class="q" onclick={() => { formText = app.formDef; m.fmsg = 'Not saved yet'; }}>Default</button><span class="meta">{m.fmsg}</span></div>
</div>
