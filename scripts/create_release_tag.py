#!/usr/bin/env python3
"""Create and, after confirmation, push a component-version release tag."""

import subprocess
import sys

from release_artifacts import ROOT, release_tag, versions


class ReleaseError(Exception):
    pass


def git(*args, check=True):
    result = subprocess.run(
        ["git", *args], cwd=ROOT, text=True, capture_output=True, check=False
    )
    if check and result.returncode:
        detail = result.stderr.strip() or f"exit status {result.returncode}"
        raise ReleaseError(f"Git {args[0]} failed: {detail}")
    return result


def push_remote():
    urls = git("remote", "get-url", "--push", "--all", "origin").stdout.splitlines()
    if len(urls) != 1 or not urls[0]:
        raise ReleaseError("origin must have exactly one push URL")
    return urls[0]


def remote_refs(remote, kind, ref):
    result = git("ls-remote", "--refs", kind, remote, ref, check=False)
    if result.returncode:
        detail = result.stderr.strip().replace(remote, "<origin>") or f"exit status {result.returncode}"
        raise ReleaseError(f"Could not inspect origin: {detail}")
    return [line.split("\t", 1) for line in result.stdout.splitlines()]


def check_repository(remote, expected_head=None):
    if git("branch", "--show-current").stdout.strip() != "master":
        raise ReleaseError("Switch to the master branch before creating a release")
    if git("status", "--porcelain", "--untracked-files=normal").stdout:
        raise ReleaseError("Commit or remove working-tree changes before creating a release")
    head = git("rev-parse", "--verify", "HEAD").stdout.strip()
    if expected_head is not None and head != expected_head:
        raise ReleaseError("The local master commit changed while awaiting confirmation")
    refs = remote_refs(remote, "--heads", "refs/heads/master")
    if len(refs) != 1 or refs[0] != [head, "refs/heads/master"]:
        raise ReleaseError("Local master must match origin's master; synchronize it first")
    return head


def tag_exists_locally(ref):
    result = git("show-ref", "--verify", "--quiet", ref, check=False)
    if result.returncode not in (0, 1):
        raise ReleaseError("Could not check the local release tag")
    return result.returncode == 0


def confirm():
    while True:
        answer = input("Push this tag to origin? [Y/n] ").strip().lower()
        if answer in ("", "y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        print("Please enter y or n.")


def create_release():
    remote = push_remote()
    head = check_repository(remote)
    firmware, plugin = versions()
    tag = release_tag(firmware, plugin)
    ref = f"refs/tags/{tag}"
    if tag_exists_locally(ref) or remote_refs(remote, "--tags", ref):
        raise ReleaseError(f"Release tag {tag} already exists locally or on origin")

    git("tag", "-a", tag, head, "-m", f"Release {tag}")
    tag_object = git("rev-parse", "--verify", ref).stdout.strip()
    rollback = True
    try:
        print(f"Firmware: {firmware}\nCura plugin: {plugin}\nTag: {tag}\nCommit: {head}")
        if not confirm():
            print("Release cancelled; removing the local tag.")
            return
        if push_remote() != remote:
            raise ReleaseError("Origin's push URL changed; no push attempted")
        check_repository(remote, head)
        if remote_refs(remote, "--tags", ref):
            raise ReleaseError(f"Release tag {tag} appeared on origin; no push attempted")
        if git("rev-parse", "--verify", ref).stdout.strip() != tag_object:
            raise ReleaseError("The local release tag changed; no push attempted")
        rollback = False
        try:
            git("push", "--no-follow-tags", "origin", f"{ref}:{ref}")
        except ReleaseError as error:
            raise ReleaseError(
                f"{str(error).replace(remote, '<origin>')}\n"
                "The local tag was retained; check origin before retrying."
            ) from error
        print(f"Pushed {tag} to origin.")
    finally:
        if rollback:
            git("update-ref", "-d", ref, tag_object)


def main():
    try:
        create_release()
    except EOFError:
        print("Confirmation unavailable; release cancelled.", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nRelease interrupted; check origin before retrying.", file=sys.stderr)
        return 1
    except (ReleaseError, ValueError, OSError) as error:
        print(f"Release failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
