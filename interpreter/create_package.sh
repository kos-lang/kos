#!/bin/sh

set -e

# Determine version being built
VERSION_MAJOR=0
VERSION_MINOR=1
VERSION_REVISION=0
if [ $# -gt 0 ]; then
    VERSION="$1"
    if echo "$VERSION" | grep -q "^v\?[0-9]\+\.[0-9]\+\.[0-9]\+$"; then
        VERSION_MAJOR="${VERSION%.*.*}"
        VERSION_MAJOR="${VERSION_MAJOR#v}"
        VERSION_MINOR="${VERSION#*.}"
        VERSION_REVISION="${VERSION_MINOR#*.}"
        VERSION_MINOR="${VERSION_MINOR%.*}"
    else
        echo "Ignoring invalid version: $VERSION"
        echo "Expected version to be in the form: v1.2.3"
    fi
fi
VERSION="$VERSION_MAJOR.$VERSION_MINOR.$VERSION_REVISION"
echo "Packaging version $VERSION"

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

compile_kos()
{
    make -j "$JOBS" "$@" builtin_modules=0 strict=1 version_major="$VERSION_MAJOR" version_minor="$VERSION_MINOR" version_revision="$VERSION_REVISION"
}

collect_package()
{
    local BUILDDIR_LOCAL="$1"
    local PKGDIR_LOCAL="$1/package"

    if [ "$UNAME" = "Windows" ]; then
        mkdir -p "$PKGDIR_LOCAL"/modules
        cp "$BUILDDIR_LOCAL"/interpreter/kos.exe "$PKGDIR_LOCAL"/
        cp "$BUILDDIR_LOCAL"/interpreter/modules/* "$PKGDIR_LOCAL"/modules/
    else
        mkdir -p "$PKGDIR_LOCAL"/bin
        mkdir -p "$PKGDIR_LOCAL"/share/kos/modules
        ln "$BUILDDIR_LOCAL"/interpreter/kos "$PKGDIR_LOCAL"/bin/
        ln "$BUILDDIR_LOCAL"/share/kos/modules/* "$PKGDIR_LOCAL"/share/kos/modules/
    fi
}

create_pkg_dir()
{
    rm -rf Out

    compile_kos test

    collect_package "$BUILDDIR"

    local KOS
    if [ "$UNAME" = "Windows" ]; then
        KOS="$PKGDIR/kos.exe"
    else
        KOS="$PKGDIR/bin/kos"
    fi

    # Test the package
    unset KOSPATH
    cd Out
    ../"$KOS" --version
    ../"$KOS" -c "import os; import re; import math; print(os.sysname)"
    cd ..
}

pkg_sources()
{
    local PKGBASENAME="$1"
    local OUTDIR="$2"

    rm -rf "$PKGBASENAME"
    mkdir "$PKGBASENAME"
    cp -a *md Makefile build core doc inc interpreter modules tests tools "$PKGBASENAME"/

    sed -i -r "/define *KOS_VERSION_MAJOR/s/[0-9]+/$VERSION_MAJOR/" "$PKGBASENAME"/inc/kos_version.h
    sed -i -r "/define *KOS_VERSION_MINOR/s/[0-9]+/$VERSION_MINOR/" "$PKGBASENAME"/inc/kos_version.h
    sed -i -r "/define *KOS_VERSION_REVISION/s/[0-9]+/$VERSION_REVISION/" "$PKGBASENAME"/inc/kos_version.h

    tar czf "$OUTDIR"/"$PKGBASENAME".tar.gz "$PKGBASENAME"
    zip -r -9 -q "$OUTDIR"/"$PKGBASENAME".zip "$PKGBASENAME"

    rm -rf "$PKGBASENAME"

    cd "$OUTDIR"
    shasum -a 256 "$PKGBASENAME".tar.gz | tee "$PKGBASENAME".tar.gz.sha
    shasum -a 256 "$PKGBASENAME".zip | tee "$PKGBASENAME".zip.sha
    cd - > /dev/null
}

# Create MacOS package
if [ "$UNAME" = "Darwin" ]; then
    create_pkg_dir

    cp interpreter/macos/uninstall.sh "$PKGDIR"/share/kos

    PKGNAME_AMD64="$PKGNAME-macos-x86_64"
    productbuild --root "$PKGDIR" /usr/local --product interpreter/macos/kos-x86_64.plist "$BUILDDIR"/"$PKGNAME_AMD64.pkg"
    cd "$BUILDDIR"
    shasum -a 256 "$PKGNAME_AMD64.pkg" | tee "$PKGNAME_AMD64.pkg.sha"
    cd - >/dev/null

# Create Linux package
elif [ "$UNAME" = "Linux" ]; then
    create_pkg_dir

    pkg_sources "$PKGNAME"-src "$BUILDDIR"

    PKGNAME="$PKGNAME-linux-amd64"

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

# Create Windows package
elif [ "$UNAME" = "Windows" ]; then
    create_pkg_dir
    PKGNAME="$PKGNAME-windows-x86"

    sed "s/0\.1/$VERSION/" < interpreter/windows/kos.wxs > "$PKGDIR"/"$PKGNAME.wxs"
    cp interpreter/artwork/kos.ico "$PKGDIR"/
    cp interpreter/artwork/kos_installer_top_banner.bmp "$PKGDIR"/
    cp interpreter/artwork/kos_installer_dialog.bmp "$PKGDIR"/
    cp interpreter/windows/license.rtf "$PKGDIR"/

    cd "$PKGDIR"
    MODDEFS=""
    MODREFS=""
    for MOD in modules/*; do
        MOD="$(echo "$MOD" | sed "s/modules\///")"
        MODDEFS="$MODDEFS<Component Id='${MOD}.Component' Guid='$(uuidgen)'>"
        MODDEFS="$MODDEFS<File Id='$MOD' Name='$MOD' DiskId='1' Source='modules\\\\$MOD' KeyPath='yes' \\/>"
        MODDEFS="$MODDEFS<\\/Component>"
        MODREFS="$MODREFS<ComponentRef Id='${MOD}.Component' \\/>"
    done
    sed -i "s/^.*MODULE_COMPONENT_DEFS.*$/$MODDEFS/ ; s/^.*MODULE_COMPONENT_REFS.*$/$MODREFS/" "$PKGNAME.wxs"

    candle.exe "$PKGNAME.wxs"
    light.exe -ext WixUIExtension "$PKGNAME.wixobj"

    mv "$PKGNAME.msi" ..
    cd ..
    shasum -a 256 "$PKGNAME.msi" | tee "$PKGNAME.msi.sha"
else
    echo "Unsupported OS '$UNAME'" >&2
    exit 1
fi
