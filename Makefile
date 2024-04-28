# Makefile! Just a launcher for scripts...

all: test release

clean:
	rm -r build_cmake

clean_cache:
	# Clear CMake caches. May be necessary after upgrading Xcode.
	rm -rf build_cmake/*/CMake*


debug:
	mkdir -p build_cmake/debug/
	cd build_cmake/debug && cmake -DCMAKE_BUILD_TYPE=Debug ../..
	cd build_cmake/debug && cmake --build .

test: debug
	cd build_cmake/debug && ./sqnice_tests

release:
	mkdir -p build_cmake/release/
	cd build_cmake/release && cmake -DCMAKE_BUILD_TYPE=MinSizeRel ../..
	cd build_cmake/release && cmake --build . --target sqnice
