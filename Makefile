.PHONY: all test clean probe
all:
	./build.sh
test:
	./test.sh
probe: build/live-probe
build/live-probe: tests/probe.cpp include/otter_plugin_abi.h
	mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude tests/probe.cpp -ldl -o $@
clean:
	rm -rf build
