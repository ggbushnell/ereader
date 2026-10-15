#!/usr/bin/env python3
"""Network guard for the companion tools: where is this Mac right now?

Background (2026-10-02, UMN Security ticket #779032): Microsoft Defender for
Endpoint flagged this laptop for the companion's LAN servers, device
discovery and password-SSH to the Pi / Miyoo. Rule since then: none of this
runs on a University of Minnesota network, and the servers listen on
localhost unless we are on the home network and LAN mode is asked for.

Classes:
  umn      the active interface has a UMN address (10/8, 128.101/16,
           134.84/16, 160.94/16) -> every tool refuses to start
  home     address inside COMPANION_HOME_NETS (default 192.168.68.0/22)
  unknown  some other network (hotel, friend's house): localhost only
  offline  no default route: localhost only

Usage:
  python3 netguard.py            prints "<class> <ip>"; exit 2 on umn
  python3 netguard.py --bind     prints the bind address for a server:
                                 0.0.0.0 only if home AND COMPANION_LAN=1,
                                 else 127.0.0.1; exit 2 on umn
  NETGUARD_FAKE_IP=10.1.2.3 ...  pretend, for testing the guard itself
"""
import ipaddress, os, subprocess, sys

UMN_NETS = [ipaddress.ip_network(n) for n in
            ("10.0.0.0/8", "128.101.0.0/16", "134.84.0.0/16", "160.94.0.0/16")]
HOME_NETS = [ipaddress.ip_network(n.strip()) for n in
             os.environ.get("COMPANION_HOME_NETS", "192.168.68.0/22").split(",") if n.strip()]


def _run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=5).stdout
    except Exception:
        return ""


def current_ip():
    """IPv4 address of the default-route interface: macOS or Linux (incl. WSL2)."""
    fake = os.environ.get("NETGUARD_FAKE_IP")
    if fake:
        return fake
    if sys.platform == "darwin":
        iface = ""
        for line in _run(["route", "-n", "get", "default"]).splitlines():
            if "interface:" in line:
                iface = line.split(":", 1)[1].strip()
        return _run(["ipconfig", "getifaddr", iface]).strip() if iface else ""
    words = _run(["ip", "-4", "route", "get", "1.1.1.1"]).split()
    return words[words.index("src") + 1] if "src" in words else ""


def classify(ip=None):
    ip = current_ip() if ip is None else ip
    if not ip:
        return "offline", ""
    try:
        a = ipaddress.ip_address(ip)
    except ValueError:
        return "unknown", ip
    if any(a in n for n in UMN_NETS):
        return "umn", ip
    if any(a in n for n in HOME_NETS):
        return "home", ip
    return "unknown", ip


REFUSE = ("netguard: this Mac is on a University of Minnesota network ({ip}).\n"
          "netguard: the companion never runs on UMN networks (UMN Security #779032, 2026-10-02).\n"
          "netguard: nothing was started. Use it at home.")


def require_not_umn(tool="companion"):
    """Call from any tool before it touches the network. Exits on a UMN address."""
    cls, ip = classify()
    if cls == "umn":
        sys.exit(REFUSE.format(ip=ip))
    if cls != "home":
        print(f"netguard: {tool}: not on the home network ({cls} {ip or '-'}); "
              f"continuing, localhost/outbound only", file=sys.stderr, flush=True)
    return cls, ip


def bind_address():
    cls, ip = classify()
    if cls == "umn":
        sys.exit(REFUSE.format(ip=ip))
    want_lan = os.environ.get("COMPANION_LAN", "0") == "1"
    if want_lan and cls == "home":
        return "0.0.0.0", cls, ip
    if want_lan:
        print(f"netguard: COMPANION_LAN=1 ignored: not on the home network ({cls} {ip or '-'}); "
              f"binding to localhost", file=sys.stderr, flush=True)
    return "127.0.0.1", cls, ip


if __name__ == "__main__":
    if "--bind" in sys.argv:
        addr, cls, ip = bind_address()
        print(addr)
    else:
        cls, ip = classify()
        print(cls, ip or "-")
        if cls == "umn":
            sys.exit(REFUSE.format(ip=ip))
