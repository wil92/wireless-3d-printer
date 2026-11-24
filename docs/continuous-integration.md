# Continuous integration

The GitHub Actions [CI workflow](../.github/workflows/ci.yml) runs on every
branch push. Its build job builds the ESP32-S3 firmware and Cura plugin
archive. Its independent test job runs the PlatformIO native tests, checks
generated frontend pages, and runs the Python, Cura plugin, frontend, and log
server host tests.

The workflow does not upload firmware or require a personal access token.
Cura UI integration tests are skipped when Cura/Uranium and PyQt6 are not
available on the runner; device and live Cura testing remain manual.

The README badges show the overall workflow status for `master` only. A
failure in either job makes the workflow fail. The badge appears after the
workflow is pushed and runs on `master`; pushes on other branches do not
change its status.
