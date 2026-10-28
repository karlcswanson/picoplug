#!/usr/bin/env python3
"""Run test_json, parse every output line as JSON, check the schema basics."""
import json
import subprocess
import sys

out = subprocess.run([sys.argv[1]], capture_output=True, text=True)
if out.returncode != 0:
    sys.stderr.write(out.stderr)
    sys.exit(1)
sys.stderr.write(out.stderr)

lines = [l for l in out.stdout.splitlines() if l.strip()]
assert len(lines) == 7, f"expected 7 messages, got {len(lines)}"
expect_names = [None, "sw-core-2", "aruba-2930f-01", "ex2300-lab", "USW-24-PoE", "host.example.com"]

for i, line in enumerate(lines):
    msg = json.loads(line)          # raises on invalid JSON
    assert msg["type"] == "status"
    assert msg["fw"] == "test"
    assert isinstance(msg["uptime_s"], int)
    assert set(msg["eth"]) >= {"link"}
    assert isinstance(msg["neighbors"], list)
    if i == 0:
        assert msg["neighbors"] == []
        assert {k: msg["eth"][k] for k in ("link", "speed", "duplex")} == {"link": True, "speed": 100, "duplex": "full"}
    elif i < 6:
        nb = msg["neighbors"]
        assert len(nb) == 1
        assert nb[0]["sys_name"] == expect_names[i], nb[0]["sys_name"]
        assert nb[0]["ttl"] == 120 and nb[0]["ttl_remaining_s"] == 110
        assert nb[0]["last_seen_s"] == 10
        if expect_names[i] == "sw-core-2":
            assert nb[0]["port_id"] == "ifname:Gi1/0/12"
            assert nb[0]["poe"]["alloc_w"] == 15.4 and nb[0]["poe"]["class"] == 3
            assert nb[0]["voice_vlan"] == {"id": 30, "tagged": True, "priority": 5, "dscp": 46}
            assert nb[0]["caps"] == ["bridge"] and nb[0]["truncated"] is True
            assert "\n" in nb[0]["sys_desc"]
        if expect_names[i] == "aruba-2930f-01":
            assert nb[0]["mgmt_addr"] == ["192.168.1.5", "fe80::96f1:28ff:fe12:3440"]
            assert [v["name"] for v in nb[0]["vlans"]] == ["DEFAULT_VLAN", "USERS", "VOICE"]
        if expect_names[i] == "host.example.com":
            assert "poe" not in nb[0] and "pvid" not in nb[0]
            assert "unknown_tlvs" not in nb[0]
            assert nb[0]["inventory"]["serial"] and nb[0]["link_agg"]["capable"] is True
    else:
        assert msg["eth"]["link"] is False and "speed" not in msg["eth"]
        assert len(msg["neighbors"]) == 4   # NEIGHBOR_MAX
print(f"check_json: {len(lines)} messages valid")
