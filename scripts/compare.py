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
            offset = 0
            line_number = 1
            line_start = 0
            while True:
                first_chunk = first.read(65536)
                second_chunk = second.read(65536)
                if first_chunk != second_chunk:
                    common_length = min(len(first_chunk), len(second_chunk))
                    mismatch = next(
                        (index for index in range(common_length)
                         if first_chunk[index] != second_chunk[index]),
                        common_length,
                    )
                    prefix = first_chunk[:mismatch]
                    line_number += prefix.count(b"\n")
                    last_newline = prefix.rfind(b"\n")
                    if last_newline != -1:
                        line_start = offset + last_newline + 1

                    first.seek(line_start)
                    second.seek(line_start)
                    first_line = first.readline()
                    second_line = second.readline()
                    print("Files have different content.")
                    print(f"First difference at line {line_number}:")
                    print(f"First file: {repr(first_line) if first_line else '<end of file>'}")
                    print(f"Second file: {repr(second_line) if second_line else '<end of file>'}")
                    return 1
                if not first_chunk:
                    print("Files have the same content.")
                    return 0
                line_number += first_chunk.count(b"\n")
                last_newline = first_chunk.rfind(b"\n")
                if last_newline != -1:
                    line_start = offset + last_newline + 1
                offset += len(first_chunk)
    except OSError as error:
        print(f"Error comparing files: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
