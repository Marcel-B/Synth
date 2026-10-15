#!/bin/bash
# Signiert Plugin-Bundles (.component, .vst3) und Apps.
#
#   scripts/sign-macos.sh BUNDLE...
#
# Mit TONWERK_SIGN_IDENTITY (z. B. "Developer ID Application: Vorname Nachname (TEAMID)", siehe
# `security find-identity -v -p codesigning`) signiert es mit diesem Zertifikat, mit Hardened Runtime und Zeitstempel,
# wie es die Notarisierung verlangt. Ohne die Variable signiert es ad hoc: Apple Silicon lädt keinen unsignierten Code,
# für die eigene Maschine reicht das.
set -euo pipefail

ENTITLEMENTS="$(cd "$(dirname "$0")" && pwd)/app.entitlements"
IDENTITY="${TONWERK_SIGN_IDENTITY:-}"

for bundle in "$@"; do
  if [[ -z "$IDENTITY" ]]; then
    codesign --force --deep --sign - "$bundle"
    continue
  fi
  # Die Hardened Runtime sperrt das Mikrofon, solange die App es nicht in ihren Entitlements nennt; die Standalone-Apps
  # von Vocoder und Robovox brauchen den Eingang. Plugins laufen im Prozess des Hosts und erben dessen Rechte.
  extra=()
  [[ "$bundle" == *.app ]] && extra=(--entitlements "$ENTITLEMENTS")
  codesign --force --options runtime --timestamp ${extra[@]+"${extra[@]}"} --sign "$IDENTITY" "$bundle"
  codesign --verify --deep --strict "$bundle"
done
