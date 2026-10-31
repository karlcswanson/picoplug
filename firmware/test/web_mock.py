#!/usr/bin/env python3
"""
Serve the built viewer page (web/dist/index.html) and a canned /status.json for developing the viewer
page without hardware.

    python3 web_mock.py [--port 8080] [--ndjson FILE] [--index N]

GET /__mock/index?N pins the served line (used by test_page.py).
FILE is NDJSON (one status object per line), e.g. the output of the host
test binary: `build-test/test_json > status.ndjson`. Without --index the
server cycles through the lines every 4 s so you can watch transitions.
"""
import argparse
import http.server
import json
import urllib.parse
import os
import time

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "..", "src", "web")
PAGE = os.path.join(HERE, "..", "web", "dist", "index.html")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--ndjson", default=os.path.join(HERE, "status.ndjson"))
    ap.add_argument("--index", type=int, default=None)
    a = ap.parse_args()
    a.probe = None
    a.form_url = "https://mdn.github.io/dom-examples/url-params/?switch={switch}&switch_ip={switch_ip}&port={port}&vlan={vlan}"
    with open(a.ndjson) as f:
        lines = [l.strip() for l in f if l.strip()]
    t0 = time.time()

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *x):
            pass

        def do_GET(self):
            path, _, query = self.path.partition("?")
            if path in ("/", "/index.html"):
                body = open(PAGE, "rb").read()
                ctype = "text/html; charset=utf-8"
            elif path == "/status.json":
                i = a.index if a.index is not None else int((time.time() - t0) // 4) % len(lines)
                body = lines[i].encode()
                ctype = "application/json"
            elif path in ("/icon.png", "/apple-touch-icon.png", "/favicon.ico"):
                body = open(os.path.join(WEB, "icon.png"), "rb").read()
                ctype = "image/png"
            elif path == "/manifest.json":
                body = open(os.path.join(WEB, "manifest.json"), "rb").read()
                ctype = "application/manifest+json"
            elif path == "/settings":
                # just enough for the Setup tab: remembers the probe number and the form link
                q = dict(kv.split("=", 1) for kv in query.split("&") if "=" in kv)
                if "probe" in q:
                    a.probe = int(q["probe"]) if q["probe"] else None
                if "form_url" in q:
                    a.form_url = urllib.parse.unquote(q["form_url"])
                body = json.dumps({"ok": True, "saved": "probe" in q or "form_url" in q, "form_url": a.form_url, "error": "", "lldp": True, "cdp": True, "probe": a.probe,
                                   "ping_target": "1.1.1.1", "dns_name": "clockclub.net", "dns_server": "", "usb_ip": "192.168.7.1",
                                   "running_ip": "192.168.7.1", "reboot_needed": False,
                                   "defaults": {"ping_target": "1.1.1.1", "dns_name": "clockclub.net", "dns_server": "", "usb_ip": "192.168.7.1",
                                                "form_url": "https://mdn.github.io/dom-examples/url-params/?switch={switch}&switch_ip={switch_ip}&port={port}&vlan={vlan}"}}).encode() + b"\n"
                ctype = "application/json"
            elif path == "/__mock/index":
                # test hook: GET /__mock/index?N pins /status.json to line N
                a.index = int(query) if query else None
                body = b'{"ok":true}\n'
                ctype = "application/json"
            else:
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

    print(f"mock viewer on http://localhost:{a.port}/ ({len(lines)} messages)")
    http.server.ThreadingHTTPServer(("", a.port), H).serve_forever()


if __name__ == "__main__":
    main()
