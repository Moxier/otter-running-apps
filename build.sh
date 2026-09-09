#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build/plugins/running-apps
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -fPIC -fvisibility=hidden \
  -Iinclude -Isrc -shared src/plugin.cpp -Wl,-z,relro,-z,now -Wl,--no-undefined \
  -o build/plugins/running-apps/libotter_plugin_running_apps.so
cp plugin.conf build/plugins/running-apps/
printf '%s\n' 'Built build/plugins/running-apps/'
