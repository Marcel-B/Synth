#!/usr/bin/env bash
# Puts the website for tonwerk.studio together in one folder (default build/site): website/ plus the screenshots from
# docs/ as img/ (the app icons are in website/icons, written by scripts/make-icons.py), so the README and the page show the same pictures. Fails when the page names a picture that does not
# exist or leaves out a plugin the build makes, so a new plugin cannot be forgotten there. The pages of Tonwerk-AI
# (ai/, the app of the YuE-UI repository) are checked for their pictures too; its icon is a copy of the app's.
#
#   scripts/build-website.sh [folder]
#   python3 -m http.server -d build/site 8000     # to look at it locally
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
site="${1:-$root/build/site}"

rm -rf "$site"
mkdir -p "$site/img"
cp -R "$root"/website/. "$site/"
cp "$root"/docs/*.png "$site/img/"

failed=0
for page in "$site"/*.html "$site"/ai/*.html; do
  for picture in $(grep -oE '(img|icons)/[A-Za-z0-9._-]*' "$page" | sort -u); do
    if [[ ! -f "$site/$picture" ]]; then
      echo "$(basename "$page") zeigt $picture, das gibt es nicht." >&2
      failed=1
    fi
  done
done

# Every plugin of CMakeLists.txt but the test app, Tonwerk Synth (kept only for old projects) and Tonwerk (the iPad app,
# shown in the iPad download panel) needs its card.
while read -r product; do
  case "$product" in "Tonwerk"|"Tonwerk Synth"|"Tonwerk Synth Tests"|"\$"*) continue ;; esac
  if ! grep -q "<h3>$product</h3>" "$site/index.html"; then
    echo "index.html stellt $product nicht vor." >&2
    failed=1
  fi
done < <(grep -oE '(PRODUCT_NAME "[^"]+"|tonwerk_add_synth\([A-Za-z]+ "[^"]+")' "$root/CMakeLists.txt" | grep -oE '"[^"]+"' | tr -d '"')

if [[ $failed -ne 0 ]]; then exit 1; fi
echo "Website in $site"
