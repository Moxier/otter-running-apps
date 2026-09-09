# v0.5.0 validation — 2026-09-09

Strict C++17 build, model tests and compiled-plugin mock niri integration tests
passed. Existing tests cover centering, workspace changes, ordering, counts,
click cycling, stale hits, clipping, malformed events, reconnects and unload.
Every test render now asserts that no animation frame was requested, even with
the old animation environment variable set to 1.

Removed motion code, its header and animation-only tests/docs/patch from this
release. Consolidated duplicate clipping logic; removed unused Group.order.
The refresh patch is retained because the host scheduling limitation remains.
