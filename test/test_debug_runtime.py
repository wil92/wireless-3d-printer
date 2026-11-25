import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DebugRuntimeTest(unittest.TestCase):
    def test_persistence_validation_and_storage_failures(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "debug-runtime-test"
            subprocess.run([
                "g++", "-std=c++11", "-DARDUINO_ARCH_ESP32",
                "-DNETWORK_DEBUG_RUNTIME_CONFIG",
                "-I", str(ROOT / "test/fixtures/debug_runtime"),
                str(ROOT / "test/fixtures/debug_runtime/test_runtime.cpp"),
                str(ROOT / "lib/debug_log/runtime_config.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
