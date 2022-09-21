#!/bin/sh

set -e

# Check input
if [ $# -ne 1 ]; then
    echo "Error: Missing required version parameter!" >&2
    echo "Usage: verify_version.sh v1.2.3" >&2
    exit 1
fi

# Determine version being built
VERSION_MAJOR=0
VERSION_MINOR=1
VERSION_REVISION=0
VERSION="$1"
if echo "$VERSION" | grep -q "^v\?[0-9]\+\.[0-9]\+\.[0-9]\+$"; then
    VERSION_MAJOR="${VERSION%.*.*}"
    VERSION_MAJOR="${VERSION_MAJOR#v}"
    VERSION_MINOR="${VERSION#*.}"
    VERSION_REVISION="${VERSION_MINOR#*.}"
    VERSION_MINOR="${VERSION_MINOR%.*}"
else
    echo "Error: Invalid version: $VERSION" >&2
    echo "Expected version to be in the form: v1.2.3" >&2
    exit 1
fi
CHECK_VERSION="v$VERSION_MAJOR.$VERSION_MINOR.$VERSION_REVISION"
if [ "$VERSION" != "$CHECK_VERSION" ]; then
    echo "Error: Invalid version: $VERSION" >&2
    echo "Expected version $CHECK_VERSION" >&2
    exit 1
fi
echo "Verifying version $VERSION"

# Get version from sources
SRC_VERSION_MAJOR="$(grep 'define *KOS_VERSION_MAJOR' inc/kos_version.h | sed 's/.*MAJOR *//')"
SRC_VERSION_MINOR="$(grep 'define *KOS_VERSION_MINOR' inc/kos_version.h | sed 's/.*MINOR *//')"
SRC_VERSION_REVISION="$(grep 'define *KOS_VERSION_REVISION' inc/kos_version.h | sed 's/.*REVISION *//')"
SRC_VERSION="v$SRC_VERSION_MAJOR.$SRC_VERSION_MINOR.$SRC_VERSION_REVISION"
echo "Source version $SRC_VERSION"

if [ "$VERSION" != "$SRC_VERSION" ]; then
    echo "Error: Detected incorrect version in inc/kos_version.h!" >&2
    exit 1
fi
