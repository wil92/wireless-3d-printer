# Compare local files

To compare two local copies byte for byte, run from the project root:

```sh
./scripts/compare.py /path/to/original.gcode /path/to/uploaded.gcode
```

The script exits with 0 for identical files, 1 for different files, and 2 for
an error. It does not retrieve files from the printer; obtain the uploaded file
separately. Uploading strips comments, blank lines, and leading and trailing
whitespace from G-code lines, so a successful upload may not be byte-for-byte
identical to the original.
