default:
	@echo "targets: appimage (Linux only), clean, debug, package, release"

UNAME_S := $(shell uname -s)

appimage-prep:
	cmake -H. -Bbuild/appimage -DCMAKE_INSTALL_PREFIX=/usr
	cd build/appimage && make
	cd build/appimage && sed -i -e 's#/usr#././#g' pg_top
	cd build/appimage && make install DESTDIR=AppDir

appimage: appimage-prep
	cd build/appimage && make appimage

appimage-podman: appimage-prep
	cd build/appimage && make appimage-podman

clean:
	-rm -rf build

debug:
	cmake -H. -Bbuild/debug -DCMAKE_BUILD_TYPE=Debug
	cd build/debug && make

package:
	git checkout-index --prefix=build/source/ -a
	cmake -Hbuild/source -Bbuild/source
	cd build/source && make package_source

release:
	cmake -H. -Bbuild/release
	cd build/release && make
