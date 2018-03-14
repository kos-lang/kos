#!/bin/sh

set -e

OUT_DIR="$1"
DEST_DIR="$2"

UNAME=$(uname -s)

case "$UNAME" in
    CYGWIN*|MINGW*|MSYS*) UNAME=Windows ;;
esac

if [ "$DEST_DIR" = "${DEST_DIR#/}" ]; then
    if [ "$UNAME" != "Windows" -o "$DEST_DIR" = "${DEST_DIR#?:}" ]; then
        DEST_DIR="$(pwd)/../$DEST_DIR"
    fi
fi

echo "Install $DEST_DIR"

if [ "$UNAME" = "Windows" ]; then
    KOS_EXE="$OUT_DIR/interpreter/kos.exe"
    BIN_DIR="$DEST_DIR/Kos"
    MODULES_DIR="$DEST_DIR/Kos/modules"
else
    KOS_EXE="$OUT_DIR/interpreter/kos"
    BIN_DIR="$DEST_DIR/bin"
    MODULES_DIR="$DEST_DIR/share/kos/modules"
fi

install -d "$BIN_DIR"
install -d "$MODULES_DIR"
install -m 0755 "$KOS_EXE" "$BIN_DIR"
for FILE in $(ls ../modules/*.kos); do
    install -m 0644 "$FILE" "$MODULES_DIR"
done
