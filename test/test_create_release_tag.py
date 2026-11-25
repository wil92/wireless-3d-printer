import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TAG = "v1.2-cura3.4"


class CreateReleaseTagTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.repo = root / "repo"
        self.remote = root / "remote.git"
        self.repo.mkdir()
        self.env = os.environ.copy()
        self.env.update({"GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull,
                         "GIT_TERMINAL_PROMPT": "0"})
        self.git("init", "-q", "--initial-branch=master")
        self.git("config", "user.name", "Release Test")
        self.git("config", "user.email", "release-test@example.invalid")
        subprocess.run(["git", "init", "-q", "--bare", str(self.remote)],
                       check=True, env=self.env)
        for directory in ("scripts", "include", "ultimate-cura-plugin/Wireless3DPrinter"):
            (self.repo / directory).mkdir(parents=True)
        for name in ("create_release_tag.py", "release_artifacts.py"):
            shutil.copyfile(ROOT / "scripts" / name, self.repo / "scripts" / name)
        (self.repo / ".gitignore").write_text("__pycache__/\n")
        (self.repo / "include/version.h").write_text('#define FIRMWARE_VERSION "1.2"\n')
        (self.repo / "ultimate-cura-plugin/Wireless3DPrinter/plugin.json").write_text(
            '{"version":"3.4"}\n'
        )
        self.git("add", ".")
        self.git("commit", "-qm", "Initial commit")
        self.git("remote", "add", "origin", str(self.remote))
        self.git("push", "-q", "origin", "master")

    def git(self, *args, check=True):
        return subprocess.run(["git", *args], cwd=self.repo, env=self.env, text=True,
                              capture_output=True, check=check)

    def run_script(self, answer):
        return subprocess.run([sys.executable, "scripts/create_release_tag.py"],
                              cwd=self.repo, env=self.env, text=True, input=answer,
                              capture_output=True)

    def local_tag(self):
        return self.git("show-ref", "--verify", "--quiet", f"refs/tags/{TAG}",
                        check=False).returncode == 0

    def remote_tag(self):
        return bool(self.git("ls-remote", "--refs", "--tags", "origin",
                             f"refs/tags/{TAG}").stdout)

    def test_decline_removes_created_tag_without_pushing(self):
        result = self.run_script("n\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        for expected in ("Firmware: 1.2", "Cura plugin: 3.4", f"Tag: {TAG}", "Commit:"):
            self.assertIn(expected, result.stdout)
        self.assertFalse(self.local_tag())
        self.assertFalse(self.remote_tag())

    def test_empty_confirmation_pushes_annotated_tag(self):
        result = self.run_script("\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.local_tag())
        self.assertTrue(self.remote_tag())
        self.assertEqual(self.git("cat-file", "-t", f"refs/tags/{TAG}").stdout.strip(), "tag")
        self.assertEqual(self.git("rev-parse", f"{TAG}^{{commit}}").stdout.strip(),
                         self.git("rev-parse", "HEAD").stdout.strip())

    def test_yes_pushes_and_invalid_answer_reprompts(self):
        result = self.run_script("maybe\nyes\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Please enter y or n.", result.stdout)
        self.assertTrue(self.remote_tag())

    def test_eof_cancels_without_pushing(self):
        result = self.run_script("")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Confirmation unavailable", result.stderr)
        self.assertFalse(self.local_tag())
        self.assertFalse(self.remote_tag())

    def test_dirty_worktree_and_wrong_branch_are_rejected(self):
        (self.repo / "untracked").write_text("dirty")
        result = self.run_script("\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("working-tree changes", result.stderr)
        (self.repo / "untracked").unlink()
        self.git("switch", "-qc", "feature")
        result = self.run_script("\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("master branch", result.stderr)
        self.assertFalse(self.local_tag())
        self.assertFalse(self.remote_tag())

    def test_unpushed_master_and_existing_tags_are_rejected(self):
        (self.repo / "file").write_text("new")
        self.git("add", "file")
        self.git("commit", "-qm", "Unpushed")
        result = self.run_script("\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must match", result.stderr)
        self.git("push", "-q", "origin", "master")
        self.git("tag", TAG)
        result = self.run_script("\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stderr)
        self.git("push", "-q", "origin", f"refs/tags/{TAG}")
        self.git("tag", "-d", TAG)
        result = self.run_script("\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stderr)
        self.assertFalse(self.local_tag())

    def test_failed_push_retains_local_tag(self):
        hook = self.remote / "hooks/pre-receive"
        hook.write_text("#!/bin/sh\nexit 1\n")
        hook.chmod(0o755)
        result = self.run_script("y\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("local tag was retained", result.stderr)
        self.assertTrue(self.local_tag())
        self.assertFalse(self.remote_tag())

    def test_master_change_while_waiting_cancels_and_removes_tag(self):
        process = subprocess.Popen(
            [sys.executable, "-u", "scripts/create_release_tag.py"],
            cwd=self.repo, env=self.env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.addCleanup(lambda: process.poll() is None and process.terminate())
        output = b""
        deadline = time.monotonic() + 10
        while b"[Y/n] " not in output and time.monotonic() < deadline:
            if select.select([process.stdout], [], [], 0.1)[0]:
                chunk = os.read(process.stdout.fileno(), 1024)
                if not chunk:
                    break
                output += chunk
        self.assertIn(b"[Y/n] ", output)
        (self.repo / "changed").write_text("new commit")
        self.git("add", "changed")
        self.git("commit", "-qm", "Advance master")
        self.git("push", "-q", "origin", "master")
        _, error = process.communicate(input=b"y\n", timeout=10)
        self.assertNotEqual(process.returncode, 0)
        self.assertIn(b"local master commit changed", error)
        self.assertFalse(self.local_tag())
        self.assertFalse(self.remote_tag())


if __name__ == "__main__":
    unittest.main()
