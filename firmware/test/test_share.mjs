// The Share link (web/src/lib/share.js): fields, the note, and the length cap on a big trunk.
//   node test/test_share.mjs
import assert from 'node:assert/strict';
import { shareParams, NOTE_MAX } from '../web/src/lib/share.js';

const n = {
  proto: 'lldp', sys_name: 'sw-access-4.example.net', chassis_id: 'mac:02:00:5e:10:4d:00', port_id: 'ifname:Gi1/0/9',
  port_desc: 'FOH rack 2', pvid: 4000, voice_vlan: { id: 620, tagged: true }, mgmt_addr: ['192.0.2.2'],
  inventory: { manufacturer: 'Cisco Systems, Inc.', model: 'C9200CX-12P-2X2G' }, link_agg: { capable: true, active: true, port_id: 1001 },
  vlans: Array.from({ length: 100 }, (_, i) => ({ id: 100 + i, name: 'VLAN' + String(100 + i).padStart(4, '0') })),
};
const s = {
  probe: 2, eth: { link: true, speed: 100, duplex: 'full', mac: '02:50:4c:38:00:19' },
  port: { dhcp: { bound: true, ip: '198.51.100.101', mask: '255.255.255.0', gateway: '198.51.100.1', servers: ['198.51.100.1'] } },
  stp: { seen: true, proto: 'mstp', role: 'designated', state: 'forwarding', root: { priority: 16384, mac: '02:00:5e:10:4d:00' } },
  lacp: { seen: true, actor: { collecting: true, distributing: true, key: 7 } },
  dot1x: { seen: true, eap: 'identity' },
  igmp: { seen: true, querier: '192.0.2.2', version: 2, mrouter: { ip: '192.0.2.1' } },
  ptp: { seen: true, v1: { subdomain: '_DFLT', master_ip: '198.51.100.129' }, v2: { domain: 0, master_ip: '198.51.100.50', grandmaster: { id: '001dc1fffe123456' } } },
};
const out = shareParams(s, n, '  under the stage, cable A12  ' + 'x'.repeat(400));
const q = new URLSearchParams(out);
const want = {
  sw: 'sw-access-4.example.net', port: 'Gi1/0/9', desc: 'FOH rack 2', vlan: '4000', voice: '620', mgmt: '192.0.2.2',
  model: 'Cisco Systems, Inc. C9200CX-12P-2X2G', via: 'LLDP', link: '100M FD', mac: '02:50:4c:38:00:19', ip: '198.51.100.101/24',
  gw: '198.51.100.1', dhcp: '198.51.100.1', probe: '2', stp: 'MSTP designated forwarding', root: '16384 02:00:5e:10:4d:00',
  lag: 'member of aggregate 1001 LACP forwarding key 7', dot1x: 'auth requested identity', igmp: '192.0.2.2 v2', pim: '192.0.2.1',
};
for (const [k, v] of Object.entries(want)) assert.equal(q.get(k), v, k);
assert.deepEqual(q.getAll('ptp'), ['v1 _DFLT 198.51.100.129', 'v2 domain 0 198.51.100.50 GM 001dc1fffe123456']);
assert.equal(q.get('note').length, NOTE_MAX);
assert.ok(q.get('note').startsWith('under the stage, cable A12'));
// a 100-VLAN trunk: the link stays under 1800 characters with the page in front, and says how many were left out
const link = 'https://clockclub.net/apps/picoplug/#' + out;
assert.ok(link.length <= 1800, 'link length ' + link.length);
const vl = q.getAll('vl'), more = +q.get('vlmore');
assert.ok(vl.length > 10 && more > 0 && vl.length + more === 100, `vl ${vl.length} + vlmore ${more}`);
assert.equal(vl[0], '100 VLAN0100');
// a small port: every VLAN, no vlmore, no note
const small = new URLSearchParams(shareParams(s, { ...n, vlans: n.vlans.slice(0, 3) }, ''));
assert.equal(small.getAll('vl').length, 3); assert.equal(small.get('vlmore'), null); assert.equal(small.get('note'), null);
console.log('test_share: ok (100-VLAN link ' + link.length + ' chars, ' + vl.length + ' VLANs in it)');
