#!/usr/bin/python3
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import ModuleType, SimpleNamespace


spec = importlib.util.spec_from_file_location(
    "diagnostic", Path(__file__).resolve().parents[1] / "portable/network-discovery-diagnostic.py")
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)


class DiscoveryDiagnosticTests(unittest.TestCase):
    def test_gio_uses_host_dns_sd_scheme(self):
        visited = []

        class Enumerator:
            def next_file(self, cancellable):
                return None

            def close(self, cancellable):
                return True

        class File:
            @staticmethod
            def new_for_uri(uri):
                visited.append(uri)
                return File()

            def enumerate_children(self, *args):
                return Enumerator()

        gi = ModuleType("gi")
        gi.require_version = lambda *args: None
        repository = ModuleType("gi.repository")
        repository.Gio = SimpleNamespace(
            Vfs=SimpleNamespace(get_default=lambda: SimpleNamespace(
                get_supported_uri_schemes=lambda: ["network", "dns-sd", "smb"])),
            File=File, FileQueryInfoFlags=SimpleNamespace(NOFOLLOW_SYMLINKS=1))
        repository.GLib = SimpleNamespace(Error=RuntimeError)
        output = io.StringIO()
        with patch.dict(diagnostic.sys.modules, {"gi": gi, "gi.repository": repository}), \
                contextlib.redirect_stdout(output):
            diagnostic.gio_worker()
        self.assertEqual(visited, ["network:///", "dns-sd://local/", "smb:///"])
        self.assertEqual(json.loads(output.getvalue())["status"], "ok")

    def test_probe_and_response(self):
        request = diagnostic.ET.fromstring(diagnostic.probe("urn:uuid:test"))
        self.assertEqual(request.findtext(".//{" + diagnostic.ADDRESSING + "}MessageID"),
                         "urn:uuid:test")
        reply = (
            f'<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
            f'xmlns:a="{diagnostic.ADDRESSING}" xmlns:d="{diagnostic.DISCOVERY}">'
            '<s:Header><a:RelatesTo>urn:uuid:test</a:RelatesTo></s:Header>'
            '<s:Body><d:ProbeMatches><d:ProbeMatch><a:EndpointReference>'
            '<a:Address>urn:uuid:server</a:Address></a:EndpointReference>'
            '<d:Types>p:Device</d:Types><d:Scopes>urn:test/name/Office%20PC</d:Scopes>'
            '<d:XAddrs>http://192.0.2.4:5357/server</d:XAddrs>'
            '</d:ProbeMatch></d:ProbeMatches></s:Body></s:Envelope>').encode()
        entries = diagnostic.parse_probe_matches(reply, "urn:uuid:test", "192.0.2.4", "eth0")
        self.assertEqual(entries[0]["name"], "Office PC")
        self.assertEqual(entries[0]["kind"], "ws-discovery-candidate")
        self.assertEqual(entries[0]["metadata_urls"], ["http://192.0.2.4:5357/server"])
        self.assertEqual(diagnostic.parse_probe_matches(reply, "other", "192.0.2.4", "eth0"), [])
        with self.assertRaises(ValueError):
            diagnostic.parse_probe_matches(b"<!DOCTYPE x><x/>", "", "", "")

    def test_netbios_output(self):
        self.assertEqual(diagnostic.parse_nmblookup(
            "querying * on 192.0.2.255\n192.0.2.4 *<00>\n192.0.2.4 *<00>\n"), ["192.0.2.4"])
        self.assertEqual(diagnostic.parse_nmblookup("name_query failed to find name *"), [])

    def test_interface_selection(self):
        result = subprocess.CompletedProcess([], 0, json.dumps([
            {"ifname": "lo", "flags": ["LOOPBACK"], "addr_info": []},
            {"ifname": "eth0", "flags": ["UP"], "addr_info": [
                {"family": "inet", "scope": "global", "local": "192.0.2.5",
                 "prefixlen": 24, "broadcast": "192.0.2.255"}]}]), "")
        with patch.object(diagnostic.shutil, "which", return_value="/usr/sbin/ip"), \
                patch.object(diagnostic, "command", return_value=result):
            selected = diagnostic.interfaces()
        self.assertEqual(len(selected), 1)
        self.assertEqual(selected[0]["broadcast"], "192.0.2.255")

    def test_explicit_failures(self):
        with patch.object(diagnostic.shutil, "which", return_value=None):
            self.assertEqual(diagnostic.netbios_discovery([], 2)["status"], "unavailable")
        with patch.object(diagnostic, "command", side_effect=subprocess.TimeoutExpired("worker", 2)):
            result = diagnostic.run_method(diagnostic.gio_discovery, 2)
        self.assertEqual(result["status"], "error")
        self.assertIn("deadline", result["errors"][0])
        result = diagnostic.run_method(lambda: (_ for _ in ()).throw(OSError("socket denied")))
        self.assertEqual(result["errors"], ["socket denied"])

    def test_netbios_does_not_mount_or_scan_ranges(self):
        calls = []

        def command(args, timeout):
            calls.append(args)
            return subprocess.CompletedProcess(args, 0, (
                "192.0.2.4 *<00>\n" if "-B" in args else
                "Looking up status\n\tOFFICE <00> -         B <ACTIVE>\n"), "")

        with patch.object(diagnostic.shutil, "which", return_value="/usr/bin/nmblookup"), \
                patch.object(diagnostic, "command", side_effect=command):
            result = diagnostic.netbios_discovery(
                [{"name": "eth0", "broadcast": "192.0.2.255"}], 2)
        self.assertEqual(result["entries"][0]["name"], "OFFICE")
        self.assertEqual(calls, [["nmblookup", "-B", "192.0.2.255", "*"],
                                 ["nmblookup", "-A", "192.0.2.4"]])

    def test_merge_preserves_interfaces_and_methods(self):
        item = {"name": "OFFICE", "address": "192.0.2.4", "interface": "eth0"}
        methods = {"netbios": {"entries": [item, item]},
                   "ws-discovery": {"entries": [dict(item, name="192.0.2.4")]}}
        summary = diagnostic.summarize(methods)
        self.assertEqual(len(summary), 1)
        self.assertEqual(summary[0]["methods"], ["netbios", "ws-discovery"])
        methods["netbios"]["entries"].append(dict(item, interface="wlan0"))
        self.assertEqual(len(diagnostic.summarize(methods)), 2)

    def test_ws_socket_failure_is_explicit(self):
        class FailingSocket:
            closed = False

            def bind(self, address):
                raise OSError("interface unavailable")

            def close(self):
                self.closed = True

        sock = FailingSocket()
        with patch.object(diagnostic.socket, "socket", return_value=sock):
            result = diagnostic.ws_discovery([{"name": "eth0", "address": "192.0.2.5"}], 2)
        self.assertTrue(sock.closed)
        self.assertEqual(result["status"], "error")
        self.assertIn("interface unavailable", result["errors"][0])

    def test_ws_deadline_and_retry(self):
        class FakeSocket:
            packets = []
            closed = False

            def bind(self, address):
                self.bound = address

            def setsockopt(self, *args):
                pass

            def setblocking(self, flag):
                pass

            def sendto(self, packet, address):
                self.packets.append((packet, address))

            def close(self):
                self.closed = True

        class FakeSelector:
            keys = {}
            clock = 0

            def __enter__(self):
                return self

            def __exit__(self, *args):
                pass

            def register(self, sock, events, data):
                from types import SimpleNamespace
                self.keys[sock] = SimpleNamespace(fileobj=sock, data=data)

            def get_map(self):
                return self.keys

            def select(self, timeout):
                self.clock += timeout
                return []

        sock, selector = FakeSocket(), FakeSelector()
        with patch.object(diagnostic.socket, "socket", return_value=sock), \
                patch.object(diagnostic.selectors, "DefaultSelector", return_value=selector), \
                patch.object(diagnostic.time, "monotonic", side_effect=lambda: selector.clock):
            result = diagnostic.ws_discovery([{"name": "eth0", "address": "192.0.2.5"}], 2)
        self.assertEqual(selector.clock, 2)
        self.assertEqual(len(sock.packets), 2)
        self.assertEqual(sock.packets[0][1], ("239.255.255.250", 3702))
        self.assertTrue(sock.closed)
        self.assertEqual(result["entries"], [])
        self.assertEqual(result["status"], "ok")

    def test_netbios_no_response_and_timeout_are_not_success(self):
        iface = {"name": "eth0", "broadcast": "192.0.2.255"}
        with patch.object(diagnostic.shutil, "which", return_value="/usr/bin/nmblookup"), \
                patch.object(diagnostic, "command", return_value=subprocess.CompletedProcess(
                    [], 1, "name_query failed to find name *", "")):
            result = diagnostic.netbios_discovery([iface], 2)
        self.assertEqual(result["status"], "error")
        self.assertIn("name_query", result["errors"][0])
        with patch.object(diagnostic.shutil, "which", return_value="/usr/bin/nmblookup"), \
                patch.object(diagnostic, "command", side_effect=subprocess.TimeoutExpired("lookup", 2)):
            result = diagnostic.netbios_discovery([iface], 2)
        self.assertEqual(result["status"], "error")
        self.assertIn("timed out", result["errors"][0])

    def test_cli_report_and_no_overwrite(self):
        iface = {"name": "eth0", "address": "192.0.2.5", "broadcast": "192.0.2.255"}
        empty = {"status": "ok", "entries": [], "errors": []}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            with patch.object(diagnostic.sys, "argv", ["diagnostic", "--output", str(path)]), \
                    patch.dict(diagnostic.os.environ, {}, clear=True), \
                    patch.object(diagnostic, "interfaces", return_value=[iface]), \
                    patch.object(diagnostic, "gio_discovery", return_value=dict(empty)), \
                    patch.object(diagnostic, "ws_discovery", return_value=dict(empty)), \
                    patch.object(diagnostic, "netbios_discovery", return_value=dict(empty)), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(diagnostic.main(), 0)
                report = json.loads(path.read_text())
                self.assertEqual(set(report["methods"]), {"gvfs", "ws-discovery", "netbios"})
                self.assertEqual(report["servers"], [])
                with self.assertRaises(SystemExit) as failure:
                    diagnostic.main()
                self.assertEqual(failure.exception.code, 2)
                self.assertEqual(json.loads(path.read_text()), report)

    def test_cli_invalid_interface_sends_no_probes(self):
        with patch.object(diagnostic.sys, "argv", ["diagnostic", "--interface", "missing"]), \
                patch.dict(diagnostic.os.environ, {}, clear=True), \
                patch.object(diagnostic, "interfaces", return_value=[]), \
                patch.object(diagnostic, "ws_discovery") as ws, \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                diagnostic.main()
            ws.assert_not_called()

    WINDOWS_METADATA = (
        '<soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope" '
        'xmlns:wsx="http://schemas.xmlsoap.org/ws/2004/09/mex" '
        'xmlns:wsdp="http://schemas.xmlsoap.org/ws/2006/02/devprof" '
        'xmlns:pub="http://schemas.microsoft.com/windows/pub/2005/07">'
        '<soap:Body><wsx:Metadata><wsx:MetadataSection>'
        '<wsdp:ThisModel><wsdp:FriendlyName>Microsoft Publication Service Device Host'
        '</wsdp:FriendlyName></wsdp:ThisModel></wsx:MetadataSection><wsx:MetadataSection>'
        '<wsdp:Relationship><wsdp:Host><wsdp:Types>pub:Computer</wsdp:Types>'
        '<pub:Computer>WIN7-PC/Workgroup:WORKGROUP</pub:Computer></wsdp:Host>'
        '</wsdp:Relationship></wsx:MetadataSection></wsx:Metadata></soap:Body>'
        '</soap:Envelope>').encode()

    def test_parse_windows_computer_values(self):
        self.assertEqual(diagnostic.parse_computer("WIN7-PC/Workgroup:WORKGROUP"),
                         {"computer": "WIN7-PC/Workgroup:WORKGROUP", "name": "WIN7-PC",
                          "membership": "Workgroup", "group": "WORKGROUP"})
        self.assertEqual(diagnostic.parse_computer("PC\\Domain:CORP")["group"], "CORP")
        self.assertEqual(diagnostic.parse_computer("PC/NotJoined")["name"], "PC")
        self.assertEqual(diagnostic.parse_metadata(self.WINDOWS_METADATA)["name"], "WIN7-PC")
        with self.assertRaises(ValueError):
            diagnostic.parse_metadata(b'<!DOCTYPE x [<!ENTITY a "b">]><x>&a;</x>')

    def test_metadata_get_against_local_server(self):
        import http.server
        import threading
        received = {}
        payload = self.WINDOWS_METADATA

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                received["type"] = self.headers["Content-Type"]
                received["body"] = self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(200)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

            def log_message(self, *args):
                pass

        server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            port = server.server_address[1]
            entry = {"address": "127.0.0.1", "endpoint": "urn:uuid:device&1",
                     "metadata_urls": ["http://198.51.100.9:5357/x/",
                                       f"http://127.0.0.1:{port}/device/"]}
            result = diagnostic.wsd_metadata(entry, 2)
        finally:
            server.shutdown()
            server.server_close()
        self.assertEqual(result["status"], "ok")
        self.assertEqual((result["name"], result["group"]), ("WIN7-PC", "WORKGROUP"))
        self.assertTrue(received["type"].startswith("application/soap+xml"))
        request = diagnostic.ET.fromstring(received["body"])
        self.assertEqual(request.findtext(".//{" + diagnostic.ADDRESSING + "}To"),
                         "urn:uuid:device&1")
        self.assertEqual(request.findtext(".//{" + diagnostic.ADDRESSING + "}Action"),
                         diagnostic.TRANSFER_GET)

    def test_metadata_refuses_other_hosts_and_schemes(self):
        entry = {"address": "192.0.2.4", "endpoint": "urn:uuid:x",
                 "metadata_urls": ["http://192.0.2.99:5357/", "https://192.0.2.4/",
                                   "http://example.invalid/"]}
        with patch.object(diagnostic.http.client, "HTTPConnection") as connection:
            result = diagnostic.wsd_metadata(entry, 1)
        connection.assert_not_called()
        self.assertEqual(result["status"], "error")
        self.assertEqual(len(result["errors"]), 3)

    def test_tcp_port_check_open_and_refused(self):
        listener = diagnostic.socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        try:
            self.assertEqual(diagnostic.tcp_port_check("127.0.0.1", port, 1)["state"], "open")
        finally:
            listener.close()
        self.assertEqual(diagnostic.tcp_port_check("127.0.0.1", port, 1)["state"], "refused")

    def test_checks_name_wsd_device_and_mark_port(self):
        wsd = {"name": "192.0.2.4", "address": "192.0.2.4", "interface": "eth0",
               "endpoint": "urn:uuid:x", "metadata_urls": []}
        methods = {"ws-discovery": {"entries": [wsd]},
                   "netbios": {"entries": [{"name": "NB", "address": "192.0.2.5",
                                            "interface": "eth0"}]}}
        metadata = {"status": "ok", "name": "WIN7-PC", "membership": "Workgroup",
                    "group": "WORKGROUP"}
        with patch.object(diagnostic, "wsd_metadata", return_value=metadata), \
                patch.object(diagnostic, "tcp_port_check",
                             side_effect=lambda address, port, timeout: {
                                 "port": port, "state": "open" if address == "192.0.2.4"
                                 else "refused"}) as check:
            checks = diagnostic.verify_candidates(methods, 2)
        self.assertEqual({call.args[1] for call in check.call_args_list}, {445})
        self.assertEqual(checks["smb_tcp_445"]["192.0.2.5"]["state"], "refused")
        servers = diagnostic.summarize(methods)
        self.assertEqual(servers[0]["names"], ["WIN7-PC"])
        self.assertEqual(servers[0]["smb_tcp_445"], "open")
        self.assertEqual(servers[0]["group"], "Workgroup:WORKGROUP")
        self.assertEqual(servers[1]["smb_tcp_445"], "refused")


if __name__ == "__main__":
    unittest.main()
