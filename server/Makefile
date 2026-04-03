.PHONY: cert build-debug build-release test clean

cert:
	bash certs/gen_cert.sh

build-debug:
	cmake --preset debug
	cmake --build --preset debug

build-release:
	cmake --preset release
	cmake --build --preset release

test: build-debug
	cd build/debug && ctest --output-on-failure

clean:
	rm -rf build/
