---
name: release-changelog
description: Prepare a GitHub-compatible release changelog from commits and changes between versions.
---

# Prepare a release changelog

Use this workflow when asked for release notes or a changelog between versions.
Follow the format in [the changelog template](template.md) and the project's
[versioning and release policy](../../../docs/versioning.md).

1. **Identify the release.** Use the requested target tag or commit; if none
   is specified, use `HEAD`. Determine the firmware and Cura plugin versions
   from the target commit, not the current working tree. Release tags have the
   form `v<firmware-version>-cura<plugin-version>`. Verify a requested release
   tag agrees with both version files at that commit; report any mismatch
   rather than inventing a version.
2. **Determine the comparison range.** Use the requested previous release if
   provided, otherwise identify the most recent preceding release tag in the
   target's local commit ancestry. Consider only tags in the project's release
   format, and verify that the previous commit is an ancestor of the target.
   Do not assume version order or tag creation date proves ancestry. If there
   are multiple plausible preceding releases, a requested ref is missing, or
   the local history or tags are insufficient, ask the user for the intended
   baseline rather than guessing. If the user confirms this is the first
   release, review the complete history through the target. Use local Git
   history; do not fetch or change remote state as part of this workflow.
3. **Analyze the changes.** Inspect the commits in the exclusive
   previous-release to inclusive target range and their relevant diffs, not
   just commit titles or the final net diff. For a first release, inspect the
   history through the target. Check affected code and documentation as needed
   to understand user-visible behavior, compatibility, migration, and upgrade
   implications. Do not include uncommitted work in a committed release.
4. **Write the notes.** Use the user-provided release date, or today's date in
   `YYYY-MM-DD` format if none was provided. Fill in the template with the
   combined release tag or both component versions. Summarize meaningful
   changes in clear, accurate, user-facing language; group related commits,
   distinguish firmware from Cura changes where useful, and avoid unsupported
   claims, implementation noise, and repetitive commit-by-commit entries.
   Include **Important notes** only when users need to know about a breaking
   change, compatibility issue, migration, or other significant caveat. Do not
   invent a note when none is supported by the evidence.
5. **Present the result.** Return the completed changelog as a self-contained
   GitHub-flavored Markdown block that the user can copy directly into the
   release description. Briefly identify the compared refs outside the block
   and disclose any limitations or uncertainty. Do not create or edit a
   changelog file unless requested. Never create, edit, publish, or push a
   GitHub release, tag, commit, or other remote content in this workflow.
