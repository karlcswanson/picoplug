// Formatting and neighbor helpers shared by the cards.

export const stripKind = s => s && s.replace(/^[a-z]+:/, '');
// hosts running lldpd (macOS, Linux) send a MAC as the port ID and the interface name as the description
export const portName = n => /^mac:/.test(n.port_id || '') && n.port_desc ? n.port_desc : stripKind(n.port_id);
export const isHost = n => {
  const c = n.caps || [];
  return /\bDarwin\b|Windows/.test(n.sys_desc || '') || (c.length > 0 && c.every(x => x === 'station'));
};
export const isStale = n => n.ttl && n.ttl_remaining_s !== undefined && n.ttl_remaining_s < n.ttl / 2;
export const v4first = a => (a || []).slice().sort((x, y) => (x.includes(':') ? 1 : 0) - (y.includes(':') ? 1 : 0));
export const hKey = n => (n.proto || 'lldp') + '|' + n.chassis_id + '|' + n.port_id;

export const fmtAgo = s => s < 60 ? s + ' s ago' : s < 3600 ? Math.floor(s / 60) + ' min ago' : Math.floor(s / 3600) + ' h ago';
export const fmtDur = s => s === undefined ? '-' : s < 120 ? s + ' s' : s < 7200 ? Math.round(s / 60) + ' min'
  : s < 172800 ? Math.round(s / 3600) + ' h' : Math.round(s / 86400) + ' d';

// the device reports sub-millisecond times as 0
export const fmtMs = ms => (ms || '<1') + ' ms';
// font size that keeps a long port name on one line (mono glyphs are ~0.62em wide)
export const portFit = p => p && p.length > 12 ? `min(36px,calc((100cqw - 32px) / ${(0.62 * p.length).toFixed(2)}))` : undefined;

export const isP = n => typeof n === 'number' && n >= 0 && n <= 9;

export function pickCurrent(ns, selKey) {
  if (selKey) { const k = ns.find(n => hKey(n) === selKey); if (k) return k; }
  return ns.slice().sort((a, b) => (!!a.picoplug - !!b.picoplug) || (a.last_seen_s - b.last_seen_s)
    || ((a.proto || 'lldp') === 'lldp' ? -1 : 1))[0];
}

export function summary(n) {
  const L = [];
  if (n.sys_name) L.push('Switch: ' + n.sys_name + ' (' + (n.proto || 'lldp').toUpperCase() + ')');
  if (n.platform) L.push('Platform: ' + n.platform);
  L.push('Port: ' + stripKind(n.port_id) + (n.port_desc ? ' (' + n.port_desc + ')' : ''));
  L.push('Chassis: ' + n.chassis_id);
  if (n.mgmt_addr) L.push('Mgmt: ' + n.mgmt_addr.join(', '));
  if (n.pvid !== undefined) L.push('PVID: ' + n.pvid);
  if (n.vlans) L.push('VLANs: ' + n.vlans.map(v => v.id + ' ' + v.name).join(', '));
  if (n.voice_vlan) L.push('Voice VLAN: ' + n.voice_vlan.id + (n.voice_vlan.tagged ? ' tagged' : '') + (n.voice_vlan.dscp !== undefined ? ' dscp ' + n.voice_vlan.dscp : ''));
  if (n.poe) L.push('PoE: ' + (n.poe.class !== undefined ? 'class ' + n.poe.class : '')
    + (n.poe.alloc_w !== undefined ? ', ' + n.poe.alloc_w + ' W allocated' : '')
    + (n.poe.req_w !== undefined ? ', ' + n.poe.req_w + ' W requested' : ''));
  if (n.mgmt_vid !== undefined) L.push('Mgmt VLAN: ' + n.mgmt_vid);
  if (n.location) L.push('Location: ' + n.location.text);
  if (n.caps) L.push('Caps: ' + n.caps.join(', '));
  if (n.sys_desc) L.push('System: ' + n.sys_desc);
  return L.join('\n');
}

export const RES = ['black', 'brown', 'red', 'orange', 'yellow', 'green', 'blue', 'violet', 'grey', 'white'];
export const waiting = (up, need) => up < need ? 'listening… ' + up + ' s' : '';
