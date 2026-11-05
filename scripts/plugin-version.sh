#!/usr/bin/env bash
# The version the plugins are built with, for a release version. Usage: scripts/plugin-version.sh <x.y.z>
# Nightly releases are named <major>.<minor>.<YYYYMMDD> (1.3.20261106), but an Audio Unit's version is one 32-bit
# number with eight bits for the patch (JUCE: major << 16 | minor << 8 | patch) and a Windows file version has 16
# bits per part, so a date there would overflow. A patch over 255 therefore becomes the count of this minor's releases
# before it (tags v<major>.<minor>.* on origin with a smaller patch): 1.3.0, then 1.3.1, 1.3.2 ... per night, so Logic
# still sees every release as newer than the last. A patch up to 255 (1.3.0 by hand) stays as it is.
set -euo pipefail

version="${1:?Usage: $0 <x.y.z>}"
IFS=. read -r major minor patch <<<"$version"
if (( patch <= 255 )); then
    echo "$version"
    exit 0
fi

count=$(git ls-remote --tags --refs origin "v$major.$minor.*" | sed 's#.*refs/tags/v##' \
    | awk -F. -v p="$patch" '$3 ~ /^[0-9]+$/ && $3 + 0 < p + 0' | wc -l)
(( count > 255 )) && count=255
echo "$major.$minor.$count"
