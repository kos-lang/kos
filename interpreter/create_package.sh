#!/bin/sh

set -e

if [ $# -gt 0 ]; then
    VERSION="$1"
    if [ "${VERSION%/*}" = "refs/tags" ]; then
        VERSION="${VERSION#*/*/}"
        if echo "$VERSION" | grep -q "^v[0-9]\+\.[0-9]\+$"; then
            VERSION_MAJOR="${VERSION%.*}"
            VERSION_MAJOR="${VERSION_MAJOR#v}"
            VERSION_MINOR="${VERSION#*.}"
            echo "Packaging version $VERSION_MAJOR.${VERSION_MINOR}"
            export version_major="$VERSION_MAJOR"
            export version_minor="$VERSION_MINOR"
        else
            echo "Ignoring invalid version: $VERSION"
            echo "Expected version to be in the form: v1.2"
        fi
    else
        echo "Ignoring invalid version parameter: $VERSION"
        echo "Expected version to be in the form: refs/tags/123"
    fi
fi

UNAME=$(uname -s)

case "$UNAME" in
    CYGWIN*|MINGW*|MSYS*) UNAME=Windows ;;
esac

if [ -z "$JOBS" ]; then
    case "$UNAME" in
        Darwin)  JOBS="$(sysctl -n hw.ncpu)" ;;
        Linux)   JOBS="$(grep -c ^processor /proc/cpuinfo)" ;;
        Windows) JOBS="$NUMBER_OF_PROCESSORS" ;;
    esac
fi

if ! test -d interpreter; then
    echo $(basename "$0") "must be run from root directory of the Kos project" >&2
    exit 1
fi

BUILDDIR=Out/release
PKGDIR="$BUILDDIR"/package

create_pkg_dir()
{
    rm -rf Out

    make -j "$JOBS" builtin_modules=0 strict=1 test

    local CP
    CP=ln
    [ "$UNAME" = "Windows" ] && CP=cp

    mkdir -p "$PKGDIR"/bin
    mkdir -p "$PKGDIR"/share/kos/modules
    "$CP" "$BUILDDIR"/interpreter/kos "$PKGDIR"/bin/
    "$CP" "$BUILDDIR"/share/kos/modules/* "$PKGDIR"/share/kos/modules/

    # Test the package
    unset KOSPATH
    "$PKGDIR"/bin/kos -c "import os; import re; import math;"
}

if [ "$UNAME" = "Darwin" ]; then
    create_pkg_dir
    productbuild --root "$PKGDIR" /usr/local --product interpreter/Kos.plist "$BUILDDIR"/kos.pkg
else
    echo "Unsupported OS '$UNAME'" >&2
    exit 1
fi
