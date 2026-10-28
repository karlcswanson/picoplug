#!/usr/bin/env python3
"""
Dump the configuration and state of an FS S3200-series switch through its
web UI (the only management port open on the bench switch; SSH and telnet
are off). Pure standard library.

    python3 firmware/tools/fs_switch_dump.py                      # prompts for the password
    FS_PASSWORD=... python3 firmware/tools/fs_switch_dump.py      # no prompt
    python3 firmware/tools/fs_switch_dump.py --password-file ~/.fs_switch_pw   # password read from a file you created
    python3 firmware/tools/fs_switch_dump.py --host 192.168.109.5 --user admin --out switch-dump
    python3 firmware/tools/fs_switch_dump.py --only 'mcast|stp|lldp'  # just those endpoints
    python3 firmware/tools/fs_switch_dump.py --list               # endpoint list only, no login

What it does, mirroring the browser:
  1. GET  cgi/get.cgi?cmd=home_login        -> RSA modulus (e = 0x10001)
  2. POST cgi/set.cgi?cmd=home_loginAuth    -> body is the login form, password
     PKCS#1 v1.5 encrypted and base64'd, wrapped the way Backbone 1.0 sends a
     string attribute (a JSON object whose only key is the form string)
  3. GET  cgi/get.cgi?cmd=home_loginStatus  -> poll until it leaves "authing"
  4. GET  every cgi/get.cgi?cmd=<x> listed in the UI's js/url.js (about 280;
     the ones that need a row parameter, *Add/*Edit/*Detail, are skipped
     unless --all), one JSON file each in the output directory
  5. POST cgi/set.cgi?cmd=file_backup (running config over HTTP) and fetch
     the file it names -> running-config.txt
  6. print the IGMP snooping summary (global, per VLAN, querier, groups,
     router ports) so the question "is the querier on?" is answered on
     the terminal.

Tested against: S3200-8MG4S-U web UI dated 2022-11 (server "Hydra/0.1.8").
"""
import argparse
import base64
import getpass
import http.cookiejar
import json
import os
import re
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


class Switch:
    def __init__(self, host, verbose=False):
        self.base = f"http://{host}/"
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))
        self.verbose = verbose

    def _req(self, path, data=None, json_body=False):
        url = self.base + path + ("&" if "?" in path else "?") + f"dummy={int(time.time() * 1000)}"
        headers = {"Referer": self.base + "home.html", "X-Requested-With": "XMLHttpRequest"}
        body = None
        if data is not None:
            if json_body:
                body = json.dumps(data).encode()
                headers["Content-Type"] = "application/json"
            else:
                body = data.encode() if isinstance(data, str) else data
                headers["Content-Type"] = "application/x-www-form-urlencoded"
        req = urllib.request.Request(url, data=body, headers=headers, method="POST" if body is not None else "GET")
        with self.opener.open(req, timeout=15) as r:
            raw = r.read()
        if self.verbose:
            print(f"  {req.get_method()} {path} -> {len(raw)} bytes", file=sys.stderr)
        return raw

    def get_json(self, cmd):
        raw = self._req(f"cgi/get.cgi?cmd={cmd}")
        try:
            return json.loads(raw.decode("utf-8", "replace"))
        except json.JSONDecodeError:
            return {"_raw": raw.decode("utf-8", "replace")}

    def set_form(self, cmd, form):
        """POST like the UI: Backbone.Model.save(formString) -> {"<formString>": {}}"""
        raw = self._req(f"cgi/set.cgi?cmd={cmd}", {f"_ds=1&{form}&_de=1": {}}, json_body=True)
        try:
            return json.loads(raw.decode("utf-8", "replace"))
        except json.JSONDecodeError:
            return {"_raw": raw.decode("utf-8", "replace")}

    # ---- login -------------------------------------------------------------
    @staticmethod
    def rsa_pkcs1_v15(modulus_hex, text):
        n = int(modulus_hex, 16)
        k = (n.bit_length() + 7) // 8
        m = text.encode("utf-8")
        if len(m) > k - 11:
            raise ValueError("password too long for the switch's RSA key")
        ps = bytearray()
        while len(ps) < k - 3 - len(m):
            b = secrets.token_bytes(1)
            if b != b"\x00":
                ps += b
        em = b"\x00\x02" + bytes(ps) + b"\x00" + m
        c = pow(int.from_bytes(em, "big"), 0x10001, n)
        return base64.b64encode(c.to_bytes((c.bit_length() + 7) // 8, "big")).decode()

    def login(self, user, password):
        info = self.get_json("home_login").get("data", {})
        modulus = info.get("modulus")
        if not modulus:
            raise RuntimeError(f"no RSA modulus in home_login reply: {info}")
        if info.get("https"):
            raise RuntimeError("switch wants HTTPS login; this script speaks plain HTTP")
        enc = self.rsa_pkcs1_v15(modulus, password)
        form = f"username={urllib.parse.quote(user)}&password={urllib.parse.quote(enc)}"
        r = self.set_form("home_loginAuth", form)
        if self.verbose:
            print(f"  loginAuth reply: {r}", file=sys.stderr)
        status = None
        for _ in range(20):
            st = self.get_json("home_loginStatus")
            status = st.get("data", {}).get("status", st)
            if status != "authing":
                break
            time.sleep(0.5)
        if status not in ("ok", "success", "authed", "login"):
            raise RuntimeError(f"login did not succeed, status: {status!r}")
        return status

    # ---- endpoint list -------------------------------------------------------
    def endpoints(self):
        js = self._req("js/url.js").decode("utf-8", "replace")
        out = []
        for name, cmd in re.findall(r"var url_get_(\w+)\s*=\s*'[^']*get\.cgi\?cmd=(\w+)'", js):
            out.append((name, cmd))
        return out


IGMP_CMDS = ["mcast_genProp", "mcast_igmpProp", "mcast_igmpQuerier", "mcast_genGroup", "mcast_genRouter", "mcast_igmpStat"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.109.5")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", help="or set FS_PASSWORD; prompted otherwise")
    ap.add_argument("--password-file", default=os.path.expanduser("~/.fs_switch_pw"), help="file holding the password (default ~/.fs_switch_pw if it exists)")
    ap.add_argument("--out", help="output directory (default switch-dump-<time>)")
    ap.add_argument("--only", help="regex on endpoint name or cmd; dump only those")
    ap.add_argument("--all", action="store_true", help="also hit the *Add/*Edit/*Detail endpoints that need a row parameter")
    ap.add_argument("--no-config", action="store_true", help="skip the running-config backup")
    ap.add_argument("--list", action="store_true", help="print the endpoint list and exit, no login")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    sw = Switch(a.host, a.verbose)
    eps = sw.endpoints()
    if a.only:
        rx = re.compile(a.only, re.I)
        eps = [e for e in eps if rx.search(e[0]) or rx.search(e[1])]
    if not a.all:
        eps = [e for e in eps if not re.search(r"(Add|Edit|Detail|Del)$", e[0])]
    # Some "get" commands start an action on the switch (cable test, tech-support
    # generation, debug), and the copper cable test drops the port's link. Skip them.
    eps = [e for e in eps if not re.search(r"^(diag|cgiDbg|monitor|mainPage)", e[0])]
    if a.list:
        for name, cmd in eps:
            print(f"{name:32s} {cmd}")
        print(f"{len(eps)} endpoints", file=sys.stderr)
        return 0

    pw = a.password or os.environ.get("FS_PASSWORD")
    if not pw and os.path.exists(a.password_file):
        with open(a.password_file) as f:
            pw = f.read().strip()
    if not pw:
        pw = getpass.getpass(f"password for {a.user}@{a.host}: ")
    print(f"login {a.user}@{a.host} ...", end=" ", flush=True)
    print(sw.login(a.user, pw))

    out = a.out or time.strftime("switch-dump-%Y%m%d-%H%M%S")
    os.makedirs(out, exist_ok=True)
    ok = fail = 0
    for name, cmd in eps:
        try:
            j = sw.get_json(cmd)
        except (urllib.error.URLError, OSError) as e:
            j = {"_error": str(e)}
        if j.get("logout"):
            print(f"session dropped at {cmd}: {j}", file=sys.stderr)
            return 2
        with open(os.path.join(out, f"{cmd}.json"), "w") as f:
            json.dump(j, f, indent=1)
        if "_error" in j or j.get("status") in ("fail", "error"):
            fail += 1
        else:
            ok += 1
    print(f"{ok} endpoints saved, {fail} answered with an error, in {out}/")

    if not a.no_config:
        r = sw.set_form("file_backup", "cfg_action=backup&method=http&fileType=running&host=ipv4&srvHost=&srcFileName=&dstFileName=")
        fn = r.get("filename")
        if fn:
            raw = sw._req(fn.lstrip("/"))
            with open(os.path.join(out, "running-config.txt"), "wb") as f:
                f.write(raw)
            print(f"running config: {len(raw)} bytes -> {out}/running-config.txt")
        else:
            print(f"running-config backup not offered: {r}")

    print("\nIGMP snooping summary")
    for cmd in IGMP_CMDS:
        p = os.path.join(out, f"{cmd}.json")
        if not os.path.exists(p):
            continue
        with open(p) as f:
            j = json.load(f)
        print(f"--- {cmd}")
        print(json.dumps(j.get("data", j), indent=1)[:4000])
    return 0


if __name__ == "__main__":
    sys.exit(main())
