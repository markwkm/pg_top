.PHONY: appimage clean default debug package release

default:
	@echo "targets: appimage (Linux only), clean, debug, package, release"

appimage:
	cmake -H. -Bbuilds/appimage -DCMAKE_INSTALL_PREFIX=/usr
	cd builds/appimage && make
	cd builds/appimage && sed -i -e 's#/usr#././#g' pg_top
	cd builds/appimage && make install DESTDIR=AppDir
	cd builds/appimage && make appimage

clean:
	-rm -rf builds

debug:
	cmake -H. -Bbuilds/debug -DCMAKE_BUILD_TYPE=Debug
	cd builds/debug && make

package:
	git checkout-index --prefix=builds/source/ -a
	cmake -Hbuilds/source -Bbuilds/source
	cd builds/source && make package_source

release:
	cmake -H. -Bbuilds/release
	cd builds/release && make
