#!/bin/sh

set -e

out_dir="$1"
DESTDIR="$2"

UNAME=$(uname -s)

case "$UNAME" in
    CYGWIN*|MINGW*|MSYS*) UNAME=Windows ;;
esac

if [ "$DESTDIR" = "${DESTDIR#/}" ]; then
    if [ "$UNAME" != "Windows" -o "$DESTDIR" = "${DESTDIR#?:}" ]; then
        DESTDIR="$(pwd)/../$DESTDIR"
    fi
fi

echo "Install $DESTDIR"

if [ "$UNAME" = "Windows" ]; then
    KOS_EXE="$out_dir/interpreter/kos.exe"
    BIN_DIR="$DESTDIR/Kos"
    MODULES_DIR="$DESTDIR/Kos/modules"
else
    KOS_EXE="$out_dir/interpreter/kos"
    BIN_DIR="$DESTDIR/bin"
    MODULES_DIR="$DESTDIR/share/kos/modules"
fi

install -d "$BIN_DIR"
install -d "$MODULES_DIR"
install -m 0755 "$KOS_EXE" "$BIN_DIR"
for FILE in $(ls ../modules/*.kos); do
    install -m 0644 "$FILE" "$MODULES_DIR"
done
