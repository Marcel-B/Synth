#!/bin/zsh
# Baut Tonwerk Synth und den Effekt Chrome Glitch auf dem Mac und installiert beide für Logic (Audio Unit) und andere
# Hosts (VST3).
# Braucht nur die Command Line Tools (xcode-select --install) und CMake (brew install cmake), kein volles Xcode.
#
#   scripts/install-macos.sh            bauen, signieren, installieren, mit auval prüfen
#   scripts/install-macos.sh --uninstall
set -euo pipefail

cd "$(dirname "$0")/.."
NAME="Tonwerk Synth"
GLITCH="Chrome Glitch"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"

if [[ "${1:-}" == "--uninstall" ]]; then
  rm -rf "$COMPONENTS/$NAME.component" "$VST3/$NAME.vst3" "$APPS/$NAME.app"
  rm -rf "$COMPONENTS/$GLITCH.component" "$VST3/$GLITCH.vst3" "$APPS/$GLITCH.app"
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "Entfernt. Die Klänge in ~/Library/Application Support/$NAME bleiben."
  exit 0
fi

xcode-select -p >/dev/null 2>&1 || { echo "Erst die Command Line Tools installieren: xcode-select --install"; exit 1; }
command -v cmake >/dev/null || { echo "CMake fehlt: brew install cmake"; exit 1; }

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DTONWERK_TESTS=OFF
cmake --build build-release --config Release --parallel "$(sysctl -n hw.ncpu)"

mkdir -p "$COMPONENTS" "$VST3" "$APPS"
for target in TonwerkSynth:"$NAME" ChromeGlitch:"$GLITCH"; do
  ARTEFACTS="build-release/${target%%:*}_artefacts/Release"
  PRODUCT="${target#*:}"
  # Ohne Entwicklerkonto reicht eine Ad-hoc-Signatur: Apple Silicon lädt keinen unsignierten Code, und Logic prüft
  # Audio Units beim ersten Laden. Für die eigene Maschine genügt das; weitergeben ließe es sich so nicht.
  for bundle in "$ARTEFACTS/AU/$PRODUCT.component" "$ARTEFACTS/VST3/$PRODUCT.vst3" "$ARTEFACTS/Standalone/$PRODUCT.app"; do
    codesign --force --deep --sign - "$bundle"
  done
  rm -rf "$COMPONENTS/$PRODUCT.component" "$VST3/$PRODUCT.vst3" "$APPS/$PRODUCT.app"
  cp -R "$ARTEFACTS/AU/$PRODUCT.component" "$COMPONENTS/"
  cp -R "$ARTEFACTS/VST3/$PRODUCT.vst3" "$VST3/"
  cp -R "$ARTEFACTS/Standalone/$PRODUCT.app" "$APPS/"
done

# macOS merkt sich Audio Units; ohne Neustart des Registrars sieht Logic eine neue Version erst nach dem Abmelden.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
echo
echo "Prüfe die Audio Units mit auval …"
if auval -v aumu Twsy Bvlp >/tmp/tonwerk-auval.log 2>&1 && auval -v aufx ChGl Bvlp >>/tmp/tonwerk-auval.log 2>&1; then
  echo "auval: bestanden."
else
  echo "auval ist fehlgeschlagen, das Protokoll steht in /tmp/tonwerk-auval.log"
  exit 1
fi
echo
echo "Installiert. In Logic: Software-Instrument-Spur, Instrument > AU-Instrumente > b-velop > $NAME."
echo "Den Effekt auf einer Audiospur: Audio-FX > Audio Units > b-velop > $GLITCH."
