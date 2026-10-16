#!/bin/sh
# Installiert die Tonwerk-Plugins aus einem Linux-Release (liegt im Archiv neben den Plugins).
#
#   tar -xzf Tonwerk-Synth-<version>-Linux-x64.tar.gz && cd Tonwerk-Synth-<version>-Linux-x64 && ./install.sh
#
# VST3 nach ~/.vst3 (dort sucht jeder VST3-Host, Bitwig, Reaper, Ardour), die Apps nach ~/.local/bin.
set -eu

cd "$(dirname "$0")"
mkdir -p "$HOME/.vst3" "$HOME/.local/bin"
for plugin in VST3/*.vst3; do
  rm -rf "$HOME/.vst3/$(basename "$plugin")"
  cp -R "$plugin" "$HOME/.vst3/"
done
cp Apps/* "$HOME/.local/bin/"
echo "Installiert: VST3 in ~/.vst3, Apps in ~/.local/bin."
