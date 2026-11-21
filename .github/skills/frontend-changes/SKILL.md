---
name: frontend-changes
description: Update the embedded Wi-Fi configuration frontend and its generated header.
---

# Frontend changes

1. Edit the appropriate page in `frontend/`, keeping endpoints and validation
   consistent with `lib/webserver/web_server_manager.cpp`.
2. Run `python3 scripts/build_views.py` to regenerate headers in
   `lib/webserver/views/`. Never edit the generated headers directly.
3. Run `python3 scripts/build_views.py --check`, `node test/host/frontend_test.js`, the
   relevant host tests, and
   `pio run -e esp32dev`. Verify the generated page is served by the firmware;
   test it on the device when authorized hardware is available, otherwise report
   that hardware validation was not performed.
