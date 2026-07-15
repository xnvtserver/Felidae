#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERSION="${FELIDAE_VERSION:-0.1.0}"
ARCH="$(uname -m)"
DIST_DIR="$ROOT_DIR/dist"
PKG_ROOT="$DIST_DIR/pkgroot"

need_cmd() { command -v "$1" >/dev/null 2>&1; }

detect_os_id() {
    if [[ -r /etc/os-release ]]; then . /etc/os-release; echo "${ID:-linux}"; else echo "linux"; fi
}

require_binaries() {
    [[ -x build/felidae && -x build/celidae ]] || { echo "Run ./build.sh before packaging." >&2; exit 1; }
}

stage_tree() {
    rm -rf "$PKG_ROOT"
    mkdir -p "$PKG_ROOT/usr/bin" "$PKG_ROOT/usr/share/doc/felidae"
    cp build/felidae "$PKG_ROOT/usr/bin/felidae"
    cp build/celidae "$PKG_ROOT/usr/bin/celidae"
    [[ -x build/felidae_debug ]] && cp build/felidae_debug "$PKG_ROOT/usr/bin/felidae_debug"
    cp README.md docs_language.md docs_native_modules.md "$PKG_ROOT/usr/share/doc/felidae/"
}

package_tarball() {
    local name="felidae-${VERSION}-linux-${ARCH}.tar.gz"
    mkdir -p "$DIST_DIR"
    tar -C "$PKG_ROOT" -czf "$DIST_DIR/$name" .
    echo "Created dist/$name"
}

package_deb() {
    need_cmd dpkg-deb || return 0
    local debroot="$DIST_DIR/debroot"
    rm -rf "$debroot"
    mkdir -p "$debroot/DEBIAN"
    cp -R "$PKG_ROOT/"* "$debroot/"
    cat > "$debroot/DEBIAN/control" <<EOF
Package: felidae
Version: $VERSION
Section: devel
Priority: optional
Architecture: amd64
Maintainer: Felidae Project <noreply@github.com>
Description: Felidae logic language runtime and Celidae diagnostics host
EOF
    dpkg-deb --build "$debroot" "$DIST_DIR/felidae_${VERSION}_amd64.deb" >/dev/null
    echo "Created dist/felidae_${VERSION}_amd64.deb"
}

package_rpm() {
    need_cmd rpmbuild || return 0
    echo "rpmbuild found; create RPM packaging from staged files in a release job."
}

package_arch() {
    need_cmd makepkg || return 0
    echo "makepkg found; create Arch packaging from staged files in a release job."
}

require_binaries
stage_tree
package_tarball
case "$(detect_os_id)" in
    debian|ubuntu|linuxmint|pop) package_deb ;;
    fedora|opensuse*|sles) package_rpm ;;
    arch|manjaro) package_arch ;;
    *) echo "Created generic tarball. Install distro packaging tools for native package output." ;;
esac
