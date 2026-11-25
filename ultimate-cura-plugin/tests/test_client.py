import http.server
import io
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
    prints = []
    printer_state = {"printer_name": "Workshop", "printer_status": "not_sd_printing",
                     "nozzle_temperature": 210.5, "bed_temperature": None}
    version = {"firmware_version": "1.0"}
    version_status = 200

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/api/version":
            data = json.dumps(self.version).encode()
            self.send_response(self.version_status)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if self.path == "/api/status":
            data = json.dumps(self.printer_state).encode()
            self.send_response(self.file_status)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
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
        if self.path.startswith("/api/files/print?"):
            self.prints.append(self.path)
            self.send_response(self.file_status)
            self.end_headers()
            self.wfile.write(b"printer busy" if self.file_status != 200 else b"acknowledged")
            return
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
        Handler.prints = []
        Handler.version = {"firmware_version": "1.0"}
        Handler.version_status = 200
        Handler.printer_state = {"printer_name": "Workshop", "printer_status": "not_sd_printing",
                                 "nozzle_temperature": 210.5, "bed_temperature": None}
        self.device = client.Device("wireless-3d-printer-123", "Workshop", "127.0.0.1", self.server.server_port)

    def test_filename_is_safe_and_has_one_extension(self):
        self.assertEqual(client.safe_filename("../../bad\"\r\nname.GCODE"), "bad___name.gcode")
        self.assertEqual(client.safe_filename("..."), "untitled_print.gcode")
        self.assertEqual(client.safe_filename("example.gcode"), "example.gcode")
        self.assertEqual(len(client.safe_filename("a" * 200)), 106)

    def test_model_filename_uses_full_uppercase_snake_case(self):
        examples = {
            "esp32-s3-supermini-box": "WRP-ESP32_S3_SUPERMINI_BOX.gcode",
            "a  mixed-name.v2.GCODE": "WRP-A_MIXED_NAME_V2.gcode",
            "../../hello-world": "WRP-HELLO_WORLD.gcode",
            "...": "WRP-UNTITLED_PRINT.gcode",
            "a" * 81: "WRP-" + "A" * 81 + ".gcode",
        }
        for name, expected in examples.items():
            with self.subTest(name=name):
                self.assertEqual(client.model_filename(name), expected)
        with self.assertRaisesRegex(ValueError, "too long"):
            client.model_filename("a" * 82)

    def test_version_compatibility_and_invalid_responses(self):
        for version in ("1.0", "1.7", "1.100"):
            Handler.version = {"firmware_version": version}
            self.assertEqual(client.version_warning(self.device), "")
        for version in ("0.9", "2.0", "10.1"):
            Handler.version = {"firmware_version": version}
            warning = client.version_warning(self.device)
            self.assertIn("WARNING", warning)
            self.assertIn("1.0", warning)
            self.assertIn(version, warning)
        for value in (None, {}, [], {"firmware_version": 1}, {"firmware_version": "1"},
                      {"firmware_version": "1.0.0"}, {"firmware_version": "01.0"},
                      {"firmware_version": "1.-1"}, {"firmware_version": "١.0"}):
            Handler.version = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                client.version_warning(self.device)
        Handler.version_status = 404
        with self.assertRaisesRegex(RuntimeError, "HTTP 404"):
            client.version_warning(self.device)
        with patch.object(client, "_file_request", return_value=b"<html>old firmware</html>"):
            with self.assertRaises(ValueError):
                client.version_warning(self.device)

    def test_version_comparison_uses_installed_plugin_metadata(self):
        Handler.version = {"firmware_version": "2.1"}
        with patch.object(Path, "open", side_effect=lambda **kwargs: io.StringIO('{"version":"2.4"}')):
            self.assertEqual(client.version_warning(self.device), "")
            Handler.version = {"firmware_version": "1.99"}
            self.assertIn("plugin version 2.4", client.version_warning(self.device))
        with patch.object(Path, "open", side_effect=lambda **kwargs: io.StringIO('{"version":"bad"}')):
            with self.assertRaisesRegex(ValueError, "Invalid version"):
                client.version_warning(self.device)

    def test_status_validation_and_filename_matching(self):
        self.assertEqual(client.printer_status(self.device), Handler.printer_state)
        for key, value in (("printer_status", "idle"), ("nozzle_temperature", True),
                           ("bed_temperature", float("nan")), ("printer_name", "")):
            original = Handler.printer_state[key]
            Handler.printer_state[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                client.printer_status(self.device)
            Handler.printer_state[key] = original
        self.assertTrue(client.matches_model({"name": "other", "path": "MY_PRINT.GCO"}, "my print"))
        self.assertTrue(client.matches_model({"name": "my print.gcode", "path": "MY~1.GCO"}, "my print"))
        self.assertTrue(client.matches_model(
            {"name": "WRP-ESP32_S3_SUPERMINI_BOX.GCO", "path": "WRP-ES~1.GCO"},
            "esp32-s3-supermini-box"))
        self.assertFalse(client.matches_model({"name": "other", "path": "OTHER.GCO"}, "my print"))
        self.assertFalse(client.matches_model({"name": "untitled_print.gcode", "path": "UNTITLED.GCO"}, ""))

    def test_identity_validation_and_upload(self):
        self.assertEqual(client.device_at("127.0.0.1", self.server.server_port), self.device)
        data = b"G28\nG1 X12 Y34\n" * 4000
        progress = []
        with tempfile.NamedTemporaryFile() as f:
            f.write(data)
            f.flush()
            client.upload(self.device, f.name, client.model_filename("esp32-s3-supermini-box"), progress.append)
        path, headers, body = Handler.uploads[0]
        self.assertEqual(headers["Content-Type"], "application/octet-stream")
        self.assertEqual(path, "/upload?name=WRP-ESP32_S3_SUPERMINI_BOX.gcode&long_name=1")
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

    def test_start_print_uses_path_and_reports_errors(self):
        client.start_print(self.device, "DIR/FILE.GCO")
        self.assertEqual(Handler.prints, ["/api/files/print?path=DIR%2FFILE.GCO"])
        for path in ("../FILE.GCO", "A\nB", "", "/A", "A//B"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                client.start_print(self.device, path)
        Handler.file_status = 409
        with self.assertRaisesRegex(RuntimeError, "printer busy.*may have started"):
            client.start_print(self.device, "FILE.GCO")
        with patch.object(client, "_file_request", side_effect=TimeoutError("timeout")):
            with self.assertRaisesRegex(RuntimeError, "may have started"):
                client.start_print(self.device, "FILE.GCO")

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
