import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from Wireless3DPrinter.file_size import format_file_size


class FileSizeTests(unittest.TestCase):
    def test_sizes_and_unit_boundaries(self):
        cases = {
            0: "0 B",
            1: "1 B",
            1023: "1023 B",
            1024: "1 KB",
            1536: "1.5 KB",
            1048: "1 KB",
            1048575: "1 MB",
            1048576: "1 MB",
            1572864: "1.5 MB",
            1073741824: "1 GB",
            1099511627776: "1 TB",
        }
        for size, expected in cases.items():
            with self.subTest(size=size):
                self.assertEqual(format_file_size(size), expected)
