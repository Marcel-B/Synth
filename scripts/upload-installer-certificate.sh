#!/bin/bash
# Legt das Zertifikat „Developer ID Installer“ als Secrets im GitHub-Repository an, damit release.yml das
# Installationspaket im Disk-Image signiert.
#
#   scripts/upload-installer-certificate.sh Installer.p12
#
# Die .p12 exportierst du vorher in der Schlüsselbundverwaltung (Meine Zertifikate → „Developer ID Installer: …“ →
# Rechtsklick → Exportieren, mit Passwort). Braucht die GitHub CLI, angemeldet mit Zugriff aufs Repository
# (brew install gh && gh auth login). Das Skript fragt nach dem Passwort der .p12, prüft, dass wirklich ein
# Developer-ID-Installer-Schlüssel darin steckt, und setzt MACOS_INSTALLER_CERTIFICATE_P12 und
# MACOS_INSTALLER_CERTIFICATE_PASSWORD. Nichts davon landet auf der Platte oder im Terminal-Verlauf.
set -euo pipefail

REPO="${TONWERK_REPO:-Marcel-B/Synth}"
p12="${1:-}"
if [[ -z "$p12" || ! -f "$p12" ]]; then
  echo "Aufruf: $0 Installer.p12" >&2
  exit 2
fi
if ! command -v gh >/dev/null; then
  echo "Die GitHub CLI fehlt: brew install gh && gh auth login" >&2
  exit 1
fi
gh auth status >/dev/null 2>&1 || { echo "Erst bei GitHub anmelden: gh auth login" >&2; exit 1; }

printf "Passwort der .p12: "
read -r -s password
echo

# Import into a throwaway keychain, as the release does, to see what the file really holds.
keychain="$(mktemp -d)/check.keychain-db"
trap 'security delete-keychain "$keychain" 2>/dev/null || true; rm -rf "$(dirname "$keychain")"' EXIT
kc_password="$(uuidgen)"
security create-keychain -p "$kc_password" "$keychain"
security unlock-keychain -p "$kc_password" "$keychain"
if ! security import "$p12" -P "$password" -A -t cert -f pkcs12 -k "$keychain" >/dev/null; then
  echo "Die .p12 ließ sich nicht öffnen; stimmt das Passwort?" >&2
  exit 1
fi
identity="$(security find-identity -v "$keychain" | grep -o '"Developer ID Installer: [^"]*"' | head -1 | tr -d '"' || true)"
if [[ -z "$identity" ]]; then
  echo "In der .p12 steckt keine Identität „Developer ID Installer“ (mit privatem Schlüssel)." >&2
  security find-identity -v "$keychain" >&2 || true
  exit 1
fi
echo "Gefunden: $identity"

base64 -i "$p12" | gh secret set MACOS_INSTALLER_CERTIFICATE_P12 --repo "$REPO"
printf '%s' "$password" | gh secret set MACOS_INSTALLER_CERTIFICATE_PASSWORD --repo "$REPO"
echo "Secrets in $REPO gesetzt. Das nächste Release signiert das Installationspaket damit."
