#!/usr/bin/env python3
"""An LLDP-MED network policy with the unknown-policy (U) flag is not a voice VLAN, and CDP's
VoIP VLAN carries no priority or DSCP, so none is reported for it. A civic location reads like a
postal address, without Cisco's private elements.

Cisco IOS-XE sends voice and voice-signaling policies with U set and VLAN 0 on a port that
has no voice VLAN. The frame goes through frame_json (the firmware's parser and JSON writer).

  test_med_policy.py <frame_json binary>
"""
import json, os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))


def tlv(t, body):
    return struct.pack('!H', t << 9 | len(body)) + body


def policy(app, vlan, unknown, tagged=False, prio=0, dscp=0):
    bits = unknown << 23 | tagged << 22 | vlan << 9 | prio << 6 | dscp
    return tlv(127, b'\x00\x12\xbb\x02' + bytes([app]) + bits.to_bytes(3, 'big'))


def frame(*policies):
    eth = bytes.fromhex('0180c200000e' '020000000009' '88cc')
    return (eth + tlv(1, b'\x04' + bytes.fromhex('02005e104d00')) + tlv(2, b'\x05Gi1/0/9') + tlv(3, b'\x00\x78')
            + tlv(127, b'\x00\x12\xbb\x01\x00\x2f\x04') + b''.join(policies) + tlv(0, b'')).hex()


def neighbor(fj, hexframe):
    out = subprocess.run([fj], input=hexframe + '\n', capture_output=True, text=True, check=True).stdout
    nbs = [n for l in out.splitlines() for n in (json.loads(l).get('status') or {}).get('neighbors') or []]
    assert len(nbs) == 1, out
    return nbs[0]


fj = sys.argv[1]
n = neighbor(fj, frame(policy(1, 0, 1), policy(2, 0, 1)))
assert 'voice_vlan' not in n, n.get('voice_vlan')
assert [(p['app'], p.get('unknown_policy')) for p in n['med_policies']] == [('voice', True), ('voice_signaling', True)], n['med_policies']
n = neighbor(fj, frame(policy(1, 620, 0, tagged=True, prio=5, dscp=46)))
assert n['voice_vlan'] == {'id': 620, 'tagged': True, 'priority': 5, 'dscp': 46}, n['voice_vlan']
assert 'unknown_policy' not in n['med_policies'][0]
cdp = ''.join(l for l in open(os.path.join(HERE, 'fixtures', 'cdp_switch.hex')) if not l.startswith('#')).replace(' ', '').replace('\n', '')
n = neighbor(fj, cdp)
assert n['voice_vlan'] == {'id': 30, 'tagged': True}, n['voice_vlan']
assert n['med_policies'] == [{'app': 'voice', 'id': 30, 'tagged': True}], n['med_policies']
# a Catalyst 9200's civic location TLV, as sent: the configured address plus Cisco's CAtype 201
# (its own IP, binary) and 202 (the port name)
loc = bytes.fromhex('0012bb03024b0255530102494c03074368696361676f1303343030180536303630361909436c6f636b636c75621b01321c03464f48'
                    '220b57205761636b6572204472c904c0000202ca074769312f302f39')
n = neighbor(fj, frame(tlv(127, loc)))
assert n['location']['text'] == 'Room FOH, Floor 2, Clockclub, 400 W Wacker Dr, Chicago, IL 60606, US', n['location']
assert n['location']['country'] == 'US'
assert [(e['type'], e['value']) for e in n['location']['civic']] == [
    (1, 'IL'), (3, 'Chicago'), (19, '400'), (24, '60606'), (25, 'Clockclub'), (27, '2'), (28, 'FOH'), (34, 'W Wacker Dr')], n['location']['civic']
print('test_med_policy: ok')
