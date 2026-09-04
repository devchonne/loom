# Maintainer: devchonne
pkgname=loom
pkgver=0.1.0 # x-release-please-version
pkgrel=1
pkgdesc="Minimal retro markdown scratchpad"
arch=('x86_64')
url="https://github.com/devchonne/loom"
license=('MIT')
depends=('qt6-base' 'qt6-multimedia' 'qt6-wayland' 'tomlplusplus' 'md4c')
makedepends=('cmake' 'ninja' 'gcc' 'pkgconf' 'gtest')
source=()
sha256sums=()

# ./src is the C++ tree. If makepkg used the repo as BUILDDIR, `makepkg -C`
# would delete it. Move the work tree out of the repo when the caller did not
# set BUILDDIR themselves.
if [[ "$BUILDDIR" -ef "$startdir" ]]; then
  export BUILDDIR="${TMPDIR:-/tmp}/loom-pkgbuild"
  mkdir -p "$BUILDDIR"
fi

build() {
  cmake -S "$startdir" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}

check() {
  ctest --test-dir build --output-on-failure
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
