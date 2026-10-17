#!/bin/sh
# Installiert die Tonwerk-Plugins aus einem Linux-Release (liegt im Archiv neben den Plugins).
#
#   tar -xzf Tonwerk-Synth-<version>-Linux-x64.tar.gz && cd Tonwerk-Synth-<version>-Linux-x64 && ./install.sh
#
# VST3 nach ~/.vst3 (dort sucht jeder VST3-Host, Bitwig, Reaper, Ardour), die Apps nach ~/.local/bin, und für jede App
# ein Eintrag im Anwendungsmenü mit ihrem Symbol (~/.local/share/applications).
set -eu

cd "$(dirname "$0")"
mkdir -p "$HOME/.vst3" "$HOME/.local/bin"
for plugin in VST3/*.vst3; do
  rm -rf "$HOME/.vst3/$(basename "$plugin")"
  cp -R "$plugin" "$HOME/.vst3/"
done
cp Apps/* "$HOME/.local/bin/"

applications="$HOME/.local/share/applications"
icons="$HOME/.local/share/icons/tonwerk"
mkdir -p "$applications" "$icons"
for app in Apps/*; do
  name="$(basename "$app")"
  id="de.bvelop.$(printf '%s' "$name" | tr 'A-Z ' 'a-z-')"
  icon=""
  if [ -f "Icons/$name.png" ]; then
    cp "Icons/$name.png" "$icons/$id.png"
    icon="Icon=$icons/$id.png"
  fi
  # StartupWMClass is the window class JUCE gives the app (its name), so the dock shows the icon on its window.
  cat > "$applications/$id.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=$name
Exec="$HOME/.local/bin/$name"
$icon
Categories=AudioVideo;Audio;Music;
StartupWMClass=$name
Terminal=false
DESKTOP
done
if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database "$applications" || true; fi
echo "Installiert: VST3 in ~/.vst3, Apps in ~/.local/bin, im Anwendungsmenü unter Audio."
