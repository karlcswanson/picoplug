#!/usr/bin/env python3
"""
Relay the board's page to a phone on the same Wi-Fi while the board stays
plugged into this Mac.

    python3 firmware/tools/phone_proxy.py            # listen on :8080 -> 192.168.7.1:80
    python3 firmware/tools/phone_proxy.py --port 9000 --target 192.168.7.1:9000   # NDJSON stream
    python3 firmware/tools/phone_proxy.py --target picolldp.local:80

Plain TCP forwarding, so /status.json polling, /tool and /settings all
work; the page only uses relative URLs. Prints the URL to
open on the phone. Stop with Ctrl-C. macOS may ask once whether python3
may accept incoming connections; allow it.
"""
import argparse
import asyncio
import socket
import subprocess
import sys


def lan_ips():
    out = []
    for ifc in ("en0", "en1", "en8", "en7", "en6", "en5"):
        try:
            ip = subprocess.run(["ipconfig", "getifaddr", ifc], capture_output=True, text=True).stdout.strip()
        except FileNotFoundError:
            break
        if ip and not ip.startswith("192.168.7."):
            out.append((ifc, ip))
    return out


async def pipe(r, w):
    try:
        while True:
            data = await r.read(65536)
            if not data:
                break
            w.write(data)
            await w.drain()
    except (ConnectionError, asyncio.CancelledError):
        pass
    finally:
        try:
            w.close()
        except Exception:
            pass


async def handle(client_r, client_w, host, port):
    try:
        board_r, board_w = await asyncio.wait_for(asyncio.open_connection(host, port), 5)
    except Exception as e:
        peer = client_w.get_extra_info("peername")
        print(f"{peer[0]}: board {host}:{port} not reachable ({e})", file=sys.stderr)
        client_w.close()
        return
    await asyncio.gather(pipe(client_r, board_w), pipe(board_r, client_w))


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080, help="port to listen on (default 8080)")
    ap.add_argument("--target", default="192.168.7.1:80", help="board host:port (default 192.168.7.1:80)")
    a = ap.parse_args()
    host, _, port = a.target.rpartition(":")
    port = int(port)
    server = await asyncio.start_server(lambda r, w: handle(r, w, host, port), "0.0.0.0", a.port, reuse_address=True)
    ips = lan_ips()
    if ips:
        for ifc, ip in ips:
            print(f"open on the phone:  http://{ip}:{a.port}/   ({ifc})", flush=True)
    else:
        print(f"listening on :{a.port}; use this Mac's Wi-Fi address", file=sys.stderr)
    print(f"relaying to {host}:{port}; Ctrl-C to stop", flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
