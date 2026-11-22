import http.server
import json
import sys
import tempfile
import threading
import types
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from Wireless3DPrinter import client


class Handler(http.server.BaseHTTPRequestHandler):
    status = 204
    info = {"type": "wireless-3d-printer", "id": "wireless-3d-printer-123", "name": "Workshop", "api_version": 1}
    uploads = []
    files = []
    file_status = 200
    deletes = []
    bulk_deletes = []

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/api/files":
            data = json.dumps(self.files).encode()
            self.send_response(self.file_status)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if self.path != "/api/device":
            self.send_error(404)
            return
        data = json.dumps(self.info).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        data = self.rfile.read(int(self.headers["Content-Length"]))
        if self.path == "/api/files/delete":
            self.bulk_deletes.append((self.headers, data))
            self.send_response(self.file_status)
            self.end_headers()
            self.wfile.write(b"Confirmed 1 of 2 deletions. Failed at TWO.GCO: printer busy"
                             if self.file_status != 200 else b"deleted")
            return
        self.uploads.append((self.path, self.headers, data))
        self.send_response(self.status)
        self.end_headers()

    def do_DELETE(self):
        self.deletes.append(self.path)
        self.send_response(self.file_status)
        self.end_headers()
        self.wfile.write(b"printer busy" if self.file_status != 200 else b"deleted")


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        Handler.status = 204
        Handler.info = {"type": "wireless-3d-printer", "id": "wireless-3d-printer-123", "name": "Workshop", "api_version": 1}
        Handler.uploads = []
        Handler.files = []
        Handler.file_status = 200
        Handler.deletes = []
        Handler.bulk_deletes = []
        self.device = client.Device("wireless-3d-printer-123", "Workshop", "127.0.0.1", self.server.server_port)

    def test_filename_is_safe_and_has_one_extension(self):
        self.assertEqual(client.safe_filename("../../bad\"\r\nname.GCODE"), "bad___name.gcode")
        self.assertEqual(client.safe_filename("..."), "untitled_print.gcode")
        self.assertEqual(client.safe_filename("example.gcode"), "example.gcode")
        self.assertEqual(len(client.safe_filename("a" * 200)), 106)

    def test_identity_validation_and_upload(self):
        self.assertEqual(client.device_at("127.0.0.1", self.server.server_port), self.device)
        data = b"G28\nG1 X12 Y34\n" * 4000
        progress = []
        with tempfile.NamedTemporaryFile() as f:
            f.write(data)
            f.flush()
            client.upload(self.device, f.name, "sample.gcode", progress.append)
        path, headers, body = Handler.uploads[0]
        self.assertEqual(headers["Content-Type"], "application/octet-stream")
        self.assertEqual(path, "/upload?name=sample.gcode")
        self.assertEqual(len(body), int(headers["Content-Length"]))
        self.assertEqual(body, data)
        self.assertEqual(progress[-1], 100)
        self.assertEqual(progress, sorted(progress))

    def test_rejects_wrong_identity_and_upload_http_failure(self):
        Handler.info["type"] = "other"
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            client.device_at("127.0.0.1", self.server.server_port)
        Handler.status = 500
        with tempfile.NamedTemporaryFile() as f:
            f.write(b"G28\n")
            f.flush()
            with self.assertRaisesRegex(RuntimeError, "HTTP 500"):
                client.upload(self.device, f.name, "sample", lambda _: None)

    def test_discovery_resolves_removes_and_deduplicates(self):
        device = self.device

        class FakeZeroconf:
            closed = False

            def get_service_info(self, service, name, timeout):
                self.service = service
                return types.SimpleNamespace(port=device.port, parsed_addresses=lambda: ["127.0.0.1"])

            def close(self):
                self.closed = True

        class FakeBrowser:
            def __init__(self, zc, service, listener):
                self.zc = zc
                self.listener = listener
                listener.add_service(zc, service, "first")
                listener.add_service(zc, service, "second")
                listener.add_service(zc, service, "removed")
                listener.remove_service(zc, service, "removed")

            def cancel(self):
                self.cancelled = True

        zc = FakeZeroconf()
        module = types.SimpleNamespace(Zeroconf=lambda: zc, ServiceBrowser=FakeBrowser)
        with patch.dict(sys.modules, {"zeroconf": module}):
            self.assertEqual(client.discover(0), [device])
        self.assertEqual(zc.service, "_wireless-3dp._tcp.local.")
        self.assertTrue(zc.closed)

    def test_discovery_rejects_unrelated_or_unreachable_devices(self):
        Handler.info["api_version"] = 2
        with self.assertRaises(ValueError):
            client.device_at("127.0.0.1", self.server.server_port)
        with self.assertRaises(ValueError):
            client.device_at("224.0.0.1", 80)

    def test_list_and_delete_use_printer_path(self):
        Handler.files = [{"name": "Long name.gcode", "path": "DIR/A B.GCO", "size": 123}]
        self.assertEqual(client.list_files(self.device), Handler.files)
        client.delete_file(self.device, Handler.files[0]["path"])
        self.assertEqual(Handler.deletes, ["/api/files?path=DIR%2FA%20B.GCO"])

    def test_bulk_delete_sends_paths_and_reports_partial_failure(self):
        client.delete_files(self.device, ["DIR/ONE.GCO", "TWO.GCO"])
        headers, body = Handler.bulk_deletes[0]
        self.assertEqual(headers["Content-Type"], "text/plain")
        self.assertEqual(body, b"DIR/ONE.GCO\nTWO.GCO")
        Handler.file_status = 409
        with self.assertRaisesRegex(RuntimeError, "Confirmed 1 of 2.*TWO.GCO"):
            client.delete_files(self.device, ["ONE.GCO", "TWO.GCO"])
        for paths in ([], ["A", "a"], ["../A"], ["A\nB"], ["A/"], ["é"], ["A"] * 257):
            with self.subTest(paths=paths), self.assertRaises(ValueError):
                client.delete_files(self.device, paths)
        with patch.object(client, "_file_request", side_effect=TimeoutError("timed out")):
            with self.assertRaisesRegex(RuntimeError, "may have been deleted; refresh"):
                client.delete_files(self.device, ["ONE.GCO", "TWO.GCO"])

    def test_file_operation_errors_and_invalid_lists(self):
        Handler.file_status = 503
        with self.assertRaisesRegex(RuntimeError, "HTTP 503"):
            client.list_files(self.device)
        Handler.file_status = 409
        with self.assertRaisesRegex(RuntimeError, "HTTP 409.*printer busy"):
            client.delete_file(self.device, "A.GCO")
        Handler.file_status = 200
        for invalid in ({"name": "not a list"}, [{"name": "bad", "size": 1}],
                        [{"name": "bad", "path": "A.GCO", "size": True}],
                        [{"name": "bad", "path": "A.GCO", "size": -1}],
                        [{"name": "A", "path": "A", "size": 1}] * 257):
            Handler.files = invalid
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                client.list_files(self.device)
        with self.assertRaises(ValueError):
            client.delete_file(self.device, "")


if __name__ == "__main__":
    unittest.main()
