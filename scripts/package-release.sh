#!/bin/bash
# Packt die VST3-Plugins und Standalone-Apps eines Linux- oder Windows-Builds für ein Release.
#
#   scripts/package-release.sh linux|windows VERSION [BUILD_DIR]
#
# Nimmt jedes Plugin, das im Build liegt (build/*_artefacts/Release), so dass ein neues Plugin ohne Änderung hier mitkommt.
# Linux: Tonwerk-Synth-VERSION-Linux-x64.tar.gz (tar, damit die Apps ausführbar bleiben), mit install-linux.sh als install.sh.
# Windows: Tonwerk-Synth-VERSION-Windows-x64.zip.
set -euo pipefail

platform="$1"
version="$2"
build="${3:-build}"
scripts="$(cd "$(dirname "$0")" && pwd)"

case "$platform" in
  linux) dir="Tonwerk-Synth-$version-Linux-x64" ;;
  windows) dir="Tonwerk-Synth-$version-Windows-x64" ;;
  *) echo "Plattform linux oder windows, nicht $platform" >&2; exit 1 ;;
esac

rm -rf "$dir"
mkdir -p "$dir/VST3" "$dir/Apps"
count=0
for artefacts in "$build"/*_artefacts/Release; do
  [[ -d "$artefacts/VST3" ]] || continue
  cp -R "$artefacts"/VST3/*.vst3 "$dir/VST3/"
  count=$((count + 1))
  # The standalone app is one executable (Linux) or .exe (Windows) next to nothing else.
  for app in "$artefacts"/Standalone/*; do
    if [[ -f "$app" ]]; then cp "$app" "$dir/Apps/"; fi
  done
done
if [[ $count -eq 0 ]]; then
  echo "Keine Plugins unter $build/*_artefacts/Release gefunden." >&2
  exit 1
fi
echo "$count Plugins gepackt."

if [[ "$platform" == linux ]]; then
  cp "$scripts/install-linux.sh" "$dir/install.sh"
  chmod +x "$dir/install.sh" "$dir"/Apps/*
  tar -czf "$dir.tar.gz" "$dir"
  echo "$dir.tar.gz"
else
  7z a -tzip -bso0 "$dir.zip" "$dir" >/dev/null
  echo "$dir.zip"
fi
