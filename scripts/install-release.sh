#!/bin/zsh
# Installiert Tonwerk Analog, Tonwerk FM, Tonwerk DX, Tonwerk Wavetable, Tonwerk Granular, Tonwerk Physical, Tonwerk Additiv, Tonwerk PD, Tonwerk Sampler, Tonwerk Bass, Tonwerk Groovebox, Tonwerk Synth, Chrome Glitch, Tonwerk Distortion, Tonwerk Robovox, Tonwerk Vocoder, Tonwerk Wobble, Tonwerk Tape, Tonwerk Delay, Tonwerk Chorus, Tonwerk Glue, Tonwerk Shimmer, Tonwerk Filter, Tonwerk Freeze und Tonwerk Arp aus einem Release-Download (liegt im Zip neben den Plugins).
#
#   cd ~/Downloads/Tonwerk-Synth-<version>-macOS && zsh install.sh
#
# Ist das Release nur ad hoc signiert (ohne Developer-ID-Secrets gebaut), markiert macOS es nach dem Download als
# Quarantäne, und Logic würde die Plugins nicht laden. Das Skript nimmt die Markierung weg (bei einem notarisierten
# Release schadet das nicht), kopiert und prüft mit auval.
set -euo pipefail

cd "$(dirname "$0")"
NAME="Tonwerk Synth"
ANALOG="Tonwerk Analog"
FM="Tonwerk FM"
GLITCH="Chrome Glitch"
DISTORTION="Tonwerk Distortion"
WAVETABLE="Tonwerk Wavetable"
DX="Tonwerk DX"
GRANULAR="Tonwerk Granular"
PHYSICAL="Tonwerk Physical"
ADDITIVE="Tonwerk Additiv"
GROOVEBOX="Tonwerk Groovebox"
ROBOVOX="Tonwerk Robovox"
WOBBLE="Tonwerk Wobble"
TAPE="Tonwerk Tape"
PD="Tonwerk PD"
FREEZE="Tonwerk Freeze"
ARP="Tonwerk Arp"
SAMPLER="Tonwerk Sampler"
BASS="Tonwerk Bass"
VOCODER="Tonwerk Vocoder"
DELAY="Tonwerk Delay"
CHORUS="Tonwerk Chorus"
GLUE="Tonwerk Glue"
SHIMMER="Tonwerk Shimmer"
FILTER="Tonwerk Filter"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
APPS="$HOME/Applications"

xattr -dr com.apple.quarantine . 2>/dev/null || true
mkdir -p "$COMPONENTS" "$VST3" "$APPS"
for PRODUCT in "$ANALOG" "$FM" "$NAME" "$GLITCH" "$DISTORTION" "$WAVETABLE" "$DX" "$GRANULAR" "$PHYSICAL" "$ADDITIVE" "$GROOVEBOX" "$ROBOVOX" "$WOBBLE" "$PD" "$TAPE" "$FREEZE" "$ARP" "$SAMPLER" "$BASS" "$VOCODER" "$DELAY" "$CHORUS" "$GLUE" "$SHIMMER" "$FILTER"; do
  rm -rf "$COMPONENTS/$PRODUCT.component" "$VST3/$PRODUCT.vst3" "$APPS/$PRODUCT.app"
  cp -R "$PRODUCT.component" "$COMPONENTS/"
  cp -R "$PRODUCT.vst3" "$VST3/"
  cp -R "$PRODUCT.app" "$APPS/"
done

# macOS merkt sich Audio Units; ohne Neustart des Registrars sieht Logic eine neue Version erst nach dem Abmelden.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
if auval -v aumu TwAn Bvlp >/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwFm Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu Twsy Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aufx ChGl Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwDs Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwWt Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwDx Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwGr Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwPh Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aumu TwAd Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwGb Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumf TwRv Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aufx TwWb Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwPd Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwTp Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwFz Bvlp >>/tmp/tonwerk-auval.log 2>&1 && auval -v aumi TwAr Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwSm Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumu TwBs Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aumf TwVc Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwDl Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwCh Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwGl Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwSh Bvlp >>/tmp/tonwerk-auval.log 2>&1 \
  && auval -v aufx TwFl Bvlp >>/tmp/tonwerk-auval.log 2>&1; then
  echo "Installiert und von auval geprüft."
else
  echo "Installiert, aber auval ist fehlgeschlagen. Das Protokoll steht in /tmp/tonwerk-auval.log"
  exit 1
fi
echo "In Logic: Software-Instrument-Spur, Instrument > AU-Instrumente > b-velop > $ANALOG, $FM, $DX, $WAVETABLE, $GRANULAR, $PHYSICAL, $ADDITIVE, $PD, $SAMPLER, $BASS oder $GROOVEBOX."
echo "$NAME (beide Engines in einem) bleibt für ältere Projekte installiert."
echo "Die Effekte auf einer Audiospur: Audio-FX > Audio Units > b-velop > $GLITCH, $DISTORTION, $TAPE, $DELAY, $CHORUS, $GLUE, $SHIMMER oder $FILTER."
echo "$WOBBLE auf jeder Spur, am liebsten dem Bass: Audio-FX > Audio Units > b-velop > $WOBBLE."
echo "$FREEZE auf jeder Spur, für endlose Flächen: Audio-FX > Audio Units > b-velop > $FREEZE."
echo "$ARP vor einem Synth: Software-Instrument-Spur, MIDI-FX > Audio Units > b-velop > $ARP."
echo "$ROBOVOX auf der Gesangsspur: Audio-FX > Audio Units > b-velop > $ROBOVOX (für Noten: MIDI-Side-Chain oben im Plugin-Fenster)."
echo "$VOCODER: Software-Instrument-Spur, Instrument > AU MIDI-gesteuerte Effekte > b-velop > $VOCODER, die Gesangsspur als Side-Chain; die Noten der Spur spielen den Träger."
