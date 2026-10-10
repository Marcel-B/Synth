#!/bin/zsh
# Baut Tonwerk Analog, Tonwerk FM, Tonwerk DX, Tonwerk Wavetable, das bisherige Tonwerk Synth und die Effekte Chrome Glitch und Tonwerk Distortion auf dem Mac und installiert sie für Logic
# (Audio Unit) und andere Hosts (VST3).
# Braucht nur die Command Line Tools (xcode-select --install) und CMake (brew install cmake), kein volles Xcode.
#
#   scripts/install-macos.sh            bauen, signieren, installieren, mit auval prüfen
#   scripts/install-macos.sh --uninstall
set -euo pipefail

cd "$(dirname "$0")/.."
NAME="Tonwerk Synth"
ANALOG="Tonwerk Analog"
FM="Tonwerk FM"
GLITCH="Chrome Glitch"
DISTORTION="Tonwerk Distortion"
WAVETABLE="Tonwerk Wavetable"
DX="Tonwerk DX"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"

if [[ "${1:-}" == "--uninstall" ]]; then
  rm -rf "$COMPONENTS/$ANALOG.component" "$VST3/$ANALOG.vst3" "$APPS/$ANALOG.app"
  rm -rf "$COMPONENTS/$FM.component" "$VST3/$FM.vst3" "$APPS/$FM.app"
  rm -rf "$COMPONENTS/$NAME.component" "$VST3/$NAME.vst3" "$APPS/$NAME.app"
  rm -rf "$COMPONENTS/$GLITCH.component" "$VST3/$GLITCH.vst3" "$APPS/$GLITCH.app"
  rm -rf "$COMPONENTS/$DISTORTION.component" "$VST3/$DISTORTION.vst3" "$APPS/$DISTORTION.app"
  rm -rf "$COMPONENTS/$WAVETABLE.component" "$VST3/$WAVETABLE.vst3" "$APPS/$WAVETABLE.app"
  rm -rf "$COMPONENTS/$DX.component" "$VST3/$DX.vst3" "$APPS/$DX.app"
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "Entfernt. Die Klänge in ~/Library/Application Support/$NAME bleiben."
  exit 0
fi

xcode-select -p >/dev/null 2>&1 || { echo "Erst die Command Line Tools installieren: xcode-select --install"; exit 1; }
command -v cmake >/dev/null || { echo "CMake fehlt: brew install cmake"; exit 1; }

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DTONWERK_TESTS=OFF
cmake --build build-release --config Release --parallel "$(sysctl -n hw.ncpu)"

mkdir -p "$COMPONENTS" "$VST3" "$APPS"
for target in TonwerkAnalog:"$ANALOG" TonwerkFM:"$FM" TonwerkSynth:"$NAME" ChromeGlitch:"$GLITCH" TonwerkDistortion:"$DISTORTION" TonwerkWavetable:"$WAVETABLE" TonwerkDX:"$DX"; do
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
if auval -v aumu TwAn Bvlp >/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwFm Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu Twsy Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aufx ChGl Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwDs Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwWt Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwDx Bvlp >>/tmp/tonwerk-auval.log 2>&1; then
  echo "auval: bestanden."
else
  echo "auval ist fehlgeschlagen, das Protokoll steht in /tmp/tonwerk-auval.log"
  exit 1
fi
echo
echo "Installiert. In Logic: Software-Instrument-Spur, Instrument > AU-Instrumente > b-velop > $ANALOG, $FM, $DX oder $WAVETABLE."
echo "$NAME (beide Engines in einem) bleibt für ältere Projekte installiert."
echo "Die Effekte auf einer Audiospur: Audio-FX > Audio Units > b-velop > $GLITCH oder $DISTORTION."
