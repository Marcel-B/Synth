#!/bin/zsh
# Installiert Tonwerk Synth, Tonwerk Wavetable, Chrome Glitch und Tonwerk Distortion aus einem Release-Download (liegt im Zip neben den Plugins).
#
#   cd ~/Downloads/Tonwerk-Synth-<version>-macOS && zsh install.sh
#
# Die Plugins sind nur ad hoc signiert (ohne Apple-Entwicklerkonto), deshalb markiert macOS sie nach dem Download als
# Quarantäne, und Logic würde sie nicht laden. Das Skript nimmt die Markierung weg, kopiert und prüft mit auval.
set -euo pipefail

cd "$(dirname "$0")"
NAME="Tonwerk Synth"
GLITCH="Chrome Glitch"
DISTORTION="Tonwerk Distortion"
WAVETABLE="Tonwerk Wavetable"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"

xattr -dr com.apple.quarantine . 2>/dev/null || true
mkdir -p "$COMPONENTS" "$VST3" "$APPS"
for PRODUCT in "$NAME" "$GLITCH" "$DISTORTION" "$WAVETABLE"; do
  rm -rf "$COMPONENTS/$PRODUCT.component" "$VST3/$PRODUCT.vst3" "$APPS/$PRODUCT.app"
  cp -R "$PRODUCT.component" "$COMPONENTS/"
  cp -R "$PRODUCT.vst3" "$VST3/"
  cp -R "$PRODUCT.app" "$APPS/"
done

# macOS merkt sich Audio Units; ohne Neustart des Registrars sieht Logic eine neue Version erst nach dem Abmelden.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
if auval -v aumu Twsy Bvlp >/tmp/tonwerk-auval.log 2>&1 && auval -v aufx ChGl Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwDs Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwWt Bvlp >>/tmp/tonwerk-auval.log 2>&1; then
  echo "Installiert und von auval geprüft."
else
  echo "Installiert, aber auval ist fehlgeschlagen. Das Protokoll steht in /tmp/tonwerk-auval.log"
  exit 1
fi
echo "In Logic: Software-Instrument-Spur, Instrument > AU-Instrumente > b-velop > $NAME oder $WAVETABLE."
echo "Die Effekte auf einer Audiospur: Audio-FX > Audio Units > b-velop > $GLITCH oder $DISTORTION."
