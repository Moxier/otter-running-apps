# Running Apps for Otter + niri — v0.5.0

A small C++17 native Otter bar plugin: one themed icon per application with open
niri windows, active-app highlighting, and left-click focus. Applications with
no open windows (background daemons, tray-only processes) are not listed.

**Integration status:** the plugin compiles and passes the included C ABI and
mock niri socket tests. A previous v0.1.0 read-only probe of the real niri session produced
three app cells and one active cell. Rendering/clicks in the actual Otter panel
have not been tested.
**Reliable live refresh requires the included Otter bar patch.** The current
unmodified ABI cannot schedule a timer or watch a plugin socket. Without the
patch, the plugin reads events on ordinary bar redraws; changes can be delayed
until other widgets or input cause a redraw. This is not an instantly updating
standalone plugin on an unmodified host.

## v0.5.0 — simpler, no animation

Animation code and its host-patch instructions have been removed. No animation
setting is needed; older `OTTER_RUNNING_APPS_ANIMATIONS` values are ignored.
Centering, current-workspace filtering, layout order, count badges, and clicking
are preserved. Clipping uses one shared check, and unused group state is removed.

Visible cells are centered inside the plugin area, including short overflow
pages. Keep the plugin in `layout_center` for bar-centered placement. Workspace
changes reset paging. On multiple monitors all instances follow niri's single
focused workspace: this ABI does not identify each instance's output.

Run `./install.sh`, then restart your existing bar to update. **The earlier
refresh limitation still applies**; removing animation does not add a host timer
or guarantee instant updates on the packaged bar.

## Behavior

- Groups by exact, case-sensitive niri `app_id`. Unknown/empty IDs remain separate
  windows, so unrelated apps are never merged. Only the focused workspace is
  included; each bar instance has its own event connection.
- Clicking an inactive app focuses its last-focused window (first window in layout order if
  none has been focused since connection). Clicking the active app cycles its
  windows in layout order (left-to-right, then top-to-bottom). Focus highlight follows confirmed niri
  events, not the click request.
- Resolves XDG desktop-file IDs and `StartupWMClass` to `Icon=` names; honors
  user-over-system precedence and `Hidden=true` masks. Includes common Flatpak
  export paths. No desktop `Exec=` command is executed. Desktop entries are
  indexed at plugin initialization; restart the bar after installing new apps.
- Missing desktop entries and absolute-file icons use Otter's bundled `sparkles`
  icon. A desktop entry whose named icon is missing from the selected theme may
  still appear blank: the ABI cannot report whether icon lookup succeeded.
- Reserves eight 36-pixel cells (288 logical pixels), even when empty. This avoids
  relying on automatic plugin width remeasurement, which this host does not do.
  If there are more apps, the last cell is `>` and cycles pages. Reduce or enlarge
  the reservation with `OTTER_RUNNING_APPS_SLOTS=2..32` in the bar's environment.
  At least 72 logical pixels are needed for usable overflow paging.
- No animation, motion bookkeeping, or extra frame requests while rendering.
  The active-app background still uses Otter's normal selected state.
- Disconnects clear stale icons, retries the same `NIRI_SOCKET` every second, and
  accepts niri's new authoritative snapshot on reconnection. After a compositor
  restart with a different socket path, restart the bar from the new session.

## Compatibility and sources checked

Inspected on 2026-09-08:

- [Otter plugin documentation](https://docs.otter-shell.org/developers/plugins).
- [otter-bar](https://git.pika-os.com/otter-shell/otter-bar), commit
  `9ebc88c21c22d2c4a305b28e65147d5839788da2`, and release `v0.11.113`.
  The release and this commit have identical `src/main.zig`.
- [Otter examples](https://git.pika-os.com/otter-shell/otter-shell-plugins), commit
  `82fa3c9587d4791b973c186cc98c96b8e95f1d86`; especially hello-label and cmd-watch.
- [niri IPC](https://github.com/niri-wm/niri/wiki/IPC) and
  [niri request/action types](https://niri-wm.github.io/niri/niri_ipc/enum.Action.html).

The unchanged upstream C header is included. Target **Otter v0.11.113's full
ABI-v1 host layout, including `sd_bar_item`**, or a later compatible layout.
Do not load this into an ancient ABI-v1 binary with a shorter host struct: ABI
major alone does not establish compatibility, and there is no struct-size field
for safely probing those old layouts. A full-size host with a null `sd_bar_item`
is rejected. The current host dispatches only left clicks as plugin CLICK events;
the event struct itself has no mouse-button field.

The plugin uses niri's line-delimited Unix-socket IPC directly. Requests:

```json
"EventStream"
{"Action":{"FocusWindow":{"id":42}}}
```

Focus uses a separate connection because the event-stream connection does not
accept later requests. Unknown event types/fields are ignored. IDs stay unsigned
64-bit integers, including values above JavaScript's precise-integer range.

## Build and test on PikaOS

With Otter and niri already installed:

```sh
sudo apt install build-essential python3
cd otter-running-apps
./build.sh
./test.sh
```

No Zig, downloaded build dependency, Python package, or Otter SDK is needed for
the plugin. `json.hpp` v3.12.0 is vendored with its license. The plugin uses libc
and libstdc++; **build on your PikaOS machine** instead of copying a binary built
against another distribution's libraries. `make` and `make test` are aliases.

Tests cover centering, current-workspace filtering, stale workspace clicks, layout/workspace reordering, count badges, grouping, focus/MRU/cycling, unknown IDs, snapshot replacement,
malformed input, future events, desktop-file precedence, ABI loading, exact
64-bit focus requests, fragmented socket data, overflow paging, clipping,
reconnect, stale clicks, and destruction. Socket tests require permission to
create a temporary local Unix socket; no real desktop is contacted.

For a separate **read-only** live IPC probe from a niri terminal:

```sh
c++ -std=c++17 -Iinclude tests/probe.cpp -ldl -o build/live-probe
./build/live-probe ./build/plugins/running-apps/libotter_plugin_running_apps.so
```

This loads the actual plugin with a mock renderer for two seconds and reports
cell counts without names/titles or focus actions. Zero cells can also mean an
unavailable socket, so it is only a basic diagnostic.

## Install and enable

```sh
./install.sh
```

This builds and installs only into
`${XDG_DATA_HOME:-$HOME/.local/share}/otter-shell/plugins/running-apps/`.
It does not edit your configuration or restart the bar. The installer uses atomic
library replacement so it does not truncate a loaded `.so`. Restart the bar after
upgrades so the old library is unloaded.

Add the following to an existing layout in
`~/.config/otter-shell/otter-bar.conf`, preserving your other widgets:

```ini
layout_center = plugin:running-apps
```

In `~/.config/otter-shell/plugins.conf`, set or update this key:

```ini
running-apps_enabled = true
```

Launch/restart the bar from your niri session so it inherits `NIRI_SOCKET` and
`WAYLAND_DISPLAY`. Check the session first:

```sh
printf '%s\n' "$NIRI_SOCKET"
niri msg --json windows
```

For a temporary development install, use the build directory as a discovery root:

```sh
OTTER_PLUGIN_PATH="$PWD/build/plugins" otter-bar
```

Run only one bar instance per intended panel during testing. If your session
supervises the bar, stop/restart it using that session's existing mechanism.
Do not launch a second bar over the existing panel.

## Enable reliable 100 ms refresh (host integration)

`patches/otter-bar-running-apps-refresh.patch` adds a main-loop deadline only when
an attached `running-apps` slot exists. It requests a redraw every 100 ms on those
outputs. All IPC and rendering stay on the host thread. It never sleeps in a
plugin callback, starts a worker, modifies global signal handling, or calls
Wayland from another thread.

This deliberately small workaround redraws the affected **whole bar at up to
10 Hz**, including when the desktop is idle. It is a functional integration
compromise, not a zero-cost event-driven implementation. Hidden/throttled Wayland
surfaces can delay callbacks. A future upstream timer/fd-registration API should
replace the patch. Changing `+ 100` to `+ 250` reduces redraw frequency at the
cost of response time.

The patch applies cleanly to inspected source and `v0.11.113`. A local host with
both historical patches was built and passed upstream tests on 2026-09-09;
actual panel behavior with that custom host is not yet verified. Use a separate local build so you can
return to the packaged bar immediately. Upstream's release tag has pinned remote
dependencies; its development HEAD instead expects sibling source checkouts.

From the plugin directory, prepare the source:

```sh
PLUGIN_DIR="$PWD"
git clone --branch v0.11.113 --depth 1 \
  https://git.pika-os.com/otter-shell/otter-bar.git otter-bar-v0.11.113
cd otter-bar-v0.11.113
git apply --check "$PLUGIN_DIR/patches/otter-bar-running-apps-refresh.patch"
git apply "$PLUGIN_DIR/patches/otter-bar-running-apps-refresh.patch"
```

Build with the **Zig 0.16.0 toolchain required by that release**. Unlike the C++
plugin, this downloads Otter's pinned library dependencies. The host's build also
needs its Wayland, FreeType, xkbcommon and rendering development dependencies,
plus basu/sd-bus and PipeWire for the default feature set. Use the release's
[README/build instructions](https://git.pika-os.com/otter-shell/otter-bar/src/tag/v0.11.113/README.md)
and PikaOS packaging build dependencies for your installed platform.

```sh
zig version
zig build -Doptimize=ReleaseFast
zig build test
```

Use this project's `./test.sh` for ABI smoke testing. The inspected upstream
`plugin-abi-smoke` constructs a host with a null `sd_bar_item`; this icon plugin
correctly rejects that stub, so that upstream smoke target is not applicable
without giving its mock host a bar-item callback.

After stopping your existing bar, test the local host from a niri terminal:

```sh
OTTER_PLUGIN_PATH="$PLUGIN_DIR/build/plugins" ./zig-out/bin/otter-bar
```

Keep the local host separate from `/usr/bin/otter-bar`. To revert, stop that local
host and restart your normal packaged bar. Recheck the patch before applying it
to a newer release; do not force a failed patch.

## Live acceptance checklist

1. Open two Firefox windows and a terminal: expect two app icons. Focus either
   Firefox window: its icon gets Otter's selected background.
2. Click the terminal icon: it receives focus. Click Firefox: its last-focused
   window receives focus. Click Firefox again: focus cycles to its other window.
3. Close one Firefox window: the group remains. Close the last: the icon vanishes.
4. Open an app on another workspace: it stays hidden until that workspace is
   focused. Switch to an empty workspace: the group disappears. Counts and
   click cycling must never include windows from another workspace.
5. Open more than eight distinct apps on the current workspace: use `>` to reach
   the remaining groups. Check that a short last page remains centered.
6. Leave the pointer still and open/close/focus windows using keyboard shortcuts.
   With the host patch, changes should appear around 100 ms plus frame/IPC delay.
   Without it, expect redraw-dependent delays.
7. Test your theme at 1× and 2× scale, restart the bar, and repeat with no windows.
8. If using several monitors, verify each panel and click through both. Each
   instance owns its sockets and page selection, so unloading one is independent.

No auto-launch, close-window button, pinning, tooltip, drag/reorder, window preview,
per-output filtering, or settings UI is implemented in this version.

## Troubleshooting and removal

- Empty panel: confirm `NIRI_SOCKET` in the **bar's** launch environment, plugin
  enable key, `plugin:running-apps` layout, current host ABI, and refresh patch.
- Generic icon: inspect the app's niri `app_id` and matching `.desktop` file's
  `Icon=`/`StartupWMClass`. Absolute image paths use the generic icon in v1.
- Blank matched icon: choose an installed icon-theme name in the desktop entry.
  Otter owns rendering and this ABI has no lookup-result query.
- Focus races with a closing window: harmless; niri rejects the stale request.
  The plugin never changes the highlight until it sees compositor state.
- High idle draw rate: expected with the 100 ms host workaround; use 250 ms or
  revert the patch and accept delayed updates pending an upstream scheduling API.

To disable, remove `plugin:running-apps` from the layout, set
`running-apps_enabled = false`, and restart the bar. Then remove the user plugin
directory if desired. The installer does not modify any system package.

## Project map and licenses

- `src/plugin.cpp`: C ABI, render cells, stable per-frame click mapping, paging.
- `src/core.hpp`: window state, grouping, and focus selection.
- `src/ipc.hpp`: bounded nonblocking sockets, framing, retries, action queue.
- `src/icons.hpp`: XDG desktop-entry icon indexing.
- `include/otter_plugin_abi.h`: verbatim upstream ABI header (Otter MIT license).
- `include/json.hpp`: nlohmann/json v3.12.0 (MIT license).
- `tests/`: unit and black-box integration tests.
- `patches/`: main-thread host refresh workaround.

Project code is MIT licensed; dependency notices are in `include/`.

Window positions follow [niri’s documented layout coordinates](https://niri-wm.github.io/niri/niri_ipc/struct.WindowLayout.html).

Workspace selection follows [niri workspace events](https://niri-wm.github.io/niri/niri_ipc/enum.Event.html).
