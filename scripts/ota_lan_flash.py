#!/usr/bin/env python3
"""Discover MM1-BLACK devices on the LAN / SoftAP and POST firmware.bin to /update.

After Join, firmware serves HTTP on the STA IP. SoftAP 192.168.4.1 still works.
Usage:
  ./scripts/ota_lan_flash.py
  ./scripts/ota_lan_flash.py --bin .pio/build/mm1_p4/firmware.bin --watch
"""
from __future__ import annotations

import argparse
import concurrent.futures
import ipaddress
import json
import os
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_BIN = os.path.join(ROOT, ".pio", "build", "mm1_p4", "firmware.bin")


def local_subnets() -> list[str]:
    nets = ["192.168.4.1"]
    seen: set[str] = set()
    try:
        out = os.popen("ip -4 -o addr show").read()
    except Exception:
        out = ""
    for line in out.splitlines():
        parts = line.split()
        if "inet" not in parts:
            continue
        cidr = parts[parts.index("inet") + 1]
        try:
            iface = ipaddress.ip_interface(cidr)
        except ValueError:
            continue
        if iface.ip.is_loopback or iface.network.prefixlen < 16:
            continue
        net = str(iface.network)
        if net not in seen:
            seen.add(net)
            nets.append(net)
    return nets


def probe(ip: str, timeout: float) -> dict | None:
    url = f"http://{ip}/api/status"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "MM1-OTA"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read().decode("utf-8", "replace")
            hdr = r.headers.get("X-MM1-BLACK", "")
    except Exception:
        return None
    try:
        st = json.loads(body)
    except json.JSONDecodeError:
        return None
    if hdr == "1" or st.get("mm1") is True or st.get("ssid") == "MM1-MIRA":
        st["_ip"] = ip
        return st
    if isinstance(st.get("dev"), str) and st["dev"].startswith("SAP6"):
        st["_ip"] = ip
        return st
    return None


def scan(timeout: float) -> list[dict]:
    hosts: list[str] = []
    for item in local_subnets():
        if "/" not in item:
            hosts.append(item)
            continue
        net = ipaddress.ip_network(item, strict=False)
        if net.num_addresses > 512:
            continue
        hosts.extend(str(h) for h in net.hosts())
    found: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
        futs = {ex.submit(probe, h, timeout): h for h in hosts}
        for fut in concurrent.futures.as_completed(futs):
            st = fut.result()
            if st:
                found.append(st)
    found.sort(key=lambda s: s.get("_ip", ""))
    return found


def flash(ip: str, bin_path: str, timeout: int) -> tuple[bool, str]:
    url = f"http://{ip}/update"
    size = os.path.getsize(bin_path)
    boundary = "----MM1OTA" + str(int(time.time()))
    head = (
        f"--{boundary}\r\n"
        'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode()
    tail = f"\r\n--{boundary}--\r\n".encode()
    with open(bin_path, "rb") as f:
        body = head + f.read() + tail
    req = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Content-Length": str(len(body)),
            "User-Agent": "MM1-OTA",
        },
    )
    print(f"  POST {url}  ({size} bytes)…", flush=True)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            msg = r.read().decode("utf-8", "replace")
            return True, msg or f"HTTP {r.status}"
    except urllib.error.HTTPError as e:
        return False, f"HTTP {e.code} {e.read().decode('utf-8', 'replace')}"
    except Exception as e:
        return False, str(e)


def main() -> int:
    ap = argparse.ArgumentParser(description="Flash MM1-BLACK over Join / SoftAP HTTP")
    ap.add_argument("--bin", default=DEFAULT_BIN, help="firmware.bin path")
    ap.add_argument("--timeout", type=float, default=0.35, help="scan probe timeout")
    ap.add_argument("--upload-timeout", type=int, default=180)
    ap.add_argument("--watch", action="store_true", help="keep scanning for new boards")
    ap.add_argument("--scan-only", action="store_true")
    args = ap.parse_args()

    if not args.scan_only and not os.path.isfile(args.bin):
        print(f"missing firmware: {args.bin}", file=sys.stderr)
        print("build first: pio run -e mm1_p4", file=sys.stderr)
        return 2

    flashed: set[str] = set()
    while True:
        print("Scanning for MM1-BLACK…", flush=True)
        devices = scan(args.timeout)
        if not devices:
            print("  none found (Join the lab AP, or enable SETUP → WiFi → AP)")
        for st in devices:
            ip = st["_ip"]
            print(
                f"  {ip}  fw={st.get('fw','?')}  board={st.get('board','?')}  "
                f"mac={st.get('btmac','?')}  pts={st.get('pts','?')}"
            )
            if args.scan_only or ip in flashed:
                continue
            ok, msg = flash(ip, args.bin, args.upload_timeout)
            print(f"    {'OK' if ok else 'FAIL'}  {msg}")
            if ok:
                flashed.add(ip)
        if not args.watch:
            return 0 if (args.scan_only or flashed or not devices) else 1
        time.sleep(4)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
