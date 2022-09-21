#!/bin/sh

set -e

# Check input
if [ $# -ne 1 ] && [ $# -ne 3 ]; then
    echo "Missing required version parameter!" >&2
    echo "Usage: tag_release.sh v1.2.3 [-m MESSAGE | -F FILE]" >&2
    exit 1
fi
if [ $# -eq 3 ]; then
    if [ "$2" != "-m" ] && [ "$2" != "-F" ]; then
        echo "Error: Invalid argument 2: $2" >&2
        echo "Expected -m or -F" >&2
        exit 1
    fi
    if [ "$2" = "-F" ] && [ ! -f "$3" ]; then
        echo "Error: File $3 does not exist" >&2
        exit 1
    fi
fi

VERSION="$1"
interpreter/set_version.sh "$VERSION"
interpreter/verify_version.sh "$VERSION"
echo "Tagging release $VERSION"

TAG_OPTION="-m"
TAG_COMMENT="Release $VERSION"
if [ $# -eq 3 ]; then
    TAG_OPTION="$2"
    TAG_COMMENT="$3"
fi

git tag -a "$TAG_OPTION" "$TAG_COMMENT" "$VERSION"
