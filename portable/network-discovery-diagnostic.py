#!/usr/bin/python3
"""Read-only LAN discovery diagnostic; never mounts shares or asks for credentials."""

import argparse
import concurrent.futures
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import socket
import subprocess
import sys
import time
from urllib.parse import unquote, urlsplit
import uuid
from xml.sax.saxutils import escape
import xml.etree.ElementTree as ET


DISCOVERY = "http://schemas.xmlsoap.org/ws/2005/04/discovery"
ADDRESSING = "http://schemas.xmlsoap.org/ws/2004/08/addressing"
TRANSFER_GET = "http://schemas.xmlsoap.org/ws/2004/09/transfer/Get"
MAX_METADATA_BYTES = 1024 * 1024
MAX_CHECKED_HOSTS = 256


def command(args, timeout):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout,
                          env=dict(os.environ, LC_ALL="C"), check=False)


def interfaces():
    if not shutil.which("ip"):
        raise RuntimeError("Missing 'ip' command (iproute2); cannot select LAN interfaces.")
    result = command(["ip", "-j", "-4", "address", "show", "up"], 5)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "ip address failed")
    found = []
    for item in json.loads(result.stdout):
        if "LOOPBACK" in item.get("flags", []):
            continue
        for addr in item.get("addr_info", []):
            if addr.get("family") != "inet" or addr.get("scope") != "global":
                continue
            ip = ipaddress.IPv4Address(addr["local"])
            if ip.is_loopback or ip.is_unspecified or ip.is_multicast:
                continue
            found.append({"name": item["ifname"], "address": str(ip),
                          "prefix": addr["prefixlen"], "broadcast": addr.get("broadcast")})
    return found


def gio_worker():
    try:
        import gi
    except ImportError:
        print(json.dumps({"status": "unavailable", "errors": [
            "Missing Python GObject bindings; run with /usr/bin/python3 and python3-gi installed."],
                          "entries": []}))
        return
    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib

    schemes = list(Gio.Vfs.get_default().get_supported_uri_schemes())
    entries, errors = [], []
    attempted = 0
    for uri in ("network:///", "dns-sd://local/", "smb:///"):
        if urlsplit(uri).scheme not in schemes:
            errors.append(uri + ": host GIO scheme is unavailable")
            continue
        attempted += 1
        enumerator = None
        try:
            root = Gio.File.new_for_uri(uri)
            enumerator = root.enumerate_children(
                "standard::name,standard::display-name,standard::type,standard::target-uri",
                Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, None)
            while True:
                info = enumerator.next_file(None)
                if info is None:
                    break
                target = info.get_attribute_string("standard::target-uri")
                location = target or root.get_child(info.get_name()).get_uri()
                parsed = urlsplit(location)
                entries.append({"name": info.get_display_name(), "uri": location,
                                "source_uri": uri, "file_type": int(info.get_file_type()),
                                "kind": "smb-server" if parsed.scheme == "smb" and parsed.hostname
                                else "network-entry",
                                "host": parsed.hostname or ""})
        except GLib.Error as error:
            errors.append(uri + ": " + error.message)
        finally:
            if enumerator is not None:
                try:
                    enumerator.close(None)
                except GLib.Error as error:
                    errors.append(uri + ": close failed: " + error.message)
    print(json.dumps({"status": "partial" if entries and errors else
                     "error" if errors and attempted else
                     "unavailable" if not attempted else "ok",
                     "schemes": schemes, "entries": entries, "errors": errors}))


def gio_discovery(timeout):
    result = command([sys.executable, str(Path(__file__).resolve()), "--gio-worker"], timeout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "Host-GIO worker failed")
    return json.loads(result.stdout)


def probe(message_id):
    return (f'<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
            f'xmlns:a="{ADDRESSING}" xmlns:d="{DISCOVERY}" '
            'xmlns:p="http://schemas.xmlsoap.org/ws/2006/02/devprof">'
            '<s:Header><a:Action>' + DISCOVERY + '/Probe</a:Action>'
            f'<a:MessageID>{message_id}</a:MessageID>'
            '<a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>'
            '</s:Header><s:Body><d:Probe><d:Types>p:Device</d:Types>'
            '</d:Probe></s:Body></s:Envelope>').encode("ascii")


def safe_xml(data):
    text = data.decode("utf-8")
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
        raise ValueError("DTD/entity declarations are not accepted")
    return ET.fromstring(text)


def parse_probe_matches(data, expected_id, peer, interface):
    root = safe_xml(data)
    if root.findtext(".//{" + ADDRESSING + "}RelatesTo") != expected_id:
        return []
    entries = []
    for match in root.findall(".//{" + DISCOVERY + "}ProbeMatch"):
        endpoint = match.findtext(".//{" + ADDRESSING + "}Address", "")
        types = match.findtext("{" + DISCOVERY + "}Types", "")
        scopes = match.findtext("{" + DISCOVERY + "}Scopes", "")
        names = [unquote(scope.rsplit("/", 1)[-1]) for scope in scopes.split()
                 if "/name/" in scope]
        entries.append({"name": names[0] if names else peer, "address": peer,
                        "interface": interface, "endpoint": endpoint, "types": types,
                        "scopes": scopes, "metadata_urls":
                        match.findtext("{" + DISCOVERY + "}XAddrs", "").split(),
                        "kind": "ws-discovery-candidate",
                        "note": "Device discovery only; SMB service not yet verified."})
    return entries


def ws_discovery(selected, timeout):
    entries, errors, seen = [], [], set()
    message_id = "urn:uuid:" + str(uuid.uuid4())
    packet = probe(message_id)
    with selectors.DefaultSelector() as selector:
        sockets = []
        try:
            for iface in selected:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sockets.append(sock)
                try:
                    sock.bind((iface["address"], 0))
                    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                                    socket.inet_aton(iface["address"]))
                    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
                    sock.setblocking(False)
                    sock.sendto(packet, ("239.255.255.250", 3702))
                    selector.register(sock, selectors.EVENT_READ, iface)
                except OSError as error:
                    errors.append(iface["name"] + ": " + str(error))
            started = time.monotonic()
            deadline = started + timeout
            retry_at = started + timeout / 2
            retried = False
            while selector.get_map() and time.monotonic() < deadline:
                now = time.monotonic()
                if not retried and now >= retry_at:
                    for key in list(selector.get_map().values()):
                        try:
                            key.fileobj.sendto(packet, ("239.255.255.250", 3702))
                        except OSError as error:
                            errors.append(key.data["name"] + ": retry failed: " + str(error))
                    retried = True
                wait = deadline - now if retried else min(deadline - now, max(0, retry_at - now))
                for key, _ in selector.select(max(0, wait)):
                    try:
                        data, sender = key.fileobj.recvfrom(65535)
                        for entry in parse_probe_matches(data, message_id, sender[0], key.data["name"]):
                            identity = (entry["endpoint"], entry["address"], entry["interface"])
                            if identity not in seen:
                                seen.add(identity)
                                entry["reply_seconds"] = round(time.monotonic() - started, 3)
                                entry["after_retry"] = retried
                                entries.append(entry)
                    except (OSError, ET.ParseError, ValueError) as error:
                        errors.append(key.data["name"] + ": invalid/failed response: " + str(error))
        finally:
            for sock in sockets:
                sock.close()
    return {"status": "partial" if entries and errors else "error" if errors else "ok",
            "entries": entries, "errors": errors}


def metadata_get_request(endpoint, message_id, source_id):
    return (f'<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
            f'xmlns:a="{ADDRESSING}"><s:Header>'
            f'<a:To>{escape(endpoint)}</a:To>'
            f'<a:Action>{TRANSFER_GET}</a:Action>'
            f'<a:MessageID>{message_id}</a:MessageID>'
            f'<a:ReplyTo><a:Address>{ADDRESSING}/role/anonymous</a:Address></a:ReplyTo>'
            f'<a:From><a:Address>{source_id}</a:Address></a:From>'
            '</s:Header><s:Body/></s:Envelope>').encode("utf-8")


def parse_computer(value):
    """Split Windows pub:Computer text such as NAME/Workgroup:GROUP or NAME\\Domain:DOM."""
    value = value.strip()
    match = re.match(r"^([^/\\]+)[/\\]([^:]*)(?::(.*))?$", value)
    if not match:
        return {"computer": value, "name": value, "membership": "", "group": ""}
    return {"computer": value, "name": match.group(1).strip(),
            "membership": match.group(2).strip(), "group": (match.group(3) or "").strip()}


def parse_metadata(data):
    root = safe_xml(data)
    result = {}
    for element in root.iter():
        local = element.tag.rsplit("}", 1)[-1]
        if local == "Host":
            for child in element.iter():
                if child.tag.rsplit("}", 1)[-1] == "Computer" and (child.text or "").strip():
                    result.update(parse_computer(child.text))
                    break
        elif local == "FriendlyName" and "friendly_name" not in result and element.text:
            result["friendly_name"] = element.text.strip()
    if "name" not in result:
        raise ValueError("Metadata reply has no Relationship/Host/Computer value")
    return result


def wsd_metadata(entry, timeout):
    """WS-Transfer Get on the advertised XAddrs URL, restricted to the replying address."""
    errors = []
    for url in entry.get("metadata_urls", []):
        parts = urlsplit(url)
        try:
            same_host = parts.hostname and \
                ipaddress.ip_address(parts.hostname) == ipaddress.ip_address(entry["address"])
        except ValueError:
            same_host = False
        if parts.scheme != "http" or not same_host:
            errors.append(url + ": skipped (only http on the replying address is fetched)")
            continue
        started = time.monotonic()
        connection = http.client.HTTPConnection(parts.hostname, parts.port or 80, timeout=timeout)
        try:
            connection.request("POST", (parts.path or "/") + ("?" + parts.query if parts.query else ""),
                               metadata_get_request(entry["endpoint"],
                                                    "urn:uuid:" + str(uuid.uuid4()),
                                                    "urn:uuid:" + str(uuid.uuid4())),
                               {"Content-Type": "application/soap+xml; charset=utf-8",
                                "User-Agent": "swordfish-discovery-diagnostic"})
            response = connection.getresponse()
            data = response.read(MAX_METADATA_BYTES + 1)
            if len(data) > MAX_METADATA_BYTES:
                raise ValueError("metadata reply too large")
            if response.status != 200:
                raise ValueError(f"HTTP {response.status} {response.reason}")
            result = parse_metadata(data)
            result.update({"status": "ok", "url": url,
                           "elapsed_seconds": round(time.monotonic() - started, 3)})
            return result
        except (OSError, http.client.HTTPException, ET.ParseError, ValueError) as error:
            errors.append(url + ": " + (str(error) or type(error).__name__))
        finally:
            connection.close()
    return {"status": "error", "errors": errors or ["No metadata URL advertised"]}


def tcp_port_check(address, port, timeout):
    """TCP connect only; the socket is closed without sending any SMB data."""
    started = time.monotonic()
    try:
        with socket.create_connection((address, port), timeout=timeout):
            state = "open"
    except ConnectionRefusedError:
        state = "refused"
    except socket.timeout:
        state = "no-answer"
    except OSError as error:
        state = "error: " + str(error)
    return {"port": port, "state": state, "elapsed_seconds": round(time.monotonic() - started, 3)}


def verify_candidates(methods, timeout):
    """Name WSD devices and check TCP 445 on IPv4 candidates from WSD/NetBIOS."""
    wsd_entries = methods.get("ws-discovery", {}).get("entries", [])
    addresses = []
    for method in ("ws-discovery", "netbios"):
        for entry in methods.get(method, {}).get("entries", []):
            if entry.get("address") and entry["address"] not in addresses:
                addresses.append(entry["address"])
    skipped = addresses[MAX_CHECKED_HOSTS:]
    addresses = addresses[:MAX_CHECKED_HOSTS]
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        metadata = [pool.submit(wsd_metadata, entry, timeout) for entry in wsd_entries]
        ports = {address: pool.submit(tcp_port_check, address, 445, timeout)
                 for address in addresses}
        for entry, future in zip(wsd_entries, metadata):
            entry["metadata"] = future.result()
            if entry["metadata"]["status"] == "ok":
                entry["name"] = entry["metadata"]["name"]
        smb_ports = {address: future.result() for address, future in ports.items()}
    for method in ("ws-discovery", "netbios"):
        for entry in methods.get(method, {}).get("entries", []):
            if entry.get("address") in smb_ports:
                entry["smb_tcp_445"] = smb_ports[entry["address"]]["state"]
    return {"smb_tcp_445": smb_ports,
            "skipped_hosts": skipped}


def parse_nmblookup(text):
    found = set()
    for line in text.splitlines():
        match = re.match(r"^(\d+\.\d+\.\d+\.\d+)\s+.*<00>", line)
        if match:
            found.add(str(ipaddress.IPv4Address(match.group(1))))
    return sorted(found)


def netbios_discovery(selected, timeout):
    if not shutil.which("nmblookup"):
        return {"status": "unavailable", "entries": [], "errors": [
            "Missing nmblookup (usually the samba-common-bin package)."]}
    entries, errors = [], []
    deadline = time.monotonic() + timeout
    for iface in selected:
        if not iface["broadcast"]:
            errors.append(iface["name"] + ": no IPv4 broadcast address; not probed")
            continue
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            errors.append(iface["name"] + ": method deadline reached before probing")
            continue
        try:
            result = command(["nmblookup", "-B", iface["broadcast"], "*"], remaining)
            addresses = parse_nmblookup(result.stdout)
            if result.returncode:
                errors.append(iface["name"] + ": " +
                              (result.stderr.strip() or result.stdout.strip() or
                               "nmblookup failed with exit " + str(result.returncode)))
            for address in addresses:
                name = address
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    try:
                        status = command(["nmblookup", "-A", address], min(2, remaining))
                        if status.returncode:
                            errors.append(address + ": node-status lookup failed: " +
                                          (status.stderr.strip() or status.stdout.strip()))
                        for line in status.stdout.splitlines():
                            match = re.match(r"^\s*(.+?)\s+<00>\s+-\s+.*$", line)
                            if match and "<GROUP>" not in line:
                                name = match.group(1).strip()
                                break
                    except subprocess.TimeoutExpired:
                        errors.append(address + ": node-status lookup timed out")
                else:
                    errors.append(address + ": deadline reached before name lookup")
                entries.append({"name": name, "address": address, "interface": iface["name"],
                                "kind": "netbios-candidate",
                                "note": "NetBIOS responder; SMB service not yet verified."})
        except subprocess.TimeoutExpired:
            errors.append(iface["name"] + ": NetBIOS broadcast timed out")
    return {"status": "partial" if entries and errors else "error" if errors else "ok",
            "entries": entries, "errors": errors}


def run_method(function, *args):
    started = time.monotonic()
    try:
        result = function(*args)
    except subprocess.TimeoutExpired:
        result = {"status": "error", "entries": [], "errors": ["Method deadline exceeded."]}
    except (OSError, RuntimeError, ValueError) as error:
        result = {"status": "error", "entries": [], "errors": [str(error)]}
    result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    return result


def summarize(methods):
    servers = {}
    for method, result in methods.items():
        for entry in result["entries"]:
            address = entry.get("address") or entry.get("host")
            if not address:
                continue
            # Only merge exact identities; resolving names could misidentify multihomed hosts.
            key = (address.casefold().rstrip("."), entry.get("interface", ""))
            server = servers.setdefault(key, {"host": address, "names": [], "methods": [],
                                              "interface": key[1]})
            if entry["name"] not in server["names"]:
                server["names"].append(entry["name"])
            if method not in server["methods"]:
                server["methods"].append(method)
            if entry.get("smb_tcp_445"):
                server["smb_tcp_445"] = entry["smb_tcp_445"]
            metadata = entry.get("metadata", {})
            if metadata.get("group"):
                server["group"] = metadata["membership"] + ":" + metadata["group"]
    return list(servers.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", action="append", default=[],
                        help="LAN interface name; repeat to select more than one")
    parser.add_argument("--timeout", type=int, choices=range(2, 31), default=8,
                        metavar="2..30", help="deadline per method in seconds (default: 8)")
    parser.add_argument("--output", type=Path, help="write JSON report to a NEW file")
    parser.add_argument("--no-checks", action="store_true",
                        help="skip WSD name lookup and the TCP-only port 445 check")
    parser.add_argument("--gio-worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.gio_worker:
        gio_worker()
        return 0
    if any(os.environ.get(key) for key in ("APPDIR", "GIO_MODULE_DIR", "LD_LIBRARY_PATH")):
        parser.error("Run from a normal host terminal, not inside the portable app environment.")
    try:
        available = interfaces()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(2, "Interface detection failed: " + str(error) + "\n")
    unknown = set(args.interface) - {item["name"] for item in available}
    if unknown:
        parser.error("No active IPv4 LAN address for: " + ", ".join(sorted(unknown)))
    selected = [item for item in available if not args.interface or item["name"] in args.interface]
    if not selected:
        parser.error("No active IPv4 LAN interface found.")
    print("Discovery only: no mounts, credentials, share enumeration or SMB1 connections.",
          file=sys.stderr)
    print("Probing interfaces: " + ", ".join(item["name"] for item in selected), file=sys.stderr)
    print("GVFS uses the desktop's interfaces; --interface limits only WSD/NetBIOS.",
          file=sys.stderr)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = {"gvfs": pool.submit(run_method, gio_discovery, args.timeout),
                   "ws-discovery": pool.submit(run_method, ws_discovery, selected, args.timeout),
                   "netbios": pool.submit(run_method, netbios_discovery, selected, args.timeout)}
        methods = {name: future.result() for name, future in futures.items()}
    checks = {"skipped": True}
    if not args.no_checks:
        started = time.monotonic()
        checks = verify_candidates(methods, min(5, args.timeout))
        checks["elapsed_seconds"] = round(time.monotonic() - started, 3)
    report = {"format": "swordfish-discovery-diagnostic-v2", "interfaces": selected,
              "methods": methods, "checks": checks, "servers": summarize(methods),
              "notes": ["IPv4 diagnostic; IPv6-only hosts are not covered.",
                        "No reply does not prove a host is absent.",
                        "WSD and NetBIOS candidates are not confirmed SMB servers.",
                        "Port 445 check is a TCP connect only; no SMB data or login is sent.",
                        "GVFS virtual roots must already be available; no mount is attempted.",
                        "Hostname/IP aliases are retained unless exact identities match."]}
    text = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    if args.output:
        try:
            with args.output.open("x", encoding="utf-8") as output:
                output.write(text)
        except OSError as error:
            parser.exit(2, "Cannot create report: " + str(error) + "\n")
    print(text, end="")
    return 0 if any(result["status"] in ("ok", "partial") for result in methods.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
