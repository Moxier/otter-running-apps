#!/bin/sh
set -eu
cd "$(dirname "$0")"
./build.sh
root=${XDG_DATA_HOME:-"$HOME/.local/share"}/otter-shell/plugins
if [ "$#" -gt 0 ]; then root=$1; fi
target=$root/running-apps
mkdir -p "$target"
# Replace the .so by rename, never truncate a library a running bar has mapped.
stage=$(mktemp -d "$target/.install-XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
cp build/plugins/running-apps/libotter_plugin_running_apps.so "$stage/"
cp plugin.conf "$stage/"
chmod 755 "$stage/libotter_plugin_running_apps.so"
chmod 644 "$stage/plugin.conf"
mv -f "$stage/libotter_plugin_running_apps.so" "$target/"
mv -f "$stage/plugin.conf" "$target/"
printf 'Installed to %s\nRestart otter-bar after adding plugin:running-apps to your layout.\n' "$target"
