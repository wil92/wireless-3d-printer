---
name: update-version
description: Update firmware and Cura plugin versions independently for a change.
---

# Update versions

Follow the rules in [versioning documentation](../../../docs/versioning.md).

1. Identify whether the change affects shipped firmware, the Cura plugin, or
   both. Treat their versions independently; never synchronize their values
   merely because one changed.
2. For each affected component, increment its major and reset its minor for a
   plugin/firmware compatibility break; otherwise increment its minor. Do this
   once per change, not once per edited file. Do not bump an unaffected
   component. Preserve the `major.minor` format.
3. Update firmware in `include/version.h` and plugin metadata in
   `ultimate-cura-plugin/Wireless3DPrinter/plugin.json` as applicable.
4. Update version-related documentation or tests if necessary, and run
   relevant checks. Report the resulting versions and any checks not run.
