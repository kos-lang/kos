#!/bin/sh

set -e

# Check input
if [ $# -ne 1 ]; then
    echo "Error: Missing required version parameter!" >&2
    echo "Usage: verify_version.sh v1.2.3" >&2
    exit 1
fi

# Check if there's anything to commit
NUM_CHANGES="$(git status --porcelain | grep -v inc/kos_version.h | wc -l)"
if [ $NUM_CHANGES != 0 ]; then
    echo "Error: Local files have been changed" >&2
    echo "Remove all changes before setting version" >&2
    echo "Found $NUM_CHANGES changes" >&2
    exit 1
fi

# Determine version
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
echo "Setting version $VERSION"

# Function to set version
set_version()
{
    local SCRIPT
    SCRIPT="/define *KOS_VERSION_$1/s/[0-9][0-9]*/$2/"

    local UNAME
    UNAME="$(uname)"

    if [ "$UNAME" = "Darwin" ] || [ "$UNAME" = "FreeBSD" ]; then
        sed -i "" "$SCRIPT" inc/kos_version.h
    else
        sed -i "$SCRIPT" inc/kos_version.h
    fi
}

# Set version in version.h
set_version MAJOR "$VERSION_MAJOR"
set_version MINOR "$VERSION_MINOR"
set_version REVISION "$VERSION_REVISION"

git add inc/kos_version.h
git commit -m "core: release $VERSION"
