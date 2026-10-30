#!/usr/bin/env bash
# Puts the website for tonwerk.studio together in one folder (default build/site): website/ plus the screenshots from
# docs/ as img/ (the app icons are in website/icons, written by scripts/make-icons.py), so the README and the page show the same pictures. Fails when the page names a picture that does not
# exist or leaves out a plugin the build makes, so a new plugin cannot be forgotten there. The pages of Tonwerk-AI
# (ai/, the app of the Tonwerk-AI repository) and of the Eurorack module (modul/, esp32-wavetable/) are checked for
# their pictures too; Tonwerk-AI's icon is a copy of the app's. Every page has an English copy under en/ with the same
# file name and anchors; the check fails when one of a pair is missing.
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
pages=("$site"/*.html "$site"/ai/*.html "$site"/modul/*.html "$site"/en/*.html "$site"/en/ai/*.html "$site"/en/modul/*.html)
for page in "${pages[@]}"; do
  # A picture is looked for where its relative path leads from the page (modul/img/..., ../img/... from en/), then at
  # the top (img/ from docs/, icons/).
  for picture in $(grep -oE '"[./]*(img|icons)/[A-Za-z0-9._-]*' "$page" | tr -d '"' | sort -u); do
    if [[ ! -f "$(dirname "$page")/$picture" && ! -f "$site/${picture##*../}" ]]; then
      echo "${page#"$site"/} zeigt $picture, das gibt es nicht." >&2
      failed=1
    fi
  done
done

# The German pages and their English copies come in pairs.
for page in "$site"/*.html "$site"/ai/*.html "$site"/modul/*.html; do
  relative="${page#"$site"/}"
  if [[ ! -f "$site/en/$relative" ]]; then
    echo "$relative hat keine englische Fassung unter en/$relative." >&2
    failed=1
  fi
done
for page in "$site"/en/*.html "$site"/en/ai/*.html "$site"/en/modul/*.html; do
  relative="${page#"$site"/en/}"
  if [[ ! -f "$site/$relative" ]]; then
    echo "en/$relative hat keine deutsche Fassung $relative." >&2
    failed=1
  fi
done

# Tonwerk-AI is handed out as a download, so every one of its pages leads to what it says about AI (ai/ki.html).
for page in "$site"/ai/*.html "$site"/en/ai/*.html; do
  if ! grep -q 'href="ki.html' "$page"; then
    echo "${page#"$site"/} verlinkt ki.html nicht." >&2
    failed=1
  fi
done

# Every plugin of CMakeLists.txt but the test app, Tonwerk Synth (kept only for old projects) and Tonwerk (the iPad app,
# shown in the iPad download panel) needs its card, on the German page and on the English one.
while read -r product; do
  case "$product" in "Tonwerk"|"Tonwerk Synth"|"Tonwerk Synth Tests"|"\$"*) continue ;; esac
  for index in index.html en/index.html; do
    if ! grep -q "<h3>$product</h3>" "$site/$index"; then
      echo "$index stellt $product nicht vor." >&2
      failed=1
    fi
  done
done < <(grep -oE '(PRODUCT_NAME "[^"]+"|tonwerk_add_synth\([A-Za-z]+ "[^"]+")' "$root/CMakeLists.txt" | grep -oE '"[^"]+"' | tr -d '"')

if [[ $failed -ne 0 ]]; then exit 1; fi
echo "Website in $site"
