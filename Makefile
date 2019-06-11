default:
	@echo "targets: clean, debug, package, release"

clean:
	-rm -rf _builds

debug:
	cmake -H. -B_builds/debug -DCMAKE_BUILD_TYPE=Debug
	cd _builds/debug && make

package:
	git checkout-index --prefix=_builds/source/ -a
	cmake -H_builds/source -B_builds/source
	cd _builds/source && make package_source

release:
	cmake -H. -B_builds/release
	cd _builds/release && make
