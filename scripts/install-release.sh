#!/bin/zsh
# Installiert Tonwerk Synth aus einem Release-Download (liegt im Zip neben den Plugins).
#
#   cd ~/Downloads/Tonwerk-Synth-<version>-macOS && zsh install.sh
#
# Die Plugins sind nur ad hoc signiert (ohne Apple-Entwicklerkonto), deshalb markiert macOS sie nach dem Download als
# Quarantäne, und Logic würde sie nicht laden. Das Skript nimmt die Markierung weg, kopiert und prüft mit auval.
set -euo pipefail

cd "$(dirname "$0")"
NAME="Tonwerk Synth"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"

xattr -dr com.apple.quarantine . 2>/dev/null || true
mkdir -p "$COMPONENTS" "$VST3" "$APPS"
rm -rf "$COMPONENTS/$NAME.component" "$VST3/$NAME.vst3" "$APPS/$NAME.app"
cp -R "$NAME.component" "$COMPONENTS/"
cp -R "$NAME.vst3" "$VST3/"
cp -R "$NAME.app" "$APPS/"

# macOS merkt sich Audio Units; ohne Neustart des Registrars sieht Logic eine neue Version erst nach dem Abmelden.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
if auval -v aumu Twsy Bvlp >/tmp/tonwerk-auval.log 2>&1; then
  echo "Installiert und von auval geprüft."
else
  echo "Installiert, aber auval ist fehlgeschlagen. Das Protokoll steht in /tmp/tonwerk-auval.log"
  exit 1
fi
echo "In Logic: Software-Instrument-Spur, Instrument > AU-Instrumente > b-velop > $NAME."
