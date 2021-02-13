#!/bin/sh

set -e

# Determine version being built
VERSION_MAJOR=0
VERSION_MINOR=1
if [ $# -gt 0 ]; then
    VERSION="$1"
    if [ "${VERSION%/*}" = "refs/tags" ]; then
        VERSION="${VERSION#*/*/}"
        if echo "$VERSION" | grep -q "^v[0-9]\+\.[0-9]\+$"; then
            VERSION_MAJOR="${VERSION%.*}"
            VERSION_MAJOR="${VERSION_MAJOR#v}"
            VERSION_MINOR="${VERSION#*.}"
            echo "Packaging version $VERSION_MAJOR.${VERSION_MINOR}"
        else
            echo "Ignoring invalid version: $VERSION"
            echo "Expected version to be in the form: v1.2"
        fi
    else
        echo "Ignoring invalid version parameter: $VERSION"
        echo "Expected version to be in the form: refs/tags/123"
    fi
fi
VERSION="$VERSION_MAJOR.$VERSION_MINOR"

# Determine OS
UNAME=$(uname -s)

case "$UNAME" in
    CYGWIN*|MINGW*|MSYS*) UNAME=Windows ;;
esac

# Determine number of jobs
if [ -z "$JOBS" ]; then
    case "$UNAME" in
        Darwin)  JOBS="$(sysctl -n hw.ncpu)" ;;
        Linux)   JOBS="$(grep -c ^processor /proc/cpuinfo)" ;;
        Windows) JOBS="$NUMBER_OF_PROCESSORS" ;;
    esac
fi
echo "Using $JOBS jobs"

# Check if we're in the right directory
if ! test -d interpreter; then
    echo $(basename "$0") "must be run from root directory of the Kos project" >&2
    exit 1
fi

BUILDDIR=Out/release
PKGDIR="$BUILDDIR"/package
PKGNAME="kos-$VERSION"

create_pkg_dir()
{
    rm -rf Out

    make -j "$JOBS" test builtin_modules=0 strict=1 version_major="$VERSION_MAJOR" version_minor="$VERSION_MINOR"

    local KOS
    if [ "$UNAME" = "Windows" ]; then
        KOS="$PKGDIR/kos.exe"
        mkdir -p "$PKGDIR"/modules
        cp "$BUILDDIR"/interpreter/kos.exe "$PKGDIR"/
        cp "$BUILDDIR"/interpreter/modules/* "$PKGDIR"/modules/
    else
        KOS="$PKGDIR/bin/kos"
        mkdir -p "$PKGDIR"/bin
        mkdir -p "$PKGDIR"/share/kos/modules
        ln "$BUILDDIR"/interpreter/kos "$PKGDIR"/bin/
        ln "$BUILDDIR"/share/kos/modules/* "$PKGDIR"/share/kos/modules/
    fi

    # Test the package
    unset KOSPATH
    cd Out
    ../"$KOS" --version
    ../"$KOS" -c "import os; import re; import math; print(os.sysname)"
    cd ..
}

if [ "$UNAME" = "Darwin" ]; then
    create_pkg_dir
    productbuild --root "$PKGDIR" /usr/local --product interpreter/macos/kos.plist "$BUILDDIR"/"$PKGNAME.pkg"
    cd "$BUILDDIR"
    shasum -a 256 "$PKGNAME.pkg" | tee "$PKGNAME.pkg.sha"
elif [ "$UNAME" = "Linux" ]; then
    create_pkg_dir

    mkdir "$PKGDIR"/usr
    mv "$PKGDIR"/bin "$PKGDIR"/share "$PKGDIR"/usr/

    PKGSIZE=$(du -sk "$PKGDIR" | awk '{print $1}')

    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64) ARCH=amd64 ;;
    esac

    mkdir -p "$PKGDIR"/DEBIAN

    cd "$PKGDIR"
    find usr -type f | xargs md5sum > DEBIAN/md5sums
    cd - > /dev/null

    CTRLSCRIPT="/^Version:/s/:.*/: $VERSION/"
    CTRLSCRIPT="$CTRLSCRIPT;/^Installed-Size:/s/:.*/: $PKGSIZE/"
    CTRLSCRIPT="$CTRLSCRIPT;/^Architecture:/s/:.*/: $ARCH/"
    echo "Sed '$CTRLSCRIPT'"

    sed "$CTRLSCRIPT" < interpreter/debian/control > "$PKGDIR"/DEBIAN/control

    dpkg -b "$PKGDIR" "$BUILDDIR"/"$PKGNAME.deb"

    cd "$BUILDDIR"
    shasum -a 256 "$PKGNAME.deb" | tee "$PKGNAME.deb.sha"
elif [ "$UNAME" = "Windows" ]; then
    create_pkg_dir

    sed "/^\!define VERSION/s/\".*\"/\"$VERSION\"/" < interpreter/windows/kos.nsi > "$PKGDIR"/kos.nsi

    cd "$BUILDDIR"
    ls -lR package
    'C:/Program Files (x86)/NSIS/makensis.exe' 'package\kos.nsi'
    ls -lR

    shasum "Kos-${VERSION}.exe" "Kos-${VERSION}.exe.sha"
else
    echo "Unsupported OS '$UNAME'" >&2
    exit 1
fi
