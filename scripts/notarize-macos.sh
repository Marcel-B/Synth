#!/bin/bash
# Lässt signierte Bundles von Apple notarisieren und heftet das Ticket an (stapler), damit Gatekeeper sie nach dem
# Download ohne Rückfrage und ohne Netz öffnet.
#
#   scripts/notarize-macos.sh BUNDLE... (auch .dmg und .pkg)
#
# Die Bundles müssen vorher mit Developer ID signiert sein (scripts/sign-macos.sh mit TONWERK_SIGN_IDENTITY).
# Zugang zu Apples Notardienst, eins von beiden:
#   TONWERK_NOTARY_PROFILE   Name eines Profils im Schlüsselbund, einmal angelegt mit
#                            xcrun notarytool store-credentials tonwerk --apple-id … --team-id …
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   Apple-ID, Team-ID und app-spezifisches Passwort (für die CI)
set -euo pipefail

if [[ -n "${TONWERK_NOTARY_PROFILE:-}" ]]; then
  auth=(--keychain-profile "$TONWERK_NOTARY_PROFILE")
elif [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
  auth=(--apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD")
else
  echo "Für die Notarisierung fehlt TONWERK_NOTARY_PROFILE oder APPLE_ID, APPLE_TEAM_ID und APPLE_APP_PASSWORD." >&2
  exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Reicht eine Datei ein und wartet auf Apples Urteil; bricht ab, wenn es nicht „Accepted“ ist.
submit() {
  if ! xcrun notarytool submit "$1" "${auth[@]}" --wait --timeout 1h --output-format json >"$work/result.json"; then
    cat "$work/result.json" >&2 || true
  fi
  status="$(plutil -extract status raw -o - "$work/result.json" 2>/dev/null || echo unbekannt)"
  if [[ "$status" != "Accepted" ]]; then
    id="$(plutil -extract id raw -o - "$work/result.json" 2>/dev/null || true)"
    echo "Notarisierung: $status" >&2
    [[ -n "$id" ]] && xcrun notarytool log "$id" "${auth[@]}" >&2 || true
    exit 1
  fi
}

# Disk-Images und Pakete gehen, wie sie sind, zu Apple; alle Bundles zusammen in einem Zip, also eine Einreichung statt
# einer pro Plugin.
mkdir "$work/bundles"
bundles=0
for item in "$@"; do
  case "$item" in
    *.dmg | *.pkg) submit "$item" ;;
    *)
      ditto "$item" "$work/bundles/$(basename "$item")"
      bundles=$((bundles + 1))
      ;;
  esac
done
if [[ $bundles -gt 0 ]]; then
  ditto -c -k --keepParent "$work/bundles" "$work/notarize.zip"
  submit "$work/notarize.zip"
fi
echo "Notarisierung: angenommen."

# Das Ticket liegt nach der Annahme auch online; angeheftet braucht Gatekeeper kein Netz. Scheitert das Anheften,
# bleiben die Bundles trotzdem notarisiert, deshalb nur eine Warnung.
for bundle in "$@"; do
  xcrun stapler staple "$bundle" || echo "Warnung: Ticket für $bundle nicht angeheftet." >&2
done
