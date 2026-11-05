#!/usr/bin/env bash
# Decides whether main needs a new release, for the nightly run (auto-release.yml).
# Usage: scripts/release-plan.sh [commit]   (default HEAD; needs the tags and full history)
# Prints `version=<major>.<minor>.<YYYYMMDD>` (the current version and the day, 1.3.20261106; Marcel's choice of
# 2026-11-05) and writes the release notes to release-notes.md when a change since the newest tag v* reaches the release
# packages; prints nothing when none does, or when today's release exists. A new plugin raises the minor version
# (1.3.x → 1.4.<day>). The plugins themselves are built with scripts/plugin-version.sh's number, since an Audio Unit's
# patch has only eight bits.
set -euo pipefail

head="${1:-HEAD}"
last=$(git tag --list 'v*' --sort=-v:refname | head -n 1)
if [ -z "$last" ]; then
    echo "No tag v* to count from." >&2
    exit 1
fi

# Paths that never reach the packages: the website, the Eurorack module, Tonwerk Komponist, the iPad and store app, CI, documents and
# the scripts that serve those.
reaches() {
    local left
    left=$(grep -vE '^(website/|esp32-wavetable/|komponist/|icons/TonwerkKomponist\.png$|docs/|app/|\.github/|\.claude/|scripts/(build-website|build-ios|release-stats|release-plan|plugin-version)\.sh$|scripts/make-icons\.py$|\.gitignore$)|\.md$' || true)
    [ -n "$left" ]
}

if ! git diff --name-only "$last" "$head" | reaches; then
    echo "Nothing for the packages since $last." >&2
    exit 0
fi

plugins() { git show "$1:CMakeLists.txt" | grep -cE '^\s*(juce_add_plugin|tonwerk_add_synth)\(' || true; }
IFS=. read -r major minor _ <<<"${last#v}"
if [ "$(plugins "$head")" -gt "$(plugins "$last")" ]; then
    minor=$((minor + 1))
fi
version="$major.$minor.${TONWERK_RELEASE_DAY:-$(date -u +%Y%m%d)}"
if git rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
    echo "v$version exists already; one release a day." >&2
    exit 0
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
