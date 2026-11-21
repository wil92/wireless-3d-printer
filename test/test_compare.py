import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "compare.py"


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.first = Path(self.directory.name) / "first file"
        self.second = Path(self.directory.name) / "second file"

    def compare(self, *paths):
        return subprocess.run([str(SCRIPT), *map(str, paths)],
                              text=True, capture_output=True)

    def test_equal_files(self):
        for content in (b"", b"\x00\xff\r\n", b"a" * 65537):
            with self.subTest(length=len(content)):
                self.first.write_bytes(content)
                self.second.write_bytes(content)
                result = self.compare(self.first, self.second)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("same content", result.stdout)

    def test_different_files(self):
        for first, second in (
            (b"a", b"b"),
            (b"a", b"aa"),
            (b"a" * 65536 + b"x", b"a" * 65536 + b"y"),
            (b"line\n", b"line\r\n"),
        ):
            with self.subTest(first_length=len(first), second_length=len(second)):
                self.first.write_bytes(first)
                self.second.write_bytes(second)
                result = self.compare(self.first, self.second)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("different content", result.stdout)

    def test_missing_file_is_an_error(self):
        self.first.write_bytes(b"content")
        result = self.compare(self.first, self.second)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Error comparing files", result.stderr)

    def test_missing_argument_is_an_error(self):
        result = self.compare(self.first)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
