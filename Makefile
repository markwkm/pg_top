default:
	@echo "targets: appimage (Linux only), clean, debug, package, release"

UNAME_S := $(shell uname -s)

appimage:
	cmake -H. -B_builds/appimage -DCMAKE_INSTALL_PREFIX=/usr
	cd _builds/appimage && make
	cd _builds/appimage && sed -i -e 's#/usr#././#g' pg_top
	cd _builds/appimage && make install DESTDIR=AppDir
	cd _builds/appimage && make appimage

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
