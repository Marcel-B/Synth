#!/usr/bin/env bash
# Puts the website for tonwerk.studio together in one folder (default build/site): website/ plus the screenshots from
# docs/ as img/ (the app icons are in website/icons, written by scripts/make-icons.py), so the README and the page show the same pictures. Fails when a page links a file or picture that does not
# exist or leaves out a plugin the build makes, so a new plugin cannot be forgotten there. The pages of Tonwerk-AI
# (ai/, the app of the Tonwerk-AI repository) and of the Eurorack module (modul/, esp32-wavetable/) are checked too;
# Tonwerk-AI's icon is a copy of the app's. en/ holds the English pages beside their German ones (the build guide and
# the imprint stay German only).
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
# Every page's local links and pictures must exist, resolved from the page's folder (a folder means its index.html).
while read -r page; do
  for link in $(grep -oE '(href|src)="[^"]*"' "$page" | sed -E 's/^(href|src)="//; s/"$//; s/[#?].*//' \
                | grep -vE '^([a-z]+:|$)' | sort -u); do
    target="$(dirname "$page")/$link"
    [[ "$link" == */ ]] && target="${target}index.html"
    if [[ ! -e "$target" ]]; then
      echo "${page#"$site"/} verweist auf $link, das gibt es nicht." >&2
      failed=1
    fi
  done
done < <(find "$site" -name '*.html')

# The English pages (en/) mirror the German ones they name as their alternate; each German page links its English one.
for page in "$site"/en/*.html "$site"/en/ai/*.html "$site"/en/modul/*.html; do
  german="$site/${page#"$site"/en/}"
  if [[ ! -f "$german" ]]; then
    echo "${page#"$site"/} hat keine deutsche Seite." >&2
    failed=1
  elif ! grep -q 'hreflang="en"' "$german"; then
    echo "${german#"$site"/} verlinkt ihre englische Seite nicht." >&2
    failed=1
  fi
done

# Tonwerk-AI is handed out as a download, so every one of its pages leads to what it says about AI (ai/ki.html).
for page in "$site"/ai/*.html "$site"/en/ai/*.html; do
  if ! grep -q 'href="ki.html' "$page"; then
    echo "$(basename "$page") verlinkt ki.html nicht." >&2
    failed=1
  fi
done

# Every plugin of CMakeLists.txt but the test app, Tonwerk Synth (kept only for old projects) and Tonwerk (the iPad app,
# shown in the iPad download panel) needs its card.
while read -r product; do
  case "$product" in "Tonwerk"|"Tonwerk Synth"|"Tonwerk Synth Tests"|"\$"*) continue ;; esac
  for index in "$site/index.html" "$site/en/index.html"; do
    if ! grep -q "<h3>$product</h3>" "$index"; then
      echo "${index#"$site"/} stellt $product nicht vor." >&2
      failed=1
    fi
  done
done < <(grep -oE '(PRODUCT_NAME "[^"]+"|tonwerk_add_synth\([A-Za-z]+ "[^"]+")' "$root/CMakeLists.txt" | grep -oE '"[^"]+"' | tr -d '"')

if [[ $failed -ne 0 ]]; then exit 1; fi
echo "Website in $site"
