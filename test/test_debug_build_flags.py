import os
import subprocess
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "debug_build_flags.py"


class DebugBuildFlagsTest(unittest.TestCase):
    def run_flags(self, address=None, required=False):
        environment = os.environ.copy()
        environment.pop("DEBUG_ADDRESS", None)
        if address is not None:
            environment["DEBUG_ADDRESS"] = address
        return subprocess.run(
            [sys.executable, str(SCRIPT)] + (["--required"] if required else []),
            env=environment, text=True, capture_output=True,
        )

    def test_normal_build_without_address(self):
        result = self.run_flags()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_debug_build_requires_address(self):
        for address in (None, ""):
            with self.subTest(address=address):
                result = self.run_flags(address, required=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("DEBUG_ADDRESS", result.stderr)

    def test_host_port_and_http_url(self):
        for address, expected in (
            ("192.168.1.1", "http://192.168.1.1:8080/"),
            ("192.168.1.1:1234", "http://192.168.1.1:1234/"),
            ("http://logs.local:9000/", "http://logs.local:9000/"),
        ):
            with self.subTest(address=address):
                result = self.run_flags(address, required=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), f'-DNETWORK_DEBUG_ADDRESS=\\"{expected}\\"')

    def test_rejects_malformed_addresses(self):
        for address in ("", "https://example.com", "host:", "host:0", "host:99999",
                        "http://user:pass@host/", "host/path", "host\n-DMALICIOUS"):
            with self.subTest(address=address):
                result = self.run_flags(address)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("DEBUG_ADDRESS", result.stderr)


if __name__ == "__main__":
    unittest.main()
