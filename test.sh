#!/bin/sh
set -eu
cd "$(dirname "$0")"
./build.sh
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc tests/core.cpp -o build/test-core
./build/test-core
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc tests/ipc.cpp -o build/test-ipc
./build/test-ipc
python3 tests/integration.py
