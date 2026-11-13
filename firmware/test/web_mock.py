#!/usr/bin/env python3
"""
Serve the built viewer page (web/dist/index.html) and a canned /status.json for developing the viewer
page without hardware.

    python3 web_mock.py [--port 8080] [--ndjson FILE] [--index N]

GET /__mock/index?N pins the served line (used by test_page.py). /ws is a WebSocket that pushes
the current line at once when it changes and every 2 s, like the firmware; GET /__mock/ws?0
closes the sockets and refuses new ones (?1 allows them again), so the page falls back to polling.
FILE is NDJSON (one status object per line), e.g. the output of the host
test binary: `build-test/test_json > status.ndjson`. Without --index the
server cycles through the lines every 4 s so you can watch transitions.
"""
import argparse
import base64
import hashlib
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
    a.ws = True
    a.form_url = "https://clockclub.net/apps/picoplug/#{share}"
    with open(a.ndjson) as f:
        lines = [l.strip() for l in f if l.strip()]
    t0 = time.time()
    current = lambda: a.index if a.index is not None else int((time.time() - t0) // 4) % len(lines)

    def frame(data, opcode=0x1):
        n = len(data)
        hdr = bytes([0x80 | opcode, n]) if n < 126 else bytes([0x80 | opcode, 126]) + n.to_bytes(2, "big") if n < 65536 \
            else bytes([0x80 | opcode, 127]) + n.to_bytes(8, "big")
        return hdr + data

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *x):
            pass

        def do_GET(self):
            path, _, query = self.path.partition("?")
            if path in ("/", "/index.html"):
                body = open(PAGE, "rb").read()
                ctype = "text/html; charset=utf-8"
            elif path == "/ws" and a.ws and self.headers.get("Sec-WebSocket-Key"):
                key = self.headers["Sec-WebSocket-Key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
                self.send_response(101)
                self.send_header("Upgrade", "websocket")
                self.send_header("Connection", "Upgrade")
                self.send_header("Sec-WebSocket-Accept", base64.b64encode(hashlib.sha1(key.encode()).digest()).decode())
                self.end_headers()
                self.close_connection = True
                last, sent = None, 0.0
                try:
                    while a.ws:
                        i = current()
                        if i != last or time.time() - sent > 2:
                            self.wfile.write(frame(lines[i].encode())); self.wfile.flush()
                            last, sent = i, time.time()
                        time.sleep(0.05)
                    self.wfile.write(frame(b"", 0x8)); self.wfile.flush()
                except OSError:
                    pass
                return
            elif path == "/status.json":
                body = lines[current()].encode()
                ctype = "application/json"
            elif path in ("/icon.png", "/apple-touch-icon.png", "/favicon.ico"):
                body = open(os.path.join(WEB, "icon.png"), "rb").read()
                ctype = "image/png"
            elif path == "/manifest.json":
                body = open(os.path.join(WEB, "manifest.json"), "rb").read()
                ctype = "application/manifest+json"
            elif path == "/settings":
                # just enough for the Settings tab: remembers the probe number and the form link
                q = dict(kv.split("=", 1) for kv in query.split("&") if "=" in kv)
                if "probe" in q:
                    a.probe = int(q["probe"]) if q["probe"] else None
                if "form_url" in q:
                    a.form_url = urllib.parse.unquote(q["form_url"])
                body = json.dumps({"ok": True, "saved": "probe" in q or "form_url" in q, "form_url": a.form_url, "error": "", "lldp": True, "cdp": True, "probe": a.probe,
                                   "ping_target": "1.1.1.1", "dns_name": "clockclub.net", "dns_server": "", "usb_ip": "192.168.7.1",
                                   "running_ip": "192.168.7.1", "reboot_needed": False,
                                   "defaults": {"ping_target": "1.1.1.1", "dns_name": "clockclub.net", "dns_server": "", "usb_ip": "192.168.7.1",
                                                "form_url": "https://clockclub.net/apps/picoplug/#{share}"}}).encode() + b"\n"
                ctype = "application/json"
            elif path == "/peer":
                # GET /peer?scan=1 -> ok; ?name=NAME -> another tester's status: a tester listed in the
                # current line's peers reads as line 6 (the LAG port), any other name is 404
                q = dict(kv.split("=", 1) for kv in query.split("&") if "=" in kv)
                names = [x["name"] for x in json.loads(lines[current()]).get("peers", {}).get("list", [])]
                if q.get("scan") == "1":
                    body, ctype = b'{"ok":true}\n', "application/json"
                elif urllib.parse.unquote(q.get("name", "")) in names:
                    body = lines[6].encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("X-Age-Ms", "300")
                    self.end_headers()
                    self.wfile.write(body)
                    return
                else:
                    body = b'{"ok":false,"pending":false,"error":"not heard on this network"}\n'
                    self.send_response(404)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
            elif path == "/__mock/ws":
                a.ws = query != "0"
                body = b'{"ok":true}\n'
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
