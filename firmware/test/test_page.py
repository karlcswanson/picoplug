#!/usr/bin/env python3
"""
Smoke test for the viewer page (web/dist/index.html, from npm run build): serve it from web_mock.py with
fixtures/page_status.ndjson, drive headless Chrome over the DevTools
protocol, and check that the page renders each case without JavaScript
errors and without letting switch- or server-supplied strings become HTML.

    python3 firmware/test/test_page.py            # exit 0 pass, 1 fail, 77 skipped (no Chrome)

Needs Google Chrome or Chromium on the machine (CHROME env var overrides
the lookup). Registered in CTest as page.smoke; skipped when no browser.
"""
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "fixtures", "page_status.ndjson")
SKIP = 77


def find_chrome():
    c = os.environ.get("CHROME")
    if c and os.path.exists(c):
        return c
    for cand in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                 "/Applications/Chromium.app/Contents/MacOS/Chromium",
                 "google-chrome", "google-chrome-stable", "chromium", "chromium-browser"):
        p = cand if os.path.isabs(cand) else shutil.which(cand)
        if p and os.path.exists(p):
            return p
    return None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class CDP:
    """Just enough of the DevTools WebSocket protocol for Runtime.evaluate."""

    def __init__(self, ws_url):
        host, path = ws_url.split("//")[1].split("/", 1)
        h, p = host.split(":")
        self.s = socket.create_connection((h, int(p)))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.send(f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        self.buf = buf.split(b"\r\n\r\n", 1)[1]
        self.mid = 0
        self.events = []

    def _send(self, payload):
        d = payload.encode()
        mask = os.urandom(4)
        n = len(d)
        hdr = b"\x81" + (bytes([0x80 | n]) if n < 126 else (b"\xfe" + struct.pack(">H", n) if n < 65536 else b"\xff" + struct.pack(">Q", n)))
        self.s.send(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(d)))

    def _recv(self):
        while True:
            while len(self.buf) < 2:
                self.buf += self.s.recv(65536)
            n = self.buf[1] & 0x7F
            o = 2
            if n == 126:
                while len(self.buf) < 4:
                    self.buf += self.s.recv(65536)
                n = struct.unpack(">H", self.buf[2:4])[0]
                o = 4
            elif n == 127:
                while len(self.buf) < 10:
                    self.buf += self.s.recv(65536)
                n = struct.unpack(">Q", self.buf[2:10])[0]
                o = 10
            while len(self.buf) < o + n:
                self.buf += self.s.recv(65536)
            msg = self.buf[o:o + n]
            self.buf = self.buf[o + n:]
            return json.loads(msg)

    def call(self, method, **params):
        self.mid += 1
        self._send(json.dumps({"id": self.mid, "method": method, "params": params}))
        while True:
            m = self._recv()
            if m.get("id") == self.mid:
                return m.get("result", m)
            self.events.append(m)

    def js(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, returnByValue=True, awaitPromise=True)
        if "exceptionDetails" in r:
            raise RuntimeError(r["exceptionDetails"].get("text", "evaluate failed") + ": " + expr[:80])
        return r.get("result", {}).get("value")

    def exceptions(self):
        return [e for e in self.events if e.get("method") == "Runtime.exceptionThrown"]


def main():
    chrome = find_chrome()
    if not chrome:
        print("test_page: no Chrome/Chromium found, skipping")
        return SKIP
    http_port, cdp_port = free_port(), free_port()
    mock = subprocess.Popen([sys.executable, os.path.join(HERE, "web_mock.py"), "--port", str(http_port),
                             "--ndjson", FIXTURE, "--index", "0"], stdout=subprocess.DEVNULL)
    prof = tempfile.mkdtemp(prefix="picolldp-page-")
    browser = subprocess.Popen([chrome, "--headless=new", "--user-data-dir=" + prof, "--no-first-run",
                                "--hide-scrollbars", "--window-size=480,1400",
                                f"--remote-debugging-port={cdp_port}", "about:blank"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = []

    def check(cond, what):
        if not cond:
            failures.append(what)
            print("FAIL:", what)

    try:
        tabs = None
        for _ in range(100):
            try:
                tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{cdp_port}/json"))
                break
            except Exception:
                time.sleep(0.1)
        if not tabs:
            print("test_page: Chrome did not start")
            return SKIP
        cdp = CDP([t for t in tabs if t["type"] == "page"][0]["webSocketDebuggerUrl"])
        cdp.call("Runtime.enable")
        cdp.call("Page.enable")
        base = f"http://127.0.0.1:{http_port}"

        def pin(i):
            urllib.request.urlopen(f"{base}/__mock/index?{i}").read()

        def load(i, settle=2.5):
            pin(i)
            cdp.call("Page.navigate", url=base + "/")
            time.sleep(settle)

        # the clipboard records what a tap copies (127.0.0.1 is a secure context, so the page uses it)
        cdp.call("Page.addScriptToEvaluateOnNewDocument", source="Object.defineProperty(navigator,'clipboard',{value:{writeText:t=>{window.__copied=t;return Promise.resolve()}}})")

        def view(h, settle=0.6):
            cdp.js(f"location.hash='#{h}'")
            time.sleep(settle)

        def hstat():
            return cdp.js("document.querySelector('.hstat').textContent.trim()")

        def click_text(sel, label):
            return cdp.js("(()=>{const b=[...document.querySelectorAll(%s)].find(e=>e.textContent.trim()===%s);if(!b)return false;b.click();return true})()" % (json.dumps(sel), json.dumps(label)))

        def tap(label):
            return cdp.js("(()=>{const r=[...document.querySelectorAll('.kv.copy')].find(e=>e.querySelector('.k').textContent.trim()==='%s');if(!r)return 'NO ROW';window.__copied=null;r.querySelector('.v').click();return window.__copied})()" % label)

        # --- case 0: a normal status: neighbor, bound port, finished ping
        load(0)
        text = cdp.js("document.body.innerText")
        check("mgi6" in text and "Switch" in text, "neighbor card shows the switch and port")
        check("192.168.109.101" in text, "port card shows the DHCP address")
        check("pvid" in text.lower(), "PVID row present")
        check(cdp.js("document.querySelector('.tabs a.on').textContent.trim()") == "Port" and not cdp.js("!!document.getElementById('tcard')"), "Port tab shown by default, Tools not rendered")
        check(cdp.js("!document.querySelector('.tabs').hidden"), "tab strip shown (not read-only)")
        check(hstat() == "mgi6 · VLAN 1", "header status line: " + str(hstat()))
        check(cdp.js("document.title") == "mgi6 · VLAN 1 · picoplug", "browser tab title carries the answer")
        view("tools")
        check(cdp.js("!!document.getElementById('tcard') && !document.querySelector('.card.nb')"), "Tools tab switches views without reload")
        check(cdp.js("!!document.querySelector('.tout')"), "tool output visible for a finished ping")
        ttext = cdp.js("document.body.innerText")
        check("4 received" in ttext and "20% loss" in ttext, "ping summary rendered")
        check("Recent runs".lower() in ttext.lower() and "4/5 replies" in ttext, "finished ping lands in Recent runs")
        check(cdp.js("document.getElementById('t_host').value") == "192.168.109.1", "host defaults to the gateway")
        click_text(".qt button", "Internet")
        check(cdp.js("document.getElementById('t_host').value") == "1.1.1.1", "Internet quick target fills the host")
        view("setup")
        click_text(".seg button", "Dark")
        check(cdp.js("document.documentElement.dataset.theme") == "dark", "Dark sets data-theme on html")
        click_text(".seg button", "Auto")
        check(cdp.js("document.documentElement.dataset.theme") is None, "Auto clears data-theme")
        cdp.js("{const p=document.querySelector('select[aria-label=\"Probe number\"]');p.value='4';p.dispatchEvent(new Event('change',{bubbles:true}))}"); time.sleep(0.8)
        check("picoplug-4" in cdp.js("document.body.innerText"), "probe dropdown saves and names the device")
        view("log")
        check("mgi6" in cdp.js("document.querySelector('.hist').innerText"), "Log lists the port visited")
        view("port")
        # the Details expanders open and close
        cdp.js("document.querySelector('.card.nb .disc').click()")
        time.sleep(0.3)
        check("chassis" in cdp.js("document.body.innerText").lower(), "neighbor Details expands (innerText follows CSS text-transform, so compare lowercased)")
        check(len(cdp.js("Array.from(document.querySelectorAll('*')).filter(e=>/\\p{Extended_Pictographic}/u.test(e.textContent||'')).length ? ['x'] : []")) == 0,
              "no emoji characters on the page")

        # tap-to-copy: every value row copies; identifier rows copy just the identifier
        cdp.js("[...document.querySelectorAll('a.chk')].find(a=>a.textContent.includes('Gateway')).click()"); time.sleep(0.6)
        check(cdp.js("location.hash") == '#tools' and cdp.js("document.getElementById('t_host').value") == '192.168.109.1', 'Gateway check opens Tools with the gateway filled in')
        view("port")
        check(tap('Address') == '192.168.109.101', 'Address row copies the IP without the mask')
        check(tap('PVID') == '1', 'PVID row copies its text')
        cdp.js("[...document.querySelectorAll('.card .disc')].find(b=>!b.closest('.nb')).click()")
        time.sleep(0.3)
        check(tap('DHCP server') == '192.168.109.1', 'DHCP server row copies the server address')
        check(tap('Root bridge') not in (None, 'NO ROW', ''), 'spanning-tree rows copy their text')
        # per-value segments: tapping one value copies only that value, bare
        seg = cdp.js("(()=>{const r=[...document.querySelectorAll('.kv.copy')].find(e=>e.querySelector('.k').textContent.trim()==='Timers');if(!r)return 'NO ROW';const s=r.querySelectorAll('.sg')[1];window.__copied=null;s.click();return JSON.stringify({got:window.__copied,text:s.textContent})})()")
        seg = json.loads(seg) if seg != 'NO ROW' else {}
        check(seg and seg['got'] and seg['got'].strip().isdigit() and 'max age' in seg['text'] and seg['got'] in seg['text'], 'Timers: tapping the max-age value copies just the number: ' + json.dumps(seg))
        mac = cdp.js("(()=>{const r=[...document.querySelectorAll('.kv.copy')].find(e=>e.querySelector('.k').textContent.trim()==='Root bridge');const s=[...r.querySelectorAll('.sg')].find(x=>/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/.test(x.textContent.trim()));window.__copied=null;s.click();return window.__copied})()")
        check(mac and len(mac) == 17, 'Root bridge: tapping the MAC copies the MAC only: ' + str(mac))
        cdp.call('Page.reload'); time.sleep(2.5)

        # --- case 1: hostile strings must stay text
        load(1)
        check(cdp.js("typeof window.__pwned") == "undefined", "no injected script ran")
        check(cdp.js("document.querySelectorAll('img').length") == 0, "no <img> created from switch strings")
        check(cdp.js("document.querySelectorAll('script').length") == 2, "only the page's own two script tags")
        text = cdp.js("document.body.innerText")
        check("<img src=x" in text, "hostile sys_name rendered as text")
        cdp.js("document.querySelectorAll('.card .disc').forEach(b=>b.click())")
        time.sleep(0.3)
        check(cdp.js("typeof window.__pwned") == "undefined", "no injected script ran after expanding")
        check(cdp.js("document.querySelectorAll('img').length") == 0, "no <img> after expanding")
        view("tools")
        check(cdp.js("document.querySelectorAll('.tout img').length") == 0 and cdp.js("typeof window.__pwned") == "undefined", "trace host/hops escaped")

        # --- case 2: link down
        load(2)
        text = cdp.js("document.body.innerText")
        check("No Ethernet link" in text or "no link" in text.lower(), "link-down banner")
        view("tools")
        check(cdp.js("!document.querySelector('.tout')"), "tool output hidden when idle")

        # --- case 3: continuous ping with losses: plot present, in-flight probe excluded
        load(3)
        view("tools")
        check(cdp.js("document.querySelectorAll('.tout svg.tplot rect').length") > 20, "RTT plot has bars")
        check(cdp.js("document.querySelectorAll('.tout svg.tplot rect.to').length") == 3, "lost probes drawn as no-reply bars")
        text = cdp.js("document.querySelector('.tout').innerText")
        check("30 sent, 27 received" in text, "loss line counts only completed probes")

        # --- case 4: 802.1X port, no DHCP yet, blinker running
        load(4)
        text = cdp.js("document.body.innerText")
        check("802.1x" in text.lower() and "auth requested" in text, "802.1X row present")
        check("EAP identity" in text and "2 requests" in text and "every 30 s" in text, "802.1X row details: " + text[text.lower().find("802.1x"):][:160].replace("\n", " | "))
        check("will not pass DHCP" not in text, "no explanation of the DHCP result: the page states facts only")
        check(tap('Authenticator') == '64:9d:99:32:7e:90', '802.1X card copies the authenticator MAC')
        view("tools")
        blink = "[...document.querySelectorAll('.card')].find(c=>c.textContent.includes('Find this port'))"
        check(cdp.js(blink + ".querySelector('button').textContent.trim()") == "Stop blinking", "blink button shows the stop label while active")
        check("21 s left" in cdp.js(blink + ".querySelector('.crow .meta').textContent"), "blink status shows the time left")
        load(0)
        view("tools")
        check(cdp.js(blink + ".querySelector('button').textContent.trim()") == "Blink port", "blink button idle label")
        check(cdp.js(blink + ".querySelector('.crow .meta').textContent") == "", "no blink status when idle")

        # --- case 5: an AV network: IGMP querier and a PTP grandmaster
        load(5)
        text = cdp.js("document.body.innerText")
        check("igmp querier" in text.lower() and "192.168.109.1" in text and "every 125 s" in text, "IGMP querier row")
        check("· the gateway" in text and "IGMPv3" in text and "robustness 2" in text and "answer within 10 s" in text, "IGMP querier identified and v3 details shown")
        check("3 reports from others" in text and "probably" not in text, "stray reports counted, no snooping guess")
        check("multicast router" in text.lower() and "PIM hello" in text and "DR priority 1" in text and "hold 105 s" in text, "multicast router row")
        got = tap('Multicast router')
        check(got == '192.168.109.1', 'multicast router row copies its address, got ' + repr(got))
        check(cdp.js("document.querySelector('#pc-mcast .sec').textContent") == "IGMP and PIM", "card title names PIM once a PIM router is heard")
        check("PTPv2 · domain 0 · UDP" in text and "sync every 125 ms" in text and "two-step" in text, "PTP row summary")
        check("00:1d:c1:ff:fe:12:34:56" in text and "class 6" in text and "gps" in text and "1 hop away" in text, "PTP grandmaster line")
        check(tap('IGMP querier') == '192.168.109.1', 'IGMP row copies the querier address')
        check(tap('PTP v2') == '192.168.109.50', 'PTP v2 row copies the master address')
        check("PTPv1 · subdomain _DFLT" in text and "sync every 250 ms" in text, "PTP v1 row")
        check(not any(w in text for w in ("Dante", "AES67", "SMPTE")), "PTP rows name no system or profile")
        check(tap('PTP v1') == '192.168.109.129', 'PTP v1 row copies the master address')
        load(0)
        check(cdp.js("!!document.querySelector('#pc-mcast.empty') && !!document.querySelector('#pc-ptp.empty')"), "no IGMP/PTP rows on an ordinary port")
        check(cdp.js("document.querySelector('#pc-mcast .sec').textContent") == "IGMP", "multicast card is titled IGMP when no PIM router is heard")
        check(cdp.js("!!document.querySelector('#pc-lacp.empty .psum use[href=\\'#i-fail\\']')") and cdp.js("document.querySelector('#pc-lacp .sr').textContent") == "not in one", "LAG card folded to an X on an ordinary port, words kept for screen readers")
        check(cdp.js("!!document.querySelector('#pc-peer.empty')"), "probes card folded without peers")
        check(cdp.js("!!document.querySelector('#pc-stp:not(.empty) .grid')"), "spanning-tree card open when BPDUs are heard")
        cdp.js("document.querySelector('#pc-stp .phead').click()"); time.sleep(0.3)
        check(cdp.js("!document.querySelector('#pc-stp .grid')"), "a card with data folds on tap")
        cdp.js("document.querySelector('#pc-stp .phead').click()"); time.sleep(0.3)

        # --- case 6: an LACP LAG member the switch holds out of the bundle
        load(6)
        text = cdp.js("document.body.innerText")
        check("not forwarding" in text and "every 1 s" in text and "key 2" in text and "port 7" in text, "LAG row with LACP details")
        check("LACP port channel" not in text and "Use a port outside" not in text and cdp.js("!!document.querySelector('#pc-dot1x.empty')"), "no DHCP hint or advice about the LAG")
        check(tap('LACP') == '2', 'LAG card copies the LACP key')

        # --- case 7: another probe on the same switch, path test says switched, PVIDs differ
        load(7)
        text = cdp.js("document.body.innerText")
        check("other probes" in text.lower() and "02:50:43:aa:bb:02" in text and "same switch" in text and "mgi7" in text, "peer row names the shared switch and its port")
        check("PVID 10" in text and "there," not in text, "peer row shows its PVID without flagging it")
        check("link stayed up" in text and "back after 31 s" in text and "31 s" in text and "portfast" not in text and "Through a switch" not in text, "path test states what the link did, no verdict or hint")
        check("5 other source MACs heard" in text, "other-devices count")
        check(cdp.js("[...document.querySelectorAll('#pc-peer button')].some(b=>b.textContent.trim()==='Test path')"), "Test path button present when idle")
        check(cdp.js("document.querySelector('.logo .pb2')?.textContent") == "2", "header shows this unit's probe badge, red for 2")
        check("Probe 5 · green" in text and cdp.js("!!document.querySelector('#pc-peer .pb5')"), "peer row shows the other probe's number and color")
        check(cdp.js("document.title").endswith("picoplug-2"), "tab title carries the probe number")

        # --- case 8: a bare cable between two probes
        load(8)
        text = cdp.js("document.body.innerText")
        check("link dropped" in text and "down 5.2 s" in text and "Direct cable" not in text, "path test: link dropped with the other probe")
        check("neighbor: this probe" in text, "peer that sees us as its neighbor")
        check("Other devices" in text and "none heard" in text and "nothing but probes" not in text, "no other MACs on a bare cable, stated plainly")
        cdp.call("Emulation.setDeviceMetricsOverride", width=375, height=800, deviceScaleFactor=1, mobile=True)
        for i in (7, 8):
            load(i)
            sw = cdp.js("document.documentElement.scrollWidth")
            check(sw <= 375, f"case {i}: no horizontal overflow at 375 px (scrollWidth {sw})")
        cdp.call("Emulation.clearDeviceMetricsOverride")

        # --- form link: a button on the Port tab with the neighbor filled into the Setup template
        formbtn = "([...document.querySelectorAll('.card.nb a.btn')].find(a=>a.textContent.trim()==='Form')||{})"
        load(0)
        href = cdp.js(formbtn + ".href||''")
        check(href.startswith("https://mdn.github.io/dom-examples/url-params/?switch=") and "&port=mgi6&vlan=1" in href, "form button fills the default link: " + href)
        check(cdp.js(formbtn + ".target") == "_blank", "form button opens a new tab")
        formcard = "[...document.querySelectorAll('.card')].find(c=>c.querySelector('#s_form'))"
        view("setup", 0.8)
        cdp.js("{const f=document.getElementById('s_form');f.value='https://nf.example.net/noc/?requestType=Switch&switchIp={switch_ip}&port={port}';f.dispatchEvent(new Event('input'))}"); time.sleep(0.3)
        check("port=mgi6" in cdp.js(formcard + ".innerText"), "Setup previews the filled-in link")
        cdp.js(formcard + ".querySelector('button.p').click()"); time.sleep(0.8)
        view("port", 2.5)
        href = cdp.js(formbtn + ".href||''")
        check(href.startswith("https://nf.example.net/noc/?requestType=Switch&switchIp=") and href.endswith("&port=mgi6"), "saved link drives the button: " + href)
        view("setup", 0.8)
        cdp.js("{const f=document.getElementById('s_form');f.value='';f.dispatchEvent(new Event('input'))}"); time.sleep(0.3)
        cdp.js(formcard + ".querySelector('button.p').click()"); time.sleep(0.8)
        view("port", 2.5)
        check(cdp.js(formbtn + ".href") is None, "blank link hides the button")
        urllib.request.urlopen(base + "/settings?form_url=" + urllib.parse.quote("https://mdn.github.io/dom-examples/url-params/?switch={switch}&switch_ip={switch_ip}&port={port}&vlan={vlan}", safe="")).read()

        # --- case 9: link up, nothing heard yet
        load(9)
        text = cdp.js("document.body.innerText")
        check("Listening" in text and "LLDP · CDP" in text and hstat() == "Listening… 18 s", "listening state: " + str(hstat()))
        # --- case 10: neighbor past half its TTL
        load(10)
        text = cdp.js("document.body.innerText")
        check(cdp.js("!!document.querySelector('.card.nb.stale')") and "Relisten" in text and "stale" in hstat(), "stale neighbor state: " + str(hstat()))

        # --- case 11: a Mac running lldpd: MAC as port ID, interface name in the description
        load(11)
        check(cdp.js("document.querySelector('.card.nb .port').textContent") == "en10", "host neighbor shows the interface name, not the MAC")
        check(cdp.js("!!document.querySelector('.card.nb use[href=\"#i-laptop\"]')"), "host neighbor gets the laptop icon")
        check("en10" in hstat(), "header line names the interface")

        # --- case 12: only another picoplug's CDP, flooded through a switch that does not run CDP
        load(12)
        text = cdp.js("document.body.innerText")
        check("Another picoplug" in text, "picoplug neighbor is identified")
        check(cdp.js("!!document.querySelector('.card.nb use[href=\"#i-probes\"]')"), "picoplug neighbor gets the probes icon")
        check(hstat() == "No switch · picoplug-3", "header does not name the picoplug as the switch port")
        check(cdp.js(formbtn + ".href") is None, "no form button for a picoplug neighbor")
        check(not any(e.get("sys_name") == "picoplug-3" for e in json.loads(cdp.js("localStorage.getItem('piccolldp.history.v1')") or "[]")), "picoplug neighbor kept out of the port log")
        view("setup")
        check("no CDP switch" in cdp.js("document.body.innerText"), "Setup says no CDP switch heard")
        view("port", 0.3)

        # --- case 13: fields checked against Wireshark/tcpdump: 802.3bt PoE, mgmt VLAN, protocols, EEE, location, MUD URL
        cdp.call("Emulation.setDeviceMetricsOverride", width=375, height=800, deviceScaleFactor=1, mobile=True)
        load(13)
        if not cdp.js("document.querySelector('.card.nb .disc').getAttribute('aria-expanded')==='true'"):
            cdp.js("document.querySelector('.card.nb .disc').click()"); time.sleep(0.3)
        text = cdp.js("document.querySelector('.card.nb').innerText")
        for want in ("Class 6", "4-pair", "Mgmt VLAN", "99", "Protocol VLANs", "300", "RSTP, LACP, 802.1X, 0x1234",
                     "tx 17 µs", "Floor 2, Room B12", "example.com/mud", "member of aggregate 1001"):
            check(want in text, f"extras: {want!r} shown")
        sw = cdp.js("document.documentElement.scrollWidth")
        check(sw <= 375, f"extras: no horizontal overflow at 375 px (scrollWidth {sw})")
        cdp.call("Emulation.clearDeviceMetricsOverride")

        # --- keepalive: lost replies show the reconnecting line and dim the page; a reply clears it
        cdp.call("Network.enable")
        cdp.call("Network.setBlockedURLs", urls=["*status.json*"])
        time.sleep(4)
        check(cdp.js("!!document.querySelector('.conn')") and "Reconnecting" in cdp.js("document.querySelector('.conn').textContent")
              and cdp.js("document.body.classList.contains('offline')"), "reconnecting line while the device does not answer")
        cdp.call("Network.setBlockedURLs", urls=[])
        time.sleep(4)
        check(cdp.js("!document.querySelector('.conn')") and not cdp.js("document.body.classList.contains('offline')"), "reconnecting line clears on the next reply")

        # --- read-only mode: a 403 from /tool hides the control cards
        cdp.js("""(async()=>{const r=await fetch('/tool');return r.status})()""")
        exc = cdp.exceptions()
        check(not exc, "no uncaught JavaScript exceptions: " + json.dumps([e["params"]["exceptionDetails"].get("text") for e in exc])[:300])
    finally:
        browser.terminate()
        mock.terminate()
        shutil.rmtree(prof, ignore_errors=True)
    if failures:
        print(f"test_page: {len(failures)} failure(s)")
        return 1
    print("test_page: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
