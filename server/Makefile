.PHONY: cert build-debug build-release test clean

cert:
	bash certs/gen_cert.sh

build-debug:
	conan install . --output-folder=build/debug --build=missing -s build_type=Debug
	cmake --preset debug
	cmake --build --preset debug

build-release:
	conan install . --output-folder=build/release --build=missing -s build_type=Release
	cmake --preset release
	cmake --build --preset release

test: build-debug
	cd build/debug && ctest --output-on-failure

clean:
	rm -rf build/
