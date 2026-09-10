# Otter Running Apps

A compact application switcher for **Otter Shell + niri**. Shows centered app
icons for the currently focused workspace, with active-app highlighting and
left-click window switching.

## Features

- One icon per app, with a small `2`–`9+` badge for multiple windows.
- Icons follow niri's window layout order and stay centered as apps open or close.
- Click an inactive app to restore its last-focused window; click the active app
  to cycle through its windows on the current workspace.
- Desktop-entry icon lookup, including `StartupWMClass`, unambiguous casing
  differences, and common Flatpak export locations.
- Overflow paging with `>` instead of expanding across the bar.
- Direct niri IPC, reconnect handling, and no animation or background threads.

## Before installing

**Refresh limitation:** the inspected Otter ABI has no plugin timer or socket
registration. On an unmodified bar, this plugin processes events when the bar
redraws, so updates may be delayed. The included [refresh patch](patches/otter-bar-running-apps-refresh.patch)
provides a 100 ms update interval, at the cost of redrawing affected bars up to
10 times per second even when idle. See [host integration](#host-integration).

Target: **Otter Bar v0.11.113's complete ABI-v1 layout**, including `sd_bar_item`,
or a later compatible layout. Older ABI-v1 builds with shorter host structs are
not supported; the ABI version number alone is not a compatibility check.

On multiple monitors, all instances follow niri's **single focused workspace**.
The plugin ABI does not identify the monitor hosting each instance.

## Install

With Otter Bar and niri already installed on PikaOS:

```sh
sudo apt install git build-essential python3
git clone https://github.com/Moxier/otter-running-apps.git
cd otter-running-apps
./install.sh
```

The installer builds the plugin and installs it under
`$XDG_DATA_HOME/otter-shell/plugins/running-apps`, or
`~/.local/share/otter-shell/plugins/running-apps` when `XDG_DATA_HOME` is unset.
It does not change your configuration or restart the bar. Build locally to avoid
cross-distribution libc/libstdc++ compatibility issues.

Add or update the center layout in `~/.config/otter-shell/otter-bar.conf`:

```ini
layout_center = "plugin:running-apps"
```

Preserve any other widgets you want in that layout. Enable the plugin in
`~/.config/otter-shell/plugins.conf`:

```ini
running-apps_enabled = true
```

Restart your bar using your session's normal mechanism. It must inherit
`NIRI_SOCKET` and `WAYLAND_DISPLAY` from niri. Avoid starting a second bar over
an existing one.

## Configuration and behavior

| Setting | Default | Description |
| --- | --- | --- |
| `OTTER_RUNNING_APPS_SLOTS` | `8` | Reserved icon cells, from `2` to `32`; set in the bar's launch environment. |

Each cell is 36 logical pixels wide. Eight cells reserve 288 pixels, including
when the workspace is empty. Visible icons are centered within that reservation.
Overflow pages reset when switching workspaces.

Groups use exact niri app IDs. Apps without an ID stay as separate windows.
A group takes the position of its first window in layout order; floating windows
follow tiled windows. Window counts and click cycling exclude other workspaces.
Only apps with open windows appear—not background or tray-only processes.

## Icons and troubleshooting

- **Generic icon:** no matching desktop entry was found, or its `Icon=` uses an
  absolute file path. These cases use Otter's bundled `sparkles` icon.
- **Blank icon:** a matching desktop entry may name an icon missing from the
  selected theme. This ABI cannot report a failed icon lookup.
- **Newly installed app:** restart the bar to refresh the desktop-entry index.
- **Empty or delayed panel:** check the enable key, layout, host compatibility,
  and `NIRI_SOCKET` in the bar's environment. Try `niri msg --json windows` in a
  terminal inside your niri session. IPC parsing and protocol failures are reported
  through Otter’s host logger; check the bar’s captured logs or terminal output.
  See the refresh limitation above.
- **After restarting niri:** restart the bar from the new session if the socket
  path changed. Disconnects otherwise clear stale icons and retry every second.

Pinning, previews, tooltips, per-monitor workspace filtering, and animation are
not included.

## Build and test

```sh
./build.sh
./test.sh
```

`make` and `make test` are equivalent. The plugin needs a C++17 compiler; Python 3
runs the integration tests. No Zig toolchain or Otter SDK is needed to build the
plugin. The JSON dependency is vendored, so the plugin build works offline.

Tests cover grouping, layout order, centered coordinates, workspace filtering,
badges, focus requests, stale clicks, clipping, malformed events, reconnects and
unloading. Integration tests use a temporary Unix socket and a mock renderer;
they do not manipulate your desktop. See [validation notes](VALIDATION.md).

For a separate read-only probe against your real niri session:

```sh
make probe
./build/live-probe ./build/plugins/running-apps/libotter_plugin_running_apps.so
```

`make probe` only compiles the tool. Running it uses the session’s `NIRI_SOCKET`
and reports rendered cell counts without sending focus actions. Zero cells can
also mean an unavailable socket; it is a basic diagnostic, not a full UI test.

## Host integration

The refresh patch targets Otter Bar `v0.11.113`. Applying it changes **Otter Bar
itself**, not the plugin. Use a separate source checkout and keep your packaged
bar available for rollback.

From this repository's directory, in bash or another POSIX shell:

```sh
PLUGIN_DIR="$PWD"
git clone --branch v0.11.113 --depth 1 \
  https://git.pika-os.com/otter-shell/otter-bar.git ../otter-bar-running-apps
cd ../otter-bar-running-apps
git apply --check "$PLUGIN_DIR/patches/otter-bar-running-apps-refresh.patch"
git apply "$PLUGIN_DIR/patches/otter-bar-running-apps-refresh.patch"
zig build --fetch=all
zig build -Doptimize=ReleaseFast
zig build test
```

For fish, use `set PLUGIN_DIR "$PWD"` for the first line. This host release needs
**Zig 0.16.0**, network access for its pinned dependencies, and native development
libraries. Follow the [upstream build requirements](https://git.pika-os.com/otter-shell/otter-bar/src/tag/v0.11.113/README.md).
Prefetching avoids an upstream lazy-dependency initialization failure encountered
during the build; failed dependency downloads must be resolved before compiling.

After stopping your existing bar, run `./zig-out/bin/otter-bar` **from that source
checkout**. Keep its build cache: this release may embed relative paths to built
shared libraries. Merely restarting `/usr/bin/otter-bar` uses the packaged build.

The patch applies cleanly to the target release. A local host build containing
it passed upstream tests; on-screen behavior of that custom host is not fully
validated. Hidden Wayland surfaces may still delay redraws. The patch is a
workaround until Otter offers a suitable scheduling API.

**Updates:** a local patched build does not automatically receive package
updates. Recheck and rebuild the patch against newer source versions; never
force a patch that fails its check. Installing the plugin does not modify the
system bar or its update mechanism.

## Update or remove

To update the plugin, run `git pull`, then `./install.sh`, and restart the bar.

To remove it, delete `plugin:running-apps` from your layout, set
`running-apps_enabled = false`, and restart the bar. You can then delete the
user plugin directory. No system package is modified by the installer.

## Source and license

| Path | Purpose |
| --- | --- |
| `src/plugin.cpp` | Otter ABI, centered layout, badges and click handling |
| `src/core.hpp` | niri window state, grouping and focus selection |
| `src/ipc.hpp` | Nonblocking IPC, framing and reconnects |
| `src/icons.hpp` | Desktop-entry icon lookup |
| `tests/` | Model tests and compiled-plugin integration tests |
| `patches/` | Optional Otter host refresh patch |

[MIT licensed](LICENSE). The bundled Otter ABI header and nlohmann/json v3.12.0
retain their [Otter](include/OTTER-LICENSE) and [JSON](include/JSON-LICENSE)
license notices. Dependency checksums are in [VENDORED.sha256](VENDORED.sha256).

References: [Otter plugin API](https://docs.otter-shell.org/developers/plugins),
[Otter examples](https://git.pika-os.com/otter-shell/otter-shell-plugins),
[niri IPC](https://github.com/niri-wm/niri/wiki/IPC).
