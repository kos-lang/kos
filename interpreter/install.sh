#!/bin/sh

set -e

OUT_DIR="$1"
DEST_DIR="$2"

UNAME=$(uname -s)

case "$UNAME" in
    CYGWIN*|MINGW*|MSYS*) UNAME=Windows ;;
esac

if [ "$DEST_DIR" = "${DEST_DIR#/}" ]; then
    if [ "$UNAME" != "Windows" ] || [ "$DEST_DIR" = "${DEST_DIR#?:}" ]; then
        DEST_DIR="$(pwd)/../$DEST_DIR"
    fi
fi

echo "Install $DEST_DIR"

if [ "$UNAME" = "Windows" ]; then
    KOS_EXE="$OUT_DIR/interpreter/kos.exe"
    SO_DIR="$OUT_DIR/interpreter"
    SO_EXT="dll"
    BIN_DIR="$DEST_DIR/Kos"
    MODULES_DIR="$DEST_DIR/Kos/modules"
else
    KOS_EXE="$OUT_DIR/interpreter/kos"
    SO_DIR="$OUT_DIR/share/kos/modules"
    if [ "$UNAME" = "Darwin" ]; then
        SO_EXT="dylib"
    else
        SO_EXT="so"
    fi
    BIN_DIR="$DEST_DIR/bin"
    MODULES_DIR="$DEST_DIR/share/kos/modules"
fi

install -d "$BIN_DIR"
install -d "$MODULES_DIR"
install -m 0755 "$KOS_EXE" "$BIN_DIR"
for FILE in ../modules/*.kos "$SO_DIR"/*.$SO_EXT; do
    install -m 0644 "$FILE" "$MODULES_DIR"
done
