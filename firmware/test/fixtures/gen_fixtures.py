#!/usr/bin/env python3
"""
Generate LLDP test fixtures as hex dumps.

The frames are synthetic but modelled on the TLV sets that each vendor's
LLDP agent actually emits (see README.md). They are built with a small
raw TLV encoder so every byte is explicit, then cross-checked with Scapy's
LLDP dissector (scapy.contrib.lldp) to catch encoding mistakes.

    python3 gen_fixtures.py          # writes *.hex next to this script
    python3 gen_fixtures.py --check  # only dissect existing .hex files
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

LLDP_MCAST = "01:80:c2:00:00:0e"
OUI_8021 = bytes([0x00, 0x80, 0xC2])
OUI_8023 = bytes([0x00, 0x12, 0x0F])
OUI_MED = bytes([0x00, 0x12, 0xBB])
OUI_CISCO = bytes([0x00, 0x01, 0x42])
OUI_JUNIPER = bytes([0x00, 0x90, 0x69])


def mac(s):
    return bytes.fromhex(s.replace(":", ""))


def tlv(t, v):
    assert 0 <= t < 128 and len(v) < 512
    return struct.pack(">H", (t << 9) | len(v)) + v


def org(oui, subtype, body):
    return tlv(127, oui + bytes([subtype]) + body)


def chassis(subtype, value):
    return tlv(1, bytes([subtype]) + value)


def port(subtype, value):
    return tlv(2, bytes([subtype]) + value)


def ttl(seconds):
    return tlv(3, struct.pack(">H", seconds))


def port_desc(s):
    return tlv(4, s.encode())


def sys_name(s):
    return tlv(5, s.encode())


def sys_desc(s):
    return tlv(6, s.encode())


def caps(supported, enabled):
    return tlv(7, struct.pack(">HH", supported, enabled))


def mgmt(afi, addr, if_subtype, if_num, oid=b""):
    return tlv(8, bytes([1 + len(addr), afi]) + addr + bytes([if_subtype]) +
               struct.pack(">I", if_num) + bytes([len(oid)]) + oid)


def ipv4(s):
    return bytes(int(x) for x in s.split("."))


def ipv6(s):
    import ipaddress
    return ipaddress.IPv6Address(s).packed


def pvid(vid):
    return org(OUI_8021, 1, struct.pack(">H", vid))


def vlan_name(vid, name):
    return org(OUI_8021, 3, struct.pack(">H", vid) + bytes([len(name)]) + name.encode())


def macphy(flags, pmd_adv, mau):
    return org(OUI_8023, 1, bytes([flags]) + struct.pack(">HH", pmd_adv, mau))


def power_mdi(support, pairs, pclass, at=None):
    body = bytes([support, pairs, pclass])
    if at is not None:
        tsp, req, alloc = at
        body += bytes([tsp]) + struct.pack(">HH", req, alloc)
    return org(OUI_8023, 2, body)


def max_frame(n):
    return org(OUI_8023, 4, struct.pack(">H", n))


def med_caps(c, devtype):
    return org(OUI_MED, 1, struct.pack(">H", c) + bytes([devtype]))


def med_policy(app, u, t, vid, pri, dscp):
    bits = (u << 23) | (t << 22) | (vid << 9) | (pri << 6) | dscp
    return org(OUI_MED, 2, bytes([app]) + bits.to_bytes(3, "big"))


def med_power(ptype, source, prio, deciwatts):
    return org(OUI_MED, 4, bytes([(ptype << 6) | (source << 4) | prio]) +
               struct.pack(">H", deciwatts))


def end():
    return tlv(0, b"")


def frame(src, tlvs, pad_to=60, dst=LLDP_MCAST, vlan=None):
    hdr = mac(dst) + mac(src)
    if vlan is not None:
        hdr += b"\x81\x00" + struct.pack(">H", vlan)
    body = hdr + b"\x88\xcc" + b"".join(tlvs)
    if len(body) < pad_to:
        body += b"\x00" * (pad_to - len(body))
    return body


# Capability bits
OTHER, REPEATER, BRIDGE, WLAN, ROUTER, PHONE, DOCSIS, STATION = (1 << i for i in range(8))

# 802.3 Power via MDI support byte: bit0 PSE, bit1 supported, bit2 enabled, bit3 pairs ctl
PSE_ON = 0x07
# type/source/priority byte: type(2b) source(2b) priority(4b)
AT_PSE_PRIMARY_LOW = (0 << 6) | (1 << 4) | 3   # 0x13
MAU_1000BASE_T_FD = 30
PMD_ADV_TYPICAL = 0x6C01

FIXTURES = {}

# ---------------------------------------------------------------- Cisco IOS
CISCO_SYSDESC = (
    "Cisco IOS Software, C2960X Software (C2960X-UNIVERSALK9-M), Version 15.2(7)E4, "
    "RELEASE SOFTWARE (fc2)\nTechnical Support: http://www.cisco.com/techsupport\n"
    "Copyright (c) 1986-2021 by Cisco Systems, Inc.\nCompiled Tue 09-Mar-21 21:04 by prod_rel_team"
)
FIXTURES["cisco"] = frame("00:1e:14:ab:cd:8c", [
    chassis(4, mac("00:1e:14:ab:cd:80")),
    port(5, b"Gi1/0/12"),
    ttl(120),
    port_desc("GigabitEthernet1/0/12"),
    sys_name("sw-core-2"),
    sys_desc(CISCO_SYSDESC),
    caps(BRIDGE | ROUTER, BRIDGE),
    mgmt(1, ipv4("10.0.0.2"), 2, 2),
    pvid(20),
    macphy(0x03, PMD_ADV_TYPICAL, MAU_1000BASE_T_FD),
    power_mdi(PSE_ON, 1, 4, at=(AT_PSE_PRIMARY_LOW, 130, 154)),
    med_caps(0x000F, 4),
    med_policy(1, 0, 1, 30, 5, 46),
    med_policy(2, 0, 1, 30, 5, 46),
    med_power(0, 1, 3, 154),
    org(OUI_CISCO, 1, b"\x0d"),           # Cisco 4-wire power TLV (unknown to us)
    end(),
])

# ---------------------------------------------------------------- Aruba/HPE
FIXTURES["aruba"] = frame("94:f1:28:12:34:56", [
    chassis(4, mac("94:f1:28:12:34:40")),
    port(7, b"12"),
    ttl(120),
    port_desc("12"),
    sys_name("aruba-2930f-01"),
    sys_desc("Aruba JL258A 2930F-8G-PoE+-2SFP+ Switch, revision WC.16.10.0011, ROM WC.16.01.0006"),
    caps(BRIDGE | ROUTER, BRIDGE),
    mgmt(1, ipv4("192.168.1.5"), 2, 1, bytes.fromhex("2b06010401 0b020e0b 050109".replace(" ", ""))),
    mgmt(2, ipv6("fe80::96f1:28ff:fe12:3440"), 2, 1),
    pvid(1),
    vlan_name(1, "DEFAULT_VLAN"),
    vlan_name(20, "USERS"),
    vlan_name(30, "VOICE"),
    macphy(0x03, PMD_ADV_TYPICAL, MAU_1000BASE_T_FD),
    power_mdi(PSE_ON, 1, 3, at=(AT_PSE_PRIMARY_LOW, 130, 70)),
    max_frame(9220),
    med_caps(0x000F, 4),
    med_policy(1, 0, 1, 30, 5, 46),
    med_power(0, 1, 3, 70),
    end(),
])

# ---------------------------------------------------------------- Juniper
FIXTURES["juniper"] = frame("f4:b5:2f:aa:bb:c0", [
    chassis(4, mac("f4:b5:2f:aa:bb:80")),
    port(7, b"525"),
    ttl(120),
    port_desc("ge-0/0/12"),
    sys_name("ex2300-lab"),
    sys_desc("Juniper Networks, Inc. ex2300-24p Ethernet Switch, kernel JUNOS 20.4R3.8, "
             "Build date: 2021-09-16 05:36:15 UTC Copyright (c) 1996-2021 Juniper Networks, Inc."),
    caps(BRIDGE | ROUTER, BRIDGE | ROUTER),
    mgmt(1, ipv4("10.10.10.1"), 2, 525),
    pvid(20),
    vlan_name(20, "users"),
    macphy(0x03, PMD_ADV_TYPICAL, MAU_1000BASE_T_FD),
    power_mdi(PSE_ON, 1, 4, at=(AT_PSE_PRIMARY_LOW, 130, 130)),
    max_frame(1514),
    org(OUI_JUNIPER, 1, b"ex2300-24p"),   # Juniper chassis model (unknown to us)
    med_caps(0x000F, 4),
    med_policy(1, 0, 1, 30, 5, 46),
    med_power(0, 1, 3, 130),
    end(),
])

# ---------------------------------------------------------------- Ubiquiti
# No 802.3at DLL extension, and real zero padding after End (60-byte minimum).
FIXTURES["ubiquiti"] = frame("78:45:58:aa:bb:cc", [
    chassis(4, mac("78:45:58:aa:bb:cc")),
    port(7, b"12"),
    ttl(120),
    port_desc("Port 12"),
    sys_name("USW-24-PoE"),
    sys_desc("USW-24-PoE, 6.5.59.14777, Linux 3.6.5"),
    caps(BRIDGE, BRIDGE),
    mgmt(1, ipv4("192.168.1.10"), 2, 1),
    pvid(1),
    macphy(0x03, PMD_ADV_TYPICAL, MAU_1000BASE_T_FD),
    power_mdi(PSE_ON, 1, 5),
    med_caps(0x0007, 4),
    end(),
], pad_to=160)

# ---------------------------------------------------------------- Linux lldpd
# lldpd with -M 3 (MED class III + inventory). Port ID is a MAC, PD side.
FIXTURES["lldpd"] = frame("52:54:00:12:34:56", [
    chassis(4, mac("52:54:00:12:34:56")),
    port(3, mac("52:54:00:12:34:56")),
    ttl(120),
    sys_name("host.example.com"),
    sys_desc("Debian GNU/Linux 12 (bookworm) Linux 6.1.0-18-amd64 #1 SMP PREEMPT_DYNAMIC "
             "Debian 6.1.76-1 (2024-02-01) x86_64"),
    caps(BRIDGE | WLAN | ROUTER | STATION, STATION),
    mgmt(1, ipv4("192.0.2.10"), 2, 2),
    mgmt(2, ipv6("2001:db8::10"), 2, 2),
    port_desc("eth0"),
    macphy(0x03, PMD_ADV_TYPICAL, MAU_1000BASE_T_FD),
    org(OUI_8023, 3, b"\x01\x00\x00\x00\x00"),   # link aggregation (unknown to us)
    max_frame(1518),
    med_caps(0x0011, 3),
    org(OUI_MED, 5, b"1.0"),                      # inventory: hardware revision
    org(OUI_MED, 6, b"rel-1.16.2-0"),             # firmware revision
    org(OUI_MED, 7, b"6.1.0-18-amd64"),           # software revision
    org(OUI_MED, 8, b"QEMU-1234"),                # serial number
    org(OUI_MED, 9, b"QEMU"),                     # manufacturer
    org(OUI_MED, 10, b"Standard PC (Q35 + ICH9, 2009)"),  # model
    end(),
])


# ======================================================================
# CDP fixtures (Cisco Discovery Protocol over 802.3/LLC/SNAP)
# ======================================================================
CDP_MCAST = "01:00:0c:cc:cc:cc"
CDP_SNAP = bytes.fromhex("aaaa0300000c2000")


def cdp_tlv(t, v):
    return struct.pack(">HH", t, len(v) + 4) + v


def cdp_str(t, s):
    return cdp_tlv(t, s.encode())


def cdp_addrs(t, addrs):
    recs = b""
    for a in addrs:
        if ":" in a:
            recs += bytes([2, 8]) + bytes.fromhex("aaaa03000000" + "86dd") + struct.pack(">H", 16) + ipv6(a)
        else:
            recs += bytes([1, 1, 0xCC]) + struct.pack(">H", 4) + ipv4(a)
    return cdp_tlv(t, struct.pack(">I", len(addrs)) + recs)


def cdp_checksum(data, quirk):
    """Internet checksum; Cisco puts an odd trailing byte in the LOW byte."""
    total = 0
    for i in range(0, len(data) - 1, 2):
        total += (data[i] << 8) | data[i + 1]
    if len(data) & 1:
        total += data[-1] if quirk else (data[-1] << 8)
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def cdp_frame(src, version, ttl, tlvs, quirk=False, pad_to=60, vlan=None):
    payload = bytes([version, ttl, 0, 0]) + b"".join(tlvs)
    ck = cdp_checksum(payload, quirk)
    payload = payload[:2] + struct.pack(">H", ck) + payload[4:]
    body = CDP_SNAP + payload
    hdr = mac(CDP_MCAST) + mac(src)
    if vlan is not None:
        hdr += b"\x81\x00" + struct.pack(">H", vlan)
    f = hdr + struct.pack(">H", len(body)) + body
    if len(f) < pad_to:
        f += b"\x00" * (pad_to - len(f))
    return f


CDP_SWITCH_VERSION = (
    "Cisco IOS Software, C2960X Software (C2960X-UNIVERSALK9-M), Version 15.2(7)E4, "
    "RELEASE SOFTWARE (fc2)\nTechnical Support: http://www.cisco.com/techsupport\n"
    "Copyright (c) 1986-2021 by Cisco Systems, Inc.\nCompiled Tue 09-Mar-21 21:04 by prod_rel_team"
)
# Catalyst 2960X access switch; odd payload length -> Cisco checksum quirk
FIXTURES["cdp_switch"] = cdp_frame("00:1e:14:ab:cd:8c", 2, 180, [
    cdp_str(1, "sw-access-1.example.com"),
    cdp_addrs(2, ["10.0.0.2", "2001:db8::2"]),
    cdp_str(3, "GigabitEthernet1/0/12"),
    cdp_tlv(4, struct.pack(">I", 0x28)),                 # switch | IGMP
    cdp_str(5, CDP_SWITCH_VERSION),
    cdp_str(6, "cisco WS-C2960X-48FPD-L"),
    cdp_tlv(8, bytes.fromhex("00000c011200000000ffffffff010221ff000000000000001e14abcd8000ff")),  # protocol hello
    cdp_str(9, "corp"),
    cdp_tlv(10, struct.pack(">H", 20)),                  # native VLAN
    cdp_tlv(11, b"\x01"),                                # full duplex
    cdp_tlv(14, b"\x20" + struct.pack(">H", 30)),        # VoIP VLAN reply
    cdp_tlv(18, b"\x00"),                                # trust bitmap
    cdp_tlv(19, b"\x00"),                                # untrusted CoS
    cdp_str(20, "sw-access-1"),
    cdp_addrs(22, ["10.0.0.2"]),
    cdp_tlv(26, struct.pack(">HHII", 0, 0, 15400, 0xFFFFFFFF)),  # power available
], quirk=True)

# Cisco IP phone (PD): power request, VoIP VLAN query
FIXTURES["cdp_phone"] = cdp_frame("00:11:22:33:44:55", 2, 180, [
    cdp_str(1, "SEP001122334455"),
    cdp_addrs(2, ["10.0.30.55"]),
    cdp_str(3, "Port 1"),
    cdp_tlv(4, struct.pack(">I", 0x90)),                 # host | phone
    cdp_str(5, "sip8845_65.14-1-1-0001-125"),
    cdp_str(6, "Cisco IP Phone 8845"),
    cdp_tlv(11, b"\x01"),
    cdp_tlv(15, b"\x20"),                                # VoIP VLAN query
    cdp_tlv(25, struct.pack(">HHI", 1, 0, 12000)),       # power request 12 W
    cdp_tlv(16, struct.pack(">H", 6300)),                # power consumption (legacy)
])

# ISR router with IP prefix TLV (unknown to us) and 4-byte MTU
FIXTURES["cdp_router"] = cdp_frame("50:1c:b0:aa:bb:01", 2, 180, [
    cdp_str(1, "rtr-edge-1"),
    cdp_addrs(2, ["192.0.2.1", "198.51.100.1"]),
    cdp_str(3, "GigabitEthernet0/0/1"),
    cdp_tlv(4, struct.pack(">I", 0x29)),                 # router | switch | IGMP
    cdp_str(5, "Cisco IOS XE Software, Version 17.09.04a"),
    cdp_str(6, "cisco ISR4331/K9"),
    cdp_tlv(7, bytes([192, 0, 2, 0, 24])),               # IP prefix 192.0.2.0/24
    cdp_tlv(11, b"\x01"),
    cdp_tlv(17, struct.pack(">I", 1500)),
    cdp_str(20, "rtr-edge-1"),
])

# CDPv1 from an old Catalyst 2950
FIXTURES["cdp_v1"] = cdp_frame("00:0d:bc:12:34:56", 1, 180, [
    cdp_str(1, "OldSwitch"),
    cdp_addrs(2, ["192.168.1.2"]),
    cdp_str(3, "FastEthernet0/1"),
    cdp_tlv(4, struct.pack(">I", 0x0A)),                 # TB bridge | switch
    cdp_str(5, "Cisco Internetwork Operating System Software \nIOS (tm) C2950 Software (C2950-I6Q4L2-M), Version 12.1(22)EA14"),
    cdp_str(6, "cisco WS-C2950-24"),
])


def scapy_check_cdp(name, data):
    try:
        from scapy.all import Dot3
        from scapy.contrib import cdp as C
    except ImportError:
        print(f"  [{name}] scapy not available")
        return True
    p = Dot3(data)          # 802.3 length field, then LLC/SNAP/CDP
    ok = p.haslayer(C.CDPv2_HDR) and p.haslayer(C.CDPMsgDeviceID) and p.haslayer(C.CDPMsgPortID)
    n = len(p[C.CDPv2_HDR].msg) if ok else 0
    dev = p[C.CDPMsgDeviceID].val if ok else b""
    print(f"  [{name}] scapy: {n} TLVs, v{p[C.CDPv2_HDR].vers if ok else '?'}, device={dev!r}, {'ok' if ok else 'MISSING'}")
    return bool(ok)


def write_hex(name, data):
    path = os.path.join(HERE, name + ".hex")
    with open(path, "w") as f:
        f.write(f"# {name}: synthetic LLDP frame, {len(data)} bytes. Generated by gen_fixtures.py\n")
        f.write("# Format: hex bytes, whitespace separated, '#' starts a comment.\n")
        for i in range(0, len(data), 16):
            f.write(" ".join(f"{b:02x}" for b in data[i:i + 16]) + "\n")
    return path


def read_hex(path):
    out = bytearray()
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0]
            for tok in line.split():
                out.append(int(tok, 16))
    return bytes(out)


def scapy_check(name, data):
    try:
        from scapy.all import Ether  # noqa
        from scapy.contrib import lldp as L  # noqa
    except ImportError:
        print(f"  [{name}] scapy not available, skipping dissection check")
        return True
    p = Ether(data)
    ok = p.haslayer(L.LLDPDUChassisID) and p.haslayer(L.LLDPDUPortID) and \
        p.haslayer(L.LLDPDUTimeToLive) and p.haslayer(L.LLDPDUEndOfLLDPDU)
    n = 0
    layer = p.getlayer(L.LLDPDU)
    while layer is not None and isinstance(layer, L.LLDPDU):
        n += 1
        layer = layer.payload
    sysname = p[L.LLDPDUSystemName].system_name if p.haslayer(L.LLDPDUSystemName) else b""
    print(f"  [{name}] scapy: {n} TLVs, mandatory+end={'ok' if ok else 'MISSING'}, "
          f"sysname={sysname!r}")
    return ok


def main():
    check_only = "--check" in sys.argv
    all_ok = True
    for name, data in FIXTURES.items():
        if not check_only:
            path = write_hex(name, data)
            rt = read_hex(path)
            assert rt == data, f"round trip mismatch for {name}"
            print(f"wrote {path} ({len(data)} bytes)")
        else:
            data = read_hex(os.path.join(HERE, name + ".hex"))
        all_ok &= scapy_check_cdp(name, data) if name.startswith("cdp_") else scapy_check(name, data)
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
