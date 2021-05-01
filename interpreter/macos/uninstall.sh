#!/bin/sh

ID=$(id -u)

if [ "$ID" != "0" ]; then
    echo "Kos uninstall script must be run with root privileges" >&2
    echo
    echo "Please run: sudo $0" >&2
    exit 1
fi

is_dir_empty()
{
    local NUM
    NUM=$(ls "$1" 2>/dev/null | wc -l)
    test $NUM -eq 0
}

INSTALL_ROOT=/usr/local

if [ -f "$INSTALL_ROOT"/bin/kos ]; then
    echo "Removing $INSTALL_ROOT/bin/kos"
    rm -f "$INSTALL_ROOT"/bin/kos
fi
if [ -d "$INSTALL_ROOT"/share/kos/modules ]; then
    if ! is_dir_empty "$INSTALL_ROOT"/share/kos/modules; then
        echo "Removing $INSTALL_ROOT/share/kos/modules/*"
        rm -f "$INSTALL_ROOT"/share/kos/modules/*
    fi
    if is_dir_empty "$INSTALL_ROOT"/share/kos/modules; then
        echo "Removing $INSTALL_ROOT/share/kos/modules"
        rmdir "$INSTALL_ROOT"/share/kos/modules
    fi
fi
if [ -f "$INSTALL_ROOT"/share/kos/uninstall.sh ]; then
    echo "Removing $INSTALL_ROOT/share/kos/uninstall.sh"
    rm -f "$INSTALL_ROOT"/share/kos/uninstall.sh
fi
if [ -d "$INSTALL_ROOT"/share/kos ]; then
    if is_dir_empty "$INSTALL_ROOT"/share/kos; then
        echo "Removing $INSTALL_ROOT/share/kos"
        rmdir "$INSTALL_ROOT"/share/kos
    fi
fi
