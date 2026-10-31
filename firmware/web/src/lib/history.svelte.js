// Ports visited and recent tool runs, kept in this browser (same keys as the shipping page).

import { hKey, stripKind } from './format.js';

const HKEY = 'piccolldp.history.v1'; // old spelling kept on purpose: renaming the key would hide history already stored in browsers
const RKEY = 'picolldp.runs';
const load = k => { try { return JSON.parse(localStorage.getItem(k) || '[]'); } catch (e) { return []; } };
const store = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) {} };

// Every change re-reads storage first, so two tabs open on the device merge instead of overwriting each other.
export const hist = $state({ list: load(HKEY), open: null });
export const runs = $state({ list: load(RKEY) });

function hPut(h) { h = h.slice(0, 300); store(HKEY, h); hist.list = h; }
export function setNote(key, v) { const h = load(HKEY), e = h.find(x => x.key === key); if (e) { e.note = v; hPut(h); } }
export function hDel(key) { hPut(load(HKEY).filter(x => x.key !== key)); }
export function hClear() { hPut([]); }

export function historyUpsert(s) {
  const now = Date.now(), h = load(HKEY);
  let changed = false;
  for (const n of (s.neighbors || [])) {
    if (n.picoplug) continue; // another tester, not a switch port
    const k = hKey(n);
    let e = h.find(x => x.key === k);
    const snap = { sys_name: n.sys_name || '', port: stripKind(n.port_id), port_desc: n.port_desc || '', chassis: n.chassis_id,
      proto: n.proto || 'lldp', pvid: n.pvid, voice: n.voice_vlan && n.voice_vlan.id, mgmt: (n.mgmt_addr || []).join(' '),
      platform: n.platform || '', poe: n.poe && n.poe.alloc_w, vlans: (n.vlans || []).map(v => v.id + ':' + v.name).join(' '), raw: n };
    if (!e) { e = { key: k, first: now, last: now, sessions: 1, note: '', ...snap }; h.unshift(e); }
    else {
      if (now - e.last > 300000) e.sessions++;
      e.last = now; Object.assign(e, snap);
      h.splice(h.indexOf(e), 1); h.unshift(e);
    }
    const d = s.port && s.port.dhcp;
    if (d && d.bound) e.dhcp = d.ip + '/' + d.mask + ' gw ' + d.gateway;
    changed = true;
  }
  if (changed) store(HKEY, h.slice(0, 300));
  // don't yank the note field while typing
  if (!(document.activeElement && document.activeElement.closest('.hist'))) hist.list = h.slice(0, 300);
  runs.list = load(RKEY);
}

export function histCsv() {
  const q = v => '"' + String(v ?? '').replace(/"/g, '""') + '"';
  const rows = [['first_seen', 'last_seen', 'visits', 'switch', 'port', 'port_desc', 'chassis', 'proto', 'pvid', 'voice_vlan', 'vlans', 'mgmt', 'platform', 'poe_alloc_w', 'dhcp', 'note']];
  for (const e of load(HKEY)) rows.push([new Date(e.first).toISOString(), new Date(e.last).toISOString(), e.sessions, e.sys_name, e.port, e.port_desc,
    e.chassis, e.proto, e.pvid, e.voice, e.vlans, e.mgmt, e.platform, e.poe, e.dhcp, e.note]);
  return rows.map(r => r.map(q).join(',')).join('\n');
}

export function runAdd(kind, host, sum, gen) {
  const r = load(RKEY), id = kind + '|' + host + '|' + gen;
  if (r.length && r[0].id === id) return;
  const i = r.findIndex(x => x.k === kind && x.h === host); if (i >= 0) r.splice(i, 1);
  r.unshift({ id, k: kind, h: host, s: sum, t: Date.now() });
  r.splice(6); store(RKEY, r); runs.list = r;
}

// one-line result of a finished ping or trace, for Recent
export function toolSum(t) {
  if (t.kind === 'ping') {
    const rt = t.rtts || [], infl = t.running && rt.length && rt[rt.length - 1] === null ? 1 : 0, ns = t.sent - infl;
    return ns ? t.recv + '/' + ns + ' replies' + (t.recv ? ', avg ' + (t.avg_ms || '<1') + ' ms' : '') : '';
  }
  return t.reached ? 'reached in ' + (t.hops || []).length + ' hops' : 'not reached';
}

export function fmtTime(t) {
  const d = new Date(t);
  return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' }) + ' ' + d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });
}
