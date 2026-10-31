#!/usr/bin/env bash
# Decides whether main needs a new release, for the nightly run (auto-release.yml).
# Usage: scripts/release-plan.sh [commit]   (default HEAD; needs the tags and full history)
# Prints `version=<x.y.z>` and writes the release notes to release-notes.md when a change since the newest tag v*
# reaches the release packages; prints nothing when none does. A new plugin raises the minor version, anything else
# the patch.
set -euo pipefail

head="${1:-HEAD}"
last=$(git tag --list 'v*' --sort=-v:refname | head -n 1)
if [ -z "$last" ]; then
    echo "No tag v* to count from." >&2
    exit 1
fi

# Paths that never reach the packages: the website, the Eurorack module, the iPad and store app, CI, documents and
# the scripts that serve those.
reaches() {
    local left
    left=$(grep -vE '^(website/|esp32-wavetable/|docs/|app/|\.github/|\.claude/|scripts/(build-website|build-ios|release-stats|release-plan)\.sh$|scripts/make-icons\.py$|\.gitignore$)|\.md$' || true)
    [ -n "$left" ]
}

if ! git diff --name-only "$last" "$head" | reaches; then
    echo "Nothing for the packages since $last." >&2
    exit 0
fi

plugins() { git show "$1:CMakeLists.txt" | grep -cE '^\s*(juce_add_plugin|tonwerk_add_synth)\(' || true; }
IFS=. read -r major minor patch <<<"${last#v}"
if [ "$(plugins "$head")" -gt "$(plugins "$last")" ]; then
    version="$major.$((minor + 1)).0"
else
    version="$major.$minor.$((patch + 1))"
fi

# One line per merged pull request that changed something in the packages, oldest first; its title is the merge
# commit's body (GitHub's default), else the subject.
{
    echo "Neu seit ${last#v}:"
    echo
    git log --first-parent --reverse --format='%H' "$last..$head" | while read -r commit; do
        if git rev-parse -q --verify "$commit^2" >/dev/null; then
            git diff --name-only "$commit^1" "$commit" | reaches || continue
            number=$(git log -1 --format='%s' "$commit" | grep -oE '#[0-9]+' | head -n 1 || true)
            title=$(git log -1 --format='%b' "$commit" | sed -n '/./{p;q}')
            [ -n "$title" ] || title=$(git log -1 --format='%s' "$commit")
        else
            git diff --name-only "$commit^" "$commit" | reaches || continue
            number=""
            title=$(git log -1 --format='%s' "$commit")
        fi
        echo "- $title${number:+ ($number)}"
    done
} >release-notes.md

echo "version=$version"
