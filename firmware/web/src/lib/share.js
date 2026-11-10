// The Share link: what the Port tab shows, as name=value pairs. The default link puts them
// after the '#' of the picoplug share page (clockclub.net/apps/picoplug/), where a browser keeps
// them: the web server never sees the switch, port or addresses. The same pairs fill {share} in
// any link set in Setup. Field list: docs/PROTOCOL.md, "Share link".
import { stripKind, portName, isP } from './format.js';

export const SHARE_PAGE = 'https://clockclub.net/apps/picoplug/#{share}';
// some chat apps and older software cut links near 2000 characters: the VLAN list stops short of that
const MAX_LINK = 1800, PAGE_LEN = 40;
export const NOTE_MAX = 200;

const prefix = mask => (mask || '').split('.').reduce((n, o) => n + ((+o).toString(2).match(/1/g) || []).length, 0);
const join = (...a) => a.filter(x => x !== undefined && x !== null && x !== '').join(' ');

export function shareParams(s, n, note) {
  s = s || {};
  const q = new URLSearchParams(), d = (s.port && s.port.dhcp) || {}, inv = n.inventory || {}, e = s.eth || {};
  const add = (k, v) => { if (v !== undefined && v !== null && v !== '') q.append(k, String(v)); };
  const port = portName(n);
  // the switch and port
  add('sw', n.sys_name || stripKind(n.chassis_id));
  add('port', port);
  if (n.port_desc && n.port_desc !== port && n.port_desc !== stripKind(n.port_id)) add('desc', n.port_desc);
  if (note && note.trim()) add('note', note.trim().slice(0, NOTE_MAX));
  add('vlan', n.pvid);
  if (n.voice_vlan) add('voice', n.voice_vlan.id);
  add('mgmt', (n.mgmt_addr || []).find(a => !a.includes(':')) || (n.mgmt_addr || [])[0]);
  add('model', join(inv.manufacturer, inv.model) || n.platform);
  if (n.poe && n.poe.alloc_w !== undefined) add('poe', n.poe.alloc_w);
  if (n.location) add('loc', n.location.text);
  add('via', (n.proto || 'lldp').toUpperCase());
  // the link and the tester
  if (e.link && e.speed) add('link', e.speed + 'M ' + (e.duplex === 'full' ? 'FD' : 'HD'));
  add('mac', e.mac);
  if (d.bound) { add('ip', d.ip + (d.mask ? '/' + prefix(d.mask) : '')); add('gw', d.gateway); }
  for (const ip of d.servers || (d.server ? [d.server] : [])) add('dhcp', ip);
  if (s && isP(s.probe)) add('probe', s.probe);
  // what else the port carries, each only when heard
  const st = s.stp;
  if (st && st.seen) {
    add('stp', join((st.proto || '').toUpperCase(), st.role, st.state !== 'unknown' ? st.state : ''));
    if (st.root) add('root', join(st.root.priority, st.root.mac));
  }
  const lag = n.link_agg && n.link_agg.active ? 'member of aggregate ' + n.link_agg.port_id : '';
  const l = s.lacp, a = l && l.seen && l.actor;
  add('lag', join(lag, a ? 'LACP ' + (a.collecting && a.distributing ? 'forwarding' : 'not forwarding') + (a.key !== undefined ? ' key ' + a.key : '') : ''));
  const x = s.dot1x;
  if (x && x.seen) add('dot1x', join('auth requested', x.eap, x.failures ? x.failures + ' failures' : ''));
  const ig = s.igmp;
  if (ig && ig.seen) add('igmp', join(ig.querier, 'v' + ig.version, ig.vlan ? 'VLAN ' + ig.vlan : ''));
  if (ig && ig.mrouter) add('pim', ig.mrouter.ip);
  const pt = s.ptp || {};
  if (pt.seen && pt.v1) add('ptp', join('v1', pt.v1.subdomain, pt.v1.master_ip || pt.v1.master));
  if (pt.seen && pt.v2) add('ptp', join('v2 domain ' + pt.v2.domain, pt.v2.master_ip || pt.v2.master,
    pt.v2.grandmaster ? 'GM ' + pt.v2.grandmaster.id : ''));
  add('t', Math.round(Date.now() / 1000));
  // VLANs last, as many as fit; how many did not
  const vl = n.vlans || [];
  let out = q.toString();
  for (let i = 0; i < vl.length; i++) {
    const p = 'vl=' + encodeURIComponent(vl[i].id + (vl[i].name ? ' ' + vl[i].name : '')).replace(/%20/g, '+');
    if (PAGE_LEN + out.length + p.length + 16 > MAX_LINK) { out += '&vlmore=' + (vl.length - i); break; }
    out += '&' + p;
  }
  return out;
}
