.PHONY: all test clean
all:
	./build.sh
test:
	./test.sh
clean:
	rm -rf build
