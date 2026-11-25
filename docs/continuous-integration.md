# Continuous integration

The GitHub Actions [CI workflow](../.github/workflows/ci.yml) runs on every
branch and tag push. Its build job builds both ESP32-S3 firmware variants and
the Cura plugin archive. Its independent test job runs the PlatformIO native tests, checks
generated frontend pages, and runs the Python, Cura plugin, frontend, and log
server host tests.

The build job pre-creates pioarduino's Python environment and installs the
HTML minifier there, so the firmware's generated-page build script can import
it on a fresh runner.

For a version tag push, a release job runs only after both jobs succeed. It
checks that the tag contains the component versions in the tagged commit,
verifies that the tagged commit is reachable from
`master` and that the remote tag still points to it, and validates the
versioned assets downloaded from the build job before publishing. Release
publication uses the workflow's `GITHUB_TOKEN` with job-scoped write access;
the other jobs have read-only access. See
[versioning and releases](versioning.md#creating-a-release).

The workflow does not upload firmware to a device or require a personal access token.
Cura UI integration tests are skipped when Cura/Uranium and PyQt6 are not
available on the runner; device and live Cura testing remain manual.

The README badges show the overall workflow status for `master` only. A
failure in either job makes the workflow fail. The badge appears after the
workflow is pushed and runs on `master`; pushes on other branches do not
change its status. The release badge points to the latest stable release, not
the most recent CI run.
