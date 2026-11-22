"""Build an installable Cura plugin zip, including its Python dependencies."""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parent


def build(output):
    output = Path(output).resolve()
    with tempfile.TemporaryDirectory(prefix="wireless-3d-printer-cura-") as temp:
        stage = Path(temp) / "Wireless3DPrinter"
        shutil.copytree(ROOT / "Wireless3DPrinter", stage, ignore=shutil.ignore_patterns(
            "__pycache__", "*.pyc", "vendor"))
        subprocess.run([sys.executable, "-m", "pip", "install", "--disable-pip-version-check",
                        "--target", str(stage / "vendor"),
                        "-r", str(ROOT / "requirements.txt")], check=True)
        output.parent.mkdir(parents=True, exist_ok=True)
        with ZipFile(output, "w", ZIP_DEFLATED) as archive:
            for file in sorted(stage.rglob("*")):
                if file.is_file() and "__pycache__" not in file.parts and file.suffix != ".pyc":
                    archive.write(file, file.relative_to(stage.parent))
    print("Created {}".format(output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(ROOT / "dist" / "Wireless3DPrinter.zip"))
    build(parser.parse_args().output)
