#!/usr/bin/env python3
"""Compare two local files byte for byte."""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description="Compare two files byte for byte.")
    parser.add_argument("file1", help="path to the first file")
    parser.add_argument("file2", help="path to the second file")
    args = parser.parse_args()

    try:
        with open(args.file1, "rb") as first, open(args.file2, "rb") as second:
            while True:
                first_chunk = first.read(65536)
                second_chunk = second.read(65536)
                if first_chunk != second_chunk:
                    print("Files have different content.")
                    return 1
                if not first_chunk:
                    print("Files have the same content.")
                    return 0
    except OSError as error:
        print(f"Error comparing files: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
