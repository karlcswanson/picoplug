// App-wide state: current tab, read-only mode, the Form link setting, the Tools target.

import { conn } from './status.svelte.js';
import { pickCurrent, portName, stripKind, isP } from './format.js';
import { shareParams } from './share.js';
import { sel } from './ui.svelte.js';
import { say } from './copy.svelte.js';

export const VIEWS = ['port', 'tools', 'log', 'settings'];
export const app = $state({
  view: 'port', readonly: false,
  form: '', formDef: '',       // the Share link template and its default
  host: '', qt: '',            // the Tools target and which quick button picked it
  fallback: '', fbFocus: false, // text to copy by hand when the clipboard refuses
  rebooting: '',               // the address the device comes back on after a reboot
});

// tabs: hash routing, no reload; a fresh load always lands on Port
export function route() {
  let v = (location.hash || '').slice(1);
  if (v === 'setup') v = 'settings';   // the tab's old name, so old links still land there
  if (app.readonly || !VIEWS.includes(v)) v = 'port';
  app.view = v;
  window.scrollTo(0, 0);
}
// switch tabs without navigating: iOS home-screen apps can reload the page on a link tap
export function go(h) { if (location.hash !== h) history.pushState(null, '', h); route(); }

export function current() { const s = conn.status; return s ? pickCurrent(s.neighbors || [], sel.key) : undefined; }

// targets the Tools quick buttons and the check rows point at
export function targets() {
  const s = conn.status || {}, d = (s.port && s.port.dhcp) || {}, pr = (s.port && s.port.probe) || {};
  const cur = current() || {};
  return {
    gw: d.gateway || '',
    inet: (pr.internet && pr.internet.target) || '',
    dns: (d.dns && d.dns[0]) || (pr.dns && pr.dns.server) || '',
    sw: (cur.mgmt_addr || []).find(a => !a.includes(':')) || '',
  };
}
export function pickTarget(k) { const t = targets()[k]; app.qt = k; if (t) app.host = t; }

// Share link: the Settings template with {name} fill-ins from the shown neighbor and the lease;
// {share} is all of it as name=value pairs (share.js), already encoded
export function formVals(s, cur, note) {
  const n = cur || {}, d = (s && s.port && s.port.dhcp) || {};
  return { switch: n.sys_name || stripKind(n.chassis_id) || '', switch_ip: (n.mgmt_addr || []).find(a => !a.includes(':')) || '',
    port: cur ? portName(n) : '', port_desc: n.port_desc || '', vlan: n.pvid !== undefined ? n.pvid : '',
    voice_vlan: n.voice_vlan ? n.voice_vlan.id : '', ip: d.bound ? d.ip : '', gateway: d.bound ? d.gateway || '' : '',
    probe: s && isP(s.probe) ? s.probe : '', share: cur ? shareParams(s, n, note) : '' };
}
export const formFill = (t, v) => t.replace(/\{(\w+)\}/g, (m, k) => !(k in v) ? m : k === 'share' ? v[k] : encodeURIComponent(v[k]));

// GET /tool?... and toast the error, if any
export function tool(q, okMsg) {
  return fetch('/tool?' + q).then(r => r.json())
    .then(j => {
      if (!j.ok) say(j.error || 'Rejected');
      else if (okMsg) say(okMsg);
      return j;
    })
    .catch(() => { say('No reply'); });
}

// the Share link setting arrives with /settings, at start and whenever Settings saves
export function formSet(j) {
  if (j.form_url === undefined) return;
  app.form = j.form_url; app.formDef = j.defaults ? j.defaults.form_url : '';
}
