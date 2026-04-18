.PHONY: cert build-debug build-release test run-debug clean

cert:
	bash certs/gen_cert.sh

build-debug:
	cmake --preset debug
	cmake --build --preset debug

build-release:
	cmake --preset release
	cmake --build --preset release

test: build-debug
	ctest --preset debug

run-debug: build-debug
	./build/debug/bin/LoomicServer --config config/server.json

clean:
	rm -rf build/
