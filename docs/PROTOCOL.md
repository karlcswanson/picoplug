# picolldp status protocol

The firmware exposes the same status document two ways:

| Transport | Where | Behaviour |
|-----------|-------|-----------|
| NDJSON stream | TCP 192.168.7.1:9000 | One JSON object per line. A status message is sent immediately on connect, then whenever something changes, plus a heartbeat every 2 s. Up to 4 clients. |
| HTTP | `GET http://192.168.7.1/status.json` | One status object, `Content-Type: application/json`, `Cache-Control: no-store`, CORS `*`. |
| HTML | `GET http://192.168.7.1/` | Built-in viewer page that polls `/status.json`. |

The NDJSON stream listens on the USB link only. HTTP also answers on the
port's own DHCP address, read-only: `/`, `/status.json`, `/frame`, the icon
and the manifest; every other path is refused there with 403. That is how
the tester works as a remote probe: leave it on a far port with USB power
and open its address from a laptop on the network.

Clients must ignore unknown fields; new fields will be added without a
version bump. A field is omitted (not `null`) when the switch did not send
the corresponding TLV.

## Status message

```json
{"type":"status","fw":"0.1.0","seq":42,"uptime_s":123,
 "eth":{"link":true,"speed":100,"duplex":"full"},
 "usb":{"connected":true},
 "neighbors":[ ...neighbor objects... ]}
```

| Field | Type | Notes |
|-------|------|-------|
| `type` | string | Always `"status"`. Other types may appear later. |
| `fw` | string | Firmware version. |
| `board` | string | Pico SDK board name, e.g. `wiznet_w55rp20_evb_pico`. |
| `probe` | int | This unit's probe number, 0 to 9 (resistor color code: 0 black, 1 brown, 2 red, 3 orange, 4 yellow, 5 green, 6 blue, 7 violet, 8 grey, 9 white). Omitted when none is set. |
| `seq` | int | Increments per message sent (per transport). |
| `uptime_s` | int | Seconds since boot. |
| `eth.mac` | string | This probe's Ethernet MAC (locally administered, from the flash ID). |
| `eth.link` | bool | W5500 PHY link. |
| `eth.speed` | int | 10 or 100. Only when link is up. |
| `eth.duplex` | string | `"full"` or `"half"`. Only when link is up. |
| `eth.chip_ok` | bool | W5500 answered over SPI and socket 0 is open. |
| `eth.version`, `eth.phycfgr`, `eth.sock_sr` | int | Raw W5500 registers for debugging (VERSIONR should be 4, socket status 66 = MACRAW). |
| `eth.frames`, `eth.resyncs`, `eth.init_attempts`, `eth.spi_hz` | int | Driver counters. |
| `usb.connected` | bool | USB host has the device configured. |
| `port` | object | Ethernet-side DHCP lease and reachability probe, see below. |
| `tx` | object | What the device itself advertises on the Ethernet port (LLDP/CDP transmit), see below. |
| `stp` | object | Spanning-tree BPDUs heard on the port, see below. |
| `neighbors` | array | Zero or more neighbor objects, newest-first is *not* guaranteed. |
| `neighbors_omitted` | int | Neighbors left out because the message would not fit its 8 KB buffer; absent when all are included. The rest of the message is always complete. |

## Port object

What the device learned by acting like a normal host on the switch port.

| Field | Type | Notes |
|-------|------|-------|
| `link` | bool | Ethernet PHY link (same as `eth.link`). |
| `dhcp.state` | string | lwIP DHCP client state: `nolink`, `selecting`, `requesting`, `bound`, `renewing`, ... |
| `dhcp.bound` | bool | A lease is held. The fields below exist only then. |
| `dhcp.ip`, `dhcp.mask`, `dhcp.gateway`, `dhcp.server` | string | Lease details. |
| `dhcp.dns` | [string] | DNS servers from the lease (up to 2). |
| `dhcp.lease_s`, `dhcp.bound_for_s` | int | Lease length and age. |
| `dhcp.offers`, `dhcp.acks`, `dhcp.naks` | int | Replies seen on the wire since the last link-up (`naks` only when non-zero). |
| `dhcp.servers` | [string] | Distinct server identifiers that answered (up to 4). More than one means two DHCP servers on the segment. Absent when none. |
| `dhcp.acquire_ms` | int | DISCOVER to ACK time of the last lease. Absent on a renew. |
| `dhcp.msg` | string | `ack`, or `offer` while an offer is shown before the ACK arrives. The fields below are decoded from that reply and exist only with `msg`. |
| `dhcp.server_mac`, `dhcp.server_id` | string | Ethernet source of the reply and option 54. |
| `dhcp.relay` | string | `giaddr` when the reply came through a DHCP relay. |
| `dhcp.t1_s`, `dhcp.t2_s` | int | Renew and rebind times (options 58, 59), when sent. |
| `dhcp.routers`, `dhcp.dns_all`, `dhcp.ntp`, `dhcp.wins`, `dhcp.tftp_ips` | [string] | Options 3, 6, 42, 44, 150 (up to 3, 3, 3, 2, 2 addresses). Absent when empty. |
| `dhcp.broadcast` | string | Option 28. |
| `dhcp.domain`, `dhcp.search` | string | Options 15 and 119 (search list space separated). |
| `dhcp.tftp`, `dhcp.bootfile` | string | Option 66 / BOOTP `sname`, option 67 / BOOTP `file`. |
| `dhcp.wpad` | string | Option 252 proxy auto-config URL. |
| `dhcp.vendor`, `dhcp.vendor_len` | string, int | Option 43 as hex (first 32 bytes); `vendor_len` is the full length when it was cut. |
| `dhcp.options` | [int] | Every option code in the reply, in order (up to 32). |
| `dhcp.raw` | string | The options exactly as received, as hex: code, length, value, repeated, with pads and the end marker removed; first 224 bytes. `raw_truncated` is set when the reply had more. The page decodes this into the expandable option list. |
| `dhcp.truncated` | bool | Present when a string or list above was cut. |
| `probe.phase` | string | `idle`, `arp`, `ping_gw`, `ping_inet`, `dns`, `done`. |
| `probe.running`, `probe.runs` | bool, int | The probe repeats every 30 s while a lease is held. |
| `probe.age_s` | int | Seconds since the last completed run. Present only after the first run, as are the three objects below. While a later run is in progress they keep the last completed run's values; they are cleared when the lease is lost or a new one arrives. |
| `probe.gateway` | object | `arp` (bool), `mac`, `reachable`, `sent`, `replies`, `rtt_ms` (best of 3 ICMP echoes). |
| `probe.internet` | object | Same for `target` (1.1.1.1 unless changed in settings). |
| `probe.dns` | object | `name`, `ok`, `addr`, `ms`, `server` (resolver the lookup went to), `custom` (true when that resolver came from settings rather than DHCP). |

The device is also reachable as `http://picoplug.local/` on the USB link
(mDNS responder). Nothing listens on the Ethernet side; the probe only
sends DHCP, ARP, ICMP echo, and one DNS query.

## Tx object

The device sends its own LLDP (every 30 s, TTL 120, plus a 4-frame fast
start on link-up or re-enable) and CDPv2 (every 60 s, holdtime 180)
frames, identifying itself as `picoplug` (`picoplug-N` with a probe number set), an LLDP-MED class III endpoint,
so that switches list it and reply with their MED TLVs. Set with `GET /tx`.

LLDP is always sent: bridges never forward it. CDP is sent only while a
CDP neighbor that is not another picoplug is heard, from its first frame
until its holdtime runs out, the same rule lldpd uses. A switch that does
not run CDP floods it like any multicast, so unprompted CDP would reach
testers on other ports and look like their switch.

| Field | Type | Notes |
|-------|------|-------|
| `lldp.enabled`, `cdp.enabled` | bool | Transmit on/off per protocol. |
| `cdp.peer` | bool | A CDP switch is heard, so CDP is actually going out. |
| `lldp.frames`, `cdp.frames` | int | Frames sent since boot. |
| `lldp.age_s`, `cdp.age_s` | int | Seconds since the last frame; absent if none sent yet. |

## Stp object

Spanning-tree BPDUs the port receives (802.1D STP, 802.1w RSTP, the CIST
part of 802.1s MSTP, and Cisco PVST+). Counters reset on link-up. The
device never sends BPDUs.

| Field | Type | Notes |
|-------|------|-------|
| `seen` | bool | At least one BPDU since link-up. Everything else below `link_up_for_s` exists only when true. |
| `link_up_for_s` | int | Seconds since the PHY link came up; absent when down. A port that has been up for 10 s or more with `seen` false is an edge port (BPDU filter / portfast with filtering) or has STP off. |
| `count`, `tc_count`, `root_changes` | int | BPDUs received, topology-change flags (plus TCN BPDUs), and root bridge changes between BPDUs (last two only when non-zero). |
| `interval_ms` | int | Time between the last two BPDUs (the hello time as actually sent). |
| `last_s`, `first_s` | int | Seconds since the last / first BPDU. |
| `proto` | string | `stp`, `rstp`, `mstp`, `unknown` (protocol version of the last BPDU). |
| `type` | string | `config`, `rst`, `tcn`. |
| `pvst`, `vlan`, `tagged` | bool, int, bool | Cisco PVST+ BPDU and its VLAN (from the 802.1Q tag or the PVST+ trailer); present only when applicable. |
| `src_mac` | string | Ethernet source of the last BPDU. |
| `flags` | int | Raw flags byte. |
| `role` | string | RSTP/MSTP port role bits: `designated`, `root`, `alternate`, `unknown`. Absent for 802.1D. |
| `state` | string | `forwarding`, `learning`, `discarding`; `unknown` for 802.1D which does not carry it. |
| `tc`, `proposal`, `agreement`, `tc_ack` | bool | Flag bits, present only when set. |
| `root` | object | `priority` (16-bit field: priority plus system ID extension), `mac`, `cost` (root path cost from the neighbor), `is_neighbor` (the sending bridge is the root). |
| `bridge` | object | `priority`, `mac` of the sending bridge. |
| `port_id`, `port_priority`, `port_number` | int | The neighbor's port identifier, split. The number is the switch's internal index, not necessarily the label on the front. |
| `msg_age_s`, `max_age_s`, `hello_s`, `fwd_delay_s` | number, int, int, int | Timers from the BPDU. |
| `mst` | object | MSTP only: `region`, `revision`, `regional_root` {`priority`,`mac`,`is_neighbor`}, `internal_cost`, `hops` (remaining), `instances` (MSTI records). For MSTP `bridge` is the sender's CIST bridge ID and `root` the CIST root; the body's bridge field (the regional root) is reported here. |

Fields apply to the last config/RST BPDU; a TCN increments `count` and `tc_count` without replacing them.

## Tool object

The on-demand ping / traceroute started from `GET /tool` (below). Always
present; `kind` is `idle` until a tool has run, and the last result stays
until the next run or reboot. One tool at a time; it needs the port to
have a DHCP address. Replies are matched by ICMP id/sequence, so the
periodic port probe keeps running alongside.

| Field | Type | Notes |
|-------|------|-------|
| `kind` | string | `idle`, `ping`, `trace`. Everything below only when not idle. |
| `running` | bool | Still sending. Poll `/status.json` until false. |
| `host`, `addr` | string | What was asked for and what it resolved to (`addr` absent until DNS answers). |
| `error` | string | Present when the run failed or stopped early: DNS failure, `stopped`, `no answer within 20 hops`. |
| `age_s` | int | Seconds since start (running) or since finish (done). |
| `generation` | int | Bumps on every change; the page uses it to skip redraws. |
| `count`, `sent`, `recv` | int | Ping only: requested (0 = continuous), sent, answered. While running the newest probe may still be in flight; the page subtracts it from the loss figure. |
| `min_ms`, `avg_ms`, `max_ms`, `last_ms` | int | Ping only, when `recv` > 0. |
| `rtts`, `first_seq` | array, int | Ping only: the last 60 probes in order, ms or `null` for no reply; `first_seq` is the 1-based number of the first entry (so a long continuous run shows a sliding window). 1 s cadence, 1 s timeout. |
| `hops` | array | Trace only: `{"ttl":1,"addr":"192.168.109.1","ms":1}` for an answer, `{"ttl":3,"timeout":true}` for none; the answering hop carries `"reached":true` (echo reply) or `"unreachable":true` (destination unreachable). TTL 1..20, 1.5 s per hop, one probe per hop. |
| `reached` | bool | Trace only: the target answered. |

## Lacp object

LACPDUs (IEEE 802.1AX, slow protocols EtherType 0x8809 subtype 1, to
01:80:c2:00:00:02) the port has sent since link-up. A switch only sends
them on a member of an LACP port channel. The tester never answers, so the
switch sees no partner: most switches then hold the port out of the bundle
and pass only link-local frames (LLDP, LACP), which shows up as DHCP
getting no offer. Some run it as an individual port instead. The actor's
collecting and distributing bits say which. Always present; only `seen`
when nothing has arrived.

| Field | Type | Notes |
|-------|------|-------|
| `seen` | bool | Any LACPDU since link-up. |
| `count`, `first_s`, `last_s` | int | LACPDUs counted, seconds since the first and the last. |
| `interval_ms` | int | Time between the last two. The switch transmits at the rate its *partner* asked for, so this, not `actor.fast`, is the real rate (1 s or 30 s). |
| `changes` | int | Present when actor or partner info changed after the first PDU. |
| `src_mac`, `version` | string, int | Sender MAC and LACP version. |
| `actor` | object | The switch port: `system_priority`, `system` (MAC), `key` (the port channel's operational key), `port_priority`, `port`, `state` (raw byte) and the decoded bits `active`, `fast`, `aggregatable`, `sync`, `collecting`, `distributing`, `defaulted`, `expired`. |
| `partner` | object | Same shape: what the switch believes about the tester. With no LACP from our side the system MAC is all zeros and `defaulted` is set on the actor. |

The page shows a "Port channel" row: "held out of the bundle" unless
`actor.collecting` and `actor.distributing` are both set, and the DHCP
row then blames the port channel instead of a missing server.

## Dot1x object

Passive 802.1X detection: EAPOL frames (EtherType 0x888E) the port has
sent since link-up. A port with port authentication sends EAP
Request/Identity on link-up and again whenever it hears an
unauthenticated client, so our LLDP/DHCP frames trigger it. The device
only listens: it sends no EAPOL-Start and never answers. Reset on
link-up and link-down. Always present.

| Field | Type | Notes |
|-------|------|-------|
| `seen` | bool | At least one EAP packet from the port. Everything below only when true. |
| `requests` | int | EAP Requests received (Identity or a method). |
| `failures` | int | EAP Failure packets (the port gave up on us); present when non-zero. |
| `first_s`, `last_s` | int | Seconds since the first EAP packet and since the last request. |
| `interval_ms` | int | Between the last two requests (the switch's tx-period, typically 30 s). |
| `authenticator` | string | Source MAC of the last request (the switch). |
| `version` | int | EAPOL protocol version (1, 2 or 3). |
| `eap` | string | Method of the last request: `identity`, `peap`, `tls`, `ttls`, `mschapv2`, `md5`, ... |
| `generation` | int | Bumps on every counted packet. |

EAPOL-Start, Logoff and EAP Responses are parsed but not counted: on a
shared segment they come from other clients, not from the port.

## Igmp object

IGMP seen on the port since link-up. The querier is what keeps multicast
(Dante, AES67, SMPTE 2110, IPTV) flowing on a VLAN with IGMP snooping;
without one, snooping switches either flood every group to every port
or time the groups out. Passive: the device itself only joins
224.0.1.129 (for PTP) and answers queries like any host. Always present.

| Field | Type | Notes |
|-------|------|-------|
| `seen` | bool | A general query has been received. Everything below except `reports` only when true. |
| `reports` | int | Membership reports heard from anyone (other hosts joining groups); present when non-zero. |
| `querier`, `querier_mac` | string | Source of the last general query. |
| `version` | int | 1, 2 or 3 (from the query format). |
| `queries`, `group_queries` | int | General and group-specific queries. |
| `querier_changes`, `prev_querier` | int, string | General queries from a different IP than the previous one, and the IP before the last change; present when non-zero. Two queriers at startup is normal, a growing count is not. |
| `qrv`, `s_flag` | int, bool | IGMPv3 only: the querier's robustness variable and the suppress-router-side-processing flag. |
| `interval_s` | int | Measured time between the last two general queries (usually 125). |
| `qqi_s` | int | The querier's query interval as advertised in an IGMPv3 query. |
| `max_resp_ds` | int | Max response time in tenths of a second. |
| `first_s`, `last_s` | int | Seconds since the first and last general query. |
| `mrouter` | object | A multicast router heard via PIMv2 Hello on 224.0.0.13: `ip`, `mac`, `hellos`, `holdtime_s`, `dr_priority`, `last_s`. Present whenever one was heard, even with no querier. |
| `generation` | int | Bumps on every general query and on a changed PIM router. |

A querier of `0.0.0.0` is a snooping querier on a switch that has no
address on the VLAN (a common and valid setup). The page labels the
querier when it can match it: the LLDP neighbor's management address or
chassis MAC ("this switch"), the DHCP gateway, the DHCP server MAC, the
PIM router, or a MAC within 16 of the STP root bridge ID ("probably the
root bridge switch"). Membership reports from other hosts reaching
this port mean the switch is not intercepting them, so snooping is
probably off on it; the page says so.

## Ptp object

IEEE 1588 PTP seen on the port since link-up. v1 (IEEE 1588-2002) and
v2 (IEEE 1588-2008 and later) are tracked separately because a network
can carry both at once with different masters. Fields report what is in
the messages; the page does not name the system or profile behind them.
The device joins 224.0.1.129
so a snooping switch forwards PTP to it; layer-2 PTP (EtherType 0x88F7)
needs no join. Presence and identity only: the hardware cannot
timestamp, so no offset or jitter figures. Always present.

| Field | Type | Notes |
|-------|------|-------|
| `seen` | bool | Either version seen. |
| `v1`, `v2` | object | One per version seen, fields below. |
| `transport` | string | `udp` (319/320) or `l2` (0x88F7). |
| `domain` | int | v2 domainNumber of the first message seen; 0 for v1. |
| `subdomain` | string | v1 only: the PTP subdomain name as sent (`_DFLT` is the v1 default). |
| `other_domains` | int | Messages in a different domain than `domain`, present when non-zero (a second clock domain on the VLAN). |
| `syncs`, `announces`, `follow_ups`, `delay_reqs` | int | Message counts (the last two only when non-zero). `delay_reqs` are slaves talking, so they show a populated port. |
| `master`, `master_ip`, `master_mac` | string | The clock sending Sync: v2 clock identity (8 bytes) or v1 UUID (6 bytes), and its addresses. With a boundary clock this differs from the grandmaster. |
| `two_step` | bool | Sync carries the two-step flag (Follow_Up carries the timestamp). |
| `sync_interval_ms`, `log_sync_interval` | int | Measured time between the last two Syncs, and the advertised log2 interval (v2; -3 = 8/s). |
| `first_s`, `last_sync_s` | int | Seconds since the first message and the last Sync. |
| `grandmaster` | object | v2 only, from Announce: `id`, `priority1`, `priority2`, `clock_class` (6 = locked to a primary reference such as GPS, 248 = default free-running), `clock_accuracy`, `time_source` (`gps`, `ptp`, `ntp`, `oscillator`, ...), `steps_removed` (0 = the master is the grandmaster), `utc_offset`, `log_announce_interval`, `changes` (grandmaster identity changed, present when non-zero), `last_s`. |
| `generation` | int | Bumps on first sight, a new master, and Announce field changes. |

## Blink object

The port blinker (`GET /tool?blink=1`): the W5500 PHY is powered down
for 1.5 s and back up for 2.5 s, eight times (32 s), so the link LED of
the far-end switch port blinks in a pattern no idle port shows. Link
tracking is frozen while it runs, so the neighbor table, STP counters
and the DHCP lease survive; transmits are skipped while the PHY is down
(the periodic probe and advertisements simply miss a turn). Always present.

| Field | Type | Notes |
|-------|------|-------|
| `active` | bool | Blinking, or waiting for the link to come back after the last cycle. |
| `remaining_s` | int | Seconds of blinking left; 0 while settling. |
| `cycles`, `link_ups` | int | Down/up cycles completed and how many of them saw the link come back (a switch that needs longer than 2.5 s to negotiate shows fewer). |
| `pulse` | bool | Present and true while the PHY is down once for another probe's path test (see Path object), not a user blink. |
| `generation` | int | Bumps on start, each cycle and stop. |

## Peers object

Other picolldp probes heard on the port. Every probe with a link
broadcasts a HELLO every 5 s on EtherType 0x88B5 (IEEE local
experimental), so probes find each other on a bare cable with no IP, or
anywhere on a shared VLAN. A peer not heard for 16 s is dropped; the
list is cleared on link-down. Always present.

| Field | Type | Notes |
|-------|------|-------|
| `count` | int | Probes in the table (up to 8). |
| `other_macs` | int | Distinct unicast source MACs heard since link-up that are not probes. 0 on a bare probe-to-probe cable; a switch port nearly always shows some (the switch's own LLDP/STP source, other hosts' broadcasts). |
| `other_macs_capped` | bool | Present when more than 32 were heard. |
| `list[]` | array | Up to 4 peers: `mac`, `age_s`, `uptime_s`, `speed`, `full_duplex`, and from the peer's own LLDP neighbor `chassis`, `port`, `pvid` (each omitted when the peer has none), `fw`, `probe` (its probe number, omitted when it has none or runs firmware before 0.3.1), and once it has answered an echo `rtt_ms` / `rtt_min_ms` (layer-2 round trip, ms resolution). `sees_us` is true when the peer's LLDP neighbor is this probe, which happens on a direct cable with advertising on. |
| `generation` | int | Bumps when a peer appears, leaves, or its switch/port/PVID/speed changes. |

## Path object

The two-probe path test (`GET /tool?path=1[&peer=MAC]`): is the run
between this probe and a peer a direct cable or a switched path? This
probe asks the peer to power its PHY down once for 3 s, then watches its
own link and sends the peer an echo every 200 ms. If this link drops in
step the run is direct (or passes link state through: a coupler, a
passive injector, a media converter with link pass-through). If this
link stays up while the peer goes silent, a switch is in between, and
the time until the peer answers again is reported (about 30 s means the
peer's switch port runs full spanning tree rather than an edge /
portfast port). Link tracking on both probes is frozen during the test,
as for the blinker. Always present.

| Field | Type | Notes |
|-------|------|-------|
| `phase` | string | `idle`, `asking` (request sent, up to 6 tries 300 ms apart), `watching` (peer off, 4.5 s), `recovering` (verdict known, waiting up to 45 s from the ACK for the peer or this link to come back), `done`. |
| `peer` | string | MAC under test. Omitted when idle. |
| `verdict` | string | `direct`, `switched`, `switched-peer-lost` (link stayed up but the peer never answered again), `no-answer` (no ACK), `inconclusive` (the peer kept answering). Present from `recovering` on. |
| `acked` | bool | The peer confirmed the request. |
| `own_link_dropped`, `own_down_ms` | bool, int | This probe's link went down during the test, and for how long in total. |
| `peer_went_silent`, `peer_came_back`, `peer_back_ms` | bool, bool, int | No echo reply for 1 s inside the off window; a reply after that; ms from the ACK to that reply. |
| `elapsed_ms`, `age_s` | int | Test duration so far (or total once done); seconds since it finished. |
| `generation` | int | Bumps on each phase or field change. |

Frame format, after the Ethernet header: `PCLP`, version 1, type, seq
(2 bytes), then per type: HELLO (1) uptime_s (4), speed (1), full (1),
pvid (2), then chassis, port and firmware as length-prefixed strings,
then the probe number plus one (1; 0 = none, absent before 0.3.1);
ECHO_REQ (2) / ECHO_REP (3) the sender's ms clock (4); BLINK_REQ (4)
token (2) and off_ms (2); BLINK_ACK (5) token (2). Big-endian, padded
to 60 bytes. A probe answers a given token once and refuses another
request for 10 s after a drop, so a retried request never drops the
link twice.

## Neighbor object

Every field the LLDP parser decoded. Absent means the TLV was not present.

| Field | Type | Source TLV |
|-------|------|------------|
| `proto` | string | `"lldp"` or `"cdp"`. CDP neighbors fill the same fields where they map: device ID becomes `chassis_id` (`local:...`), port ID becomes `port_id` (`ifname:...`), native VLAN becomes `pvid`, the VoIP VLAN becomes `voice_vlan`, software version becomes `sys_desc`. |
| `picoplug` | bool | Present (true) when the neighbor is another picoplug (system description or CDP platform starting with `picoplug`, or `picolldp` from older firmware). Its frames came through a switch that passed them along, so it is not this port's switch. |
| `cdp_version`, `cdp_caps`, `checksum_ok` | int, [string], bool | CDP only. `cdp_caps` are the raw CDP capability names (`router switch host phone igmp ...`); `checksum_ok` appears only when false. |
| `platform`, `vtp_domain`, `duplex`, `mtu` | string, string, string, int | CDP Platform, VTP domain, Duplex (`full`/`half`), MTU. |
| `cdp_power` | {`consumption_w`,`requested_w`,`available_w`} | CDP Power, Power Request and Power Available TLVs in watts. |
| `chassis_id` | string | Chassis ID, formatted `<kind>:<value>`; kinds: `mac`, `ifname`, `ifalias`, `local`, `net`, `chassis`, `portcomp`, `unknown`. Non-printable values are rendered as `0x…` hex. |
| `port_id` | string | Port ID, same format; kinds add `circuit`. |
| `src_mac` | string | Ethernet source MAC of the LLDP frame. |
| `frame_vlan` | int | Only if the frame carried an 802.1Q tag. |
| `port_desc` | string | Port Description (4) |
| `sys_name` | string | System Name (5) |
| `sys_desc` | string | System Description (6), cut at 255 bytes (`truncated` set). |
| `caps` | [string] | Enabled System Capabilities (7): `other repeater bridge wlan_ap router telephone docsis station cvlan svlan tpmr`. |
| `caps_supported` | [string] | Supported capabilities. |
| `mgmt_addr` | [string] | Management Address (8): IPv4 dotted, IPv6 RFC 5952, or `afiN:hex`. Up to 4. |
| `pvid` | int | 802.1 Port VLAN ID. |
| `mgmt_vid` | int | 802.1 Management VID. |
| `ppvids` | [{`id`,`supported`,`enabled`}] | 802.1 Port and Protocol VLAN IDs, up to 4. |
| `protocols` | string | 802.1 Protocol Identity TLVs, named and comma-joined (`STP`/`RSTP`/`MSTP`, `LACP`, `802.1X`, `LLDP`, `CDP`, `PVST+`, …); others as `0x` EtherType or `LLC 0x` SAP. Repeats are listed once. |
| `vlans` | [{`id`,`name`}] | 802.1 VLAN Name TLVs, up to 32. |
| `voice_vlan` | {`id`,`tagged`,`priority`,`dscp`,`unknown_policy?`} | LLDP-MED Network Policy with application type *voice*. |
| `med_policies` | [{`app`,`id`,`tagged`,`priority`,`dscp`}] | All MED network policies (up to 4). `app` is `voice`, `voice_signaling`, `guest_voice`, …. |
| `poe` | object | 802.3 Power via MDI. `port_class`: `pse`/`pd`; `supported`, `enabled`: bool; `pairs`: `signal`/`spare`; `class`: 0–4; with 802.3at extension: `req_w`, `alloc_w` (decimal watts), `priority` (`critical`/`high`/`low`/`unknown`), `type` (0 Type 2 PSE, 1 Type 2 PD, 2 Type 1 PSE, 3 Type 1 PD), `pd_4pid` (true when the PD supports power on both pairs). With the 802.3bt extension, `bt`: `class` (1–8, or `"dual"`), `class_a`/`class_b` (dual-signature, 1–5), `type` (3 or 4), `device` (`pse`/`pd`), `powering` (`2-pair`/`4-pair single`/`4-pair dual`), `pd` (`single`/`2-pair dual`/`4-pair dual`), `pairs` (`a`/`b`/`both`), `req_a_w`, `req_b_w`, `alloc_a_w`, `alloc_b_w`, `max_w`, `pd_load`; each only when sent non-zero. |
| `upoe` | {`supported`,`altb_detect`,`spare_desired`,`spare_on`} | Cisco 4-pair PoE TLV (OUI 00-01-42 subtype 1). |
| `med_power` | {`source`,`w`,`priority`} | LLDP-MED Extended Power via MDI. |
| `mac_phy` | {`autoneg_supported`,`autoneg_enabled`,`pmd_adv`,`mau_type`} | 802.3 MAC/PHY. `mau_type` per RFC 4836 (16 = 100BASE-TX FD, 30 = 1000BASE-T FD). |
| `max_frame` | int | 802.3 Maximum Frame Size. |
| `eee` | {`tx_us`,`rx_us`} | 802.3 Energy-Efficient Ethernet wake times, µs. |
| `link_agg` | {`capable`,`active`,`port_id`} | Link Aggregation, from the 802.1 TLV (subtype 7) or the older 802.3 one (subtype 3). `port_id` only when the port is in an aggregate. |
| `inventory` | {`serial`,`model`,`manufacturer`,`hw`,`fw`,`sw`,`asset`} | LLDP-MED inventory TLVs (subtypes 5-11), or a vendor TLV carrying the same: FortiSwitch serial (OUI 08-5b-0e subtype 2). Only the fields sent. |
| `med` | {`caps`,`device_type`} | LLDP-MED Capabilities. |
| `location` | {`format`,`text`} | LLDP-MED Location. `format`: `coordinates` (text `lat, long[, alt m]`), `civic` (address elements in the order sent, then country) or `elin`. |
| `mud_url` | string | IANA MUD URL (RFC 8520, OUI 00-00-5e subtype 1). |
| `unknown_tlvs` | int | TLVs the parser did not decode (vendor OUIs, MED inventory, …). |
| `truncated` | bool | Some field was cut to fit a fixed buffer. |
| `ttl` | int | TTL from the last frame. |
| `ttl_remaining_s` | int | Seconds until this entry expires. |
| `last_seen_s` | int | Seconds since the last frame from this neighbor. |
| `first_seen_s` | int | Seconds since the neighbor first appeared. |

Strings are ASCII; any byte outside 0x20–0x7E is escaped as `\u00XX`.

## Example

```json
{"type":"status","fw":"0.1.0","seq":7,"uptime_s":95,"eth":{"link":true,"speed":100,"duplex":"full"},"usb":{"connected":true},"neighbors":[{"chassis_id":"mac:00:1e:14:ab:cd:80","port_id":"ifname:Gi1/0/12","src_mac":"00:1e:14:ab:cd:8c","port_desc":"GigabitEthernet1/0/12","sys_name":"sw-core-2","sys_desc":"Cisco IOS Software, C2960X Software ...","caps":["bridge"],"caps_supported":["bridge","router"],"mgmt_addr":["10.0.0.2"],"pvid":20,"voice_vlan":{"id":30,"tagged":true,"priority":5,"dscp":46},"med_policies":[{"app":"voice","id":30,"tagged":true,"priority":5,"dscp":46},{"app":"voice_signaling","id":30,"tagged":true,"priority":5,"dscp":46}],"poe":{"port_class":"pse","supported":true,"enabled":true,"pairs":"signal","class":3,"req_w":13.0,"alloc_w":15.4,"priority":"low","type":0},"med_power":{"source":"pse","w":15.4,"priority":"low"},"mac_phy":{"autoneg_supported":true,"autoneg_enabled":true,"pmd_adv":27649,"mau_type":30},"med":{"caps":15,"device_type":4},"unknown_tlvs":1,"truncated":true,"ttl":120,"ttl_remaining_s":97,"last_seen_s":23,"first_seen_s":83}]}
```

## Debug endpoints

| Endpoint | Effect |
|----------|--------|
| `POST /inject` (body: hex dump of one Ethernet frame, whitespace ignored) | Handles the frame exactly as if the W5500 had received it. Used to replay `firmware/test/fixtures/*.hex` on the device. |
| `GET /frame?lldp`, `?cdp`, `?bpdu`, `?lacp` | The last frame of that kind as a hex dump (16 bytes per line), 404 if none yet. For looking at undecoded TLVs from a phone, or feeding `text2pcap`/`tcpdump -r`. |
| `GET /icon.png`, `/apple-touch-icon.png`, `/favicon.ico` | The 192 px home-screen icon (PNG, 1 KB). |
| `GET /manifest.json` | Web-app manifest (`display: standalone`). |
| `GET /reboot` | Restart the firmware. |
| `GET /reboot?bootsel=1` | Restart into the USB bootloader (RPI-RP2 volume). |
| `GET /tx` | Read the transmit state: `{"ok":true,"lldp":true,"cdp":true,"cdp_peer":false,"lldp_frames":9,"cdp_frames":0}`. |
| `GET /tx?lldp=0|1&cdp=0|1` | Enable/disable each protocol. Disabling sends a shutdown frame (TTL 0); re-enabling starts a fast-start burst. The choice is saved to flash and used at the next boot. |
| `GET /tool` | Tool state: `{"ok":true,"error":"","kind":"idle","running":false,"blink":false}`. |
| `GET /tool?ping=HOST[&count=N]`, `GET /tool?trace=HOST`, `GET /tool?stop=1` | Start a ping (N 1..60, default 5, 0 = continuous until `stop=1` or 15 minutes) or a traceroute to an IPv4 literal or DNS name, or stop the running one. Same reply shape; status 400 and `error` when it cannot start (`the port has no address yet (DHCP checking)`, `a tool is already running; stop it first`, bad host or count). Results arrive in the `tool` object of `/status.json`. |
| `GET /tool?dhcp=1` | Drop the Ethernet-side lease and start DHCP again from DISCOVER, then rerun the port checks (the page's Retry DHCP). No-op without a link. |
| `GET /tool?relisten=1` | Clear the neighbor table so the page waits for the next LLDP/CDP advertisement (the page's Clear and listen again). |
| `GET /tool?path=1[&peer=MAC]` | Start the two-probe path test against `peer`, or against the only probe heard when `peer` is omitted. 400 with `No other probe heard on this port`, `Several probes heard; pick one`, `That probe is not in the peer list`, `No Ethernet link`, `The port blinker is running` or `A path test is already running`. Progress in the `path` object. The blinker refuses to start while a path test runs. |
| `GET /tool?blink=1`, `GET /tool?blink=0` | Start the port blinker (32 s, stops itself) or stop it early. Independent of ping/trace; a ping running during a blink just loses probes. Progress in the `blink` object. 400 `Ethernet chip not ready` when the W5500 is absent. |
| `GET /settings` | Current settings and the defaults: `{"ok":true,"error":"","saved":false,"ping_target":"1.1.1.1","dns_name":"clockclub.net","dns_server":"","lldp":true,"cdp":true,"probe":null,"usb_ip":"192.168.7.1","form_url":"https://...","defaults":{...}}`. |
| `GET /settings?ping_target=A.B.C.D&dns_name=NAME&dns_server=A.B.C.D&usb_ip=A.B.C.D` | Validate, save to flash and rerun the port probe. Any subset of keys; values URL-encoded; `dns_server=` (empty) means use the DHCP-supplied resolver; `usb_ip` is the device's own address on the USB link (/24, the phone gets the next host address; applies at the next boot, the reply carries `running_ip` and `reboot_needed`); `lldp=`/`cdp=` turn advertising on or off now and at boot (the Settings card's two checkboxes); `probe=0`..`9` sets the probe number (`probe=` clears it), which changes the advertised LLDP system name / CDP device ID from `picoplug` to `picoplug-N` and sends a fresh burst; `form_url=` sets the Form link template (an `http://` or `https://` link up to 255 characters, no spaces, quotes or backslashes; empty = no button; default is the MDN URL-parameter echo page); `reset=1` restores defaults but keeps the probe number and the form link. Replies with the same object; on a bad value nothing is saved, status 400 and `error` names the field. The flash write holds interrupts for about 50 ms. |

## Quick checks

```
nc 192.168.7.1 9000                 # stream
curl -s http://192.168.7.1/status.json | python3 -m json.tool
```

## Form link

The Setup tab's Form card holds a URL template kept in flash (`form_url`).
When it is set, the Switch card on the Port tab shows a Form button that opens the
link in a new tab. The page replaces each `{name}` with the URL-encoded
value from the neighbor shown on the Port tab and the lease. Unknown
names are left as typed; a value the port does not have becomes empty.

| Fill-in | Value |
| --- | --- |
| `{switch}` | Neighbor system name, or its chassis ID |
| `{switch_ip}` | Neighbor's first IPv4 management address |
| `{port}` | Neighbor port as shown on the Port tab |
| `{port_desc}` | Neighbor port description |
| `{vlan}` | PVID |
| `{voice_vlan}` | Voice VLAN ID |
| `{ip}`, `{gateway}` | This tester's DHCP address and gateway, when bound |
| `{probe}` | This tester's probe number |

Example for a form that prefills from its query string:
`https://forms.example.net/noc/?requestType=Switch&switchIp={switch_ip}&port={port}`.
