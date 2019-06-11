default:
	@echo "targets: clean, debug, release"

clean:
	-rm -rf _builds

debug:
	cmake -H. -B_builds/debug -DCMAKE_BUILD_TYPE=Debug
	cd _builds/debug && make

release:
	cmake -H. -B_builds/release
	cd _builds/release && make
