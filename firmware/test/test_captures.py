#!/usr/bin/env python3
"""Check our decode of real switch captures against lldpd's.

Reads ref/lldp-captures.jsonl (private, gitignored; skipped when absent):
one record per capture run with the raw frames as hex and lldpd's JSON
decode of the same neighbors. Every frame goes through frame_json (the
firmware's parsers and JSON writer); each neighbor lldpd reports is then
compared field by field with our last decode from the same chassis and
port. lldpd keeps neighbors for their TTL, so it can list one whose frames
fell outside the capture window; those are reported, not failed.

  test_captures.py <frame_json binary> [captures.jsonl] [-v]
"""
import json
import os
import subprocess
import sys

SKIP = 77
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, "..", "..", "ref", "lldp-captures.jsonl")


def entries(rec):
    """lldpd neighbor entries: (via, name, chassis, port, vlans, raw)."""
    it = rec["neighbors"].get("lldp", {}).get("interface", [])
    for x in it if isinstance(it, list) else [it]:
        for e in x.values():
            ch = e["chassis"]
            if "id" in ch:
                name, ch = None, ch
            else:
                name, ch = next(iter(ch.items()))
            v = e.get("vlan", [])
            yield e["via"], name, ch, e.get("port", {}), v if isinstance(v, list) else [v], e


def as_list(v):
    return v if isinstance(v, list) else [] if v is None else [v]


def compare(via, name, ch, port, vlans, nb):
    """Return a list of 'field: lldpd X, ours Y' strings."""
    bad = []

    def eq(field, want, got):
        if want != got:
            bad.append(f"{field}: lldpd {want!r}, ours {got!r}")

    kind, _, val = nb.get("chassis_id", "").partition(":")
    eq("chassis id type", ch["id"]["type"], kind)
    eq("chassis id", ch["id"]["value"].lower(), val.lower())
    if name and name != ch["id"]["value"]:
        eq("system name", name, nb.get("sys_name"))
    if "descr" in ch:
        eq("system description", ch["descr"].rstrip("\n"), (nb.get("sys_desc") or "").rstrip("\n"))
    for ip in as_list(ch.get("mgmt-ip")):
        if ip not in nb.get("mgmt_addr", []):
            bad.append(f"mgmt address {ip} missing, ours {nb.get('mgmt_addr')}")
    caps = sorted(c["type"].lower() for c in as_list(ch.get("capability")) if c.get("enabled"))
    if caps:
        eq("capabilities", caps, sorted(nb.get("caps", [])))

    kind, _, val = nb.get("port_id", "").partition(":")
    eq("port id type", port["id"]["type"], kind)
    eq("port id", port["id"]["value"], val)
    if "descr" in port and via.startswith("LLDP"):
        eq("port description", port["descr"], nb.get("port_desc"))
    if "ttl" in port:
        eq("ttl", int(port["ttl"]), nb.get("ttl"))
    if "mfs" in port:
        eq("max frame", int(port["mfs"]), nb.get("max_frame"))
    pw = port.get("power")
    if pw:
        poe = nb.get("poe") or {}
        if not poe:
            bad.append("PoE TLV missing")
        else:
            eq("PoE port class", pw["device-type"].lower(), poe.get("port_class"))
            eq("PoE supported", pw["supported"], poe.get("supported"))
            eq("PoE enabled", pw["enabled"], poe.get("enabled"))
            if "pairs" in pw:
                eq("PoE pairs", pw["pairs"], poe.get("pairs"))
            if pw.get("class", "unknown") != "unknown":
                eq("PoE class", pw["class"], poe.get("class"))
            if "allocated" in pw:
                eq("PoE allocated W", float(pw["allocated"]) / 1000, poe.get("alloc_w"))
            if "requested" in pw:
                eq("PoE requested W", float(pw["requested"]) / 1000, poe.get("req_w"))
    an = port.get("auto-negotiation")
    if an and via.startswith("LLDP"):
        mp = nb.get("mac_phy") or {}
        eq("autoneg supported", an["supported"], mp.get("autoneg_supported"))
        eq("autoneg enabled", an["enabled"], mp.get("autoneg_enabled"))
    for v in vlans:
        if v.get("pvid"):
            eq("PVID", int(v["vlan-id"]), nb.get("pvid"))
        if v.get("value") and via.startswith("LLDP"):
            names = {x["id"]: x.get("name") for x in nb.get("vlans", [])}
            eq(f"VLAN {v['vlan-id']} name", v["value"], names.get(int(v["vlan-id"])))
    return bad


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv
    if not args:
        print(__doc__)
        return 2
    path = args[1] if len(args) > 1 else DEFAULT
    if not os.path.exists(path):
        print(f"test_captures: {path} not present, skipping")
        return SKIP

    recs = [json.loads(l) for l in open(path) if l.strip()]
    frames = [(i, f) for i, r in enumerate(recs) for f in r.get("frames", [])]
    out = subprocess.run([args[0]], input="".join(f["hex"] + "\n" for _, f in frames),
                         capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(out) == len(frames), (len(out), len(frames))

    failures, checked = 0, 0
    by_rec = {}
    for (i, f), line in zip(frames, out):
        d = json.loads(line)
        if d["kind"] in ("lldp", "cdp") and d["result"] != "ok":
            print(f"FAIL capture {i}: {d['kind']} frame not decoded: {d['result']} ({f['summary'][:80]})")
            failures += 1
        if d.get("overflow"):
            print(f"FAIL capture {i}: status JSON overflowed ({d['json_bytes']} bytes)")
            failures += 1
        if d.get("status"):
            nb = d["status"]["neighbors"][0]
            key = (nb["proto"], nb["chassis_id"].partition(":")[2].lower(), nb["port_id"].partition(":")[2])
            by_rec.setdefault(i, {})[key] = nb

    for i, r in enumerate(recs):
        if not r.get("frames"):
            if verbose:
                print(f"capture {i}: no raw frames, nothing to compare")
            continue
        for via, name, ch, port, vlans, _ in entries(r):
            proto = "cdp" if via.startswith("CDP") else "lldp"
            nb = by_rec.get(i, {}).get((proto, ch["id"]["value"].lower(), port["id"]["value"]))
            label = f"capture {i} {via} {name or ch['id']['value']} {port['id']['value']}"
            if not nb:
                print(f"note {label}: no frame from it in this capture (lldpd still held it)")
                continue
            checked += 1
            bad = compare(via, name, ch, port, vlans, nb)
            for b in bad:
                print(f"FAIL {label}: {b}")
            failures += len(bad)
            if verbose and not bad:
                print(f"ok   {label}")
    print(f"test_captures: {len(frames)} frames, {checked} neighbors compared, {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
