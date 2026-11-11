// The Port card's neighbor tabs: LLDP first and shown by default, whichever was heard last.
//   node test/test_neighbors.mjs
import assert from 'node:assert/strict';
import { orderNeighbors, pickCurrent, hKey } from '../web/src/lib/format.js';

const sw = (proto, last, ttl_remaining_s = 110) => ({ proto, chassis_id: 'mac:02:00:5e:10:4d:00', port_id: 'ifname:Gi1/0/9', last_seen_s: last, ttl: 120, ttl_remaining_s });
const cdp = sw('cdp', 2), lldp = sw('lldp', 25);
const peer = { proto: 'lldp', picoplug: true, chassis_id: 'mac:02:50:4c:38:00:05', port_id: 'local:eth0', last_seen_s: 0, ttl: 120, ttl_remaining_s: 120 };

// tabs: LLDP, then CDP, then other testers, whatever order the firmware heard them in
assert.deepEqual(orderNeighbors([peer, cdp, lldp]), [lldp, cdp, peer]);
assert.deepEqual(orderNeighbors([cdp, lldp]), orderNeighbors([lldp, cdp]));
assert.deepEqual(orderNeighbors(undefined), []);
// shown: LLDP even when CDP came in more recently
assert.equal(pickCurrent([cdp, lldp], null), lldp);
assert.equal(pickCurrent([lldp, cdp], null), lldp);
// CDP alone, or LLDP gone stale while CDP is current
assert.equal(pickCurrent([cdp], null), cdp);
assert.equal(pickCurrent([sw('lldp', 90, 30), cdp], null), cdp);
// both stale: back to LLDP; a tester is never picked over a switch
assert.equal(pickCurrent([sw('cdp', 90, 30), sw('lldp', 95, 25)], null).proto, 'lldp');
assert.equal(pickCurrent([peer, cdp], null), cdp);
assert.equal(pickCurrent([peer], null), peer);
// a tab tapped stays picked
assert.equal(pickCurrent([lldp, cdp], hKey(cdp)), cdp);
console.log('test_neighbors: ok');
