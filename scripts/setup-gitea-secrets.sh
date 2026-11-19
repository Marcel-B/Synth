#!/usr/bin/env bash
# Legt die Actions-Secrets im eigenen Gitea an, mit denen die Workflows unter .gitea/workflows signieren, notarisieren,
# zu TestFlight hochladen und nach GitHub veröffentlichen. Es sind dieselben wie bisher bei GitHub, dazu GH_PUSH_TOKEN.
#
#   scripts/setup-gitea-secrets.sh [-l gitea] [-R Marcel/Tonwerk] [NAME ...]
#
# Ohne Namen fragt es alle der Reihe nach ab; eine leere Eingabe überspringt ein Secret (es bleibt, wie es ist).
# Zertifikate (.p12) und der App-Store-Connect-Schlüssel (.p8) werden als Dateipfad angegeben, alles andere verdeckt
# eingetippt. Die Werte gehen über stdin an `tea` und stehen weder in der Shell-History noch in der Prozessliste.
# Braucht die Gitea-CLI `tea` mit einem Login (tea login add).
set -euo pipefail

login=gitea
repo=Marcel/Tonwerk

die() { echo "Fehler: $*" >&2; exit 1; }

while getopts ":l:R:h" opt; do
  case $opt in
    l) login=$OPTARG ;;
    R) repo=$OPTARG ;;
    h) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "Unbekannte Option. Hilfe mit -h." ;;
  esac
done
shift $((OPTIND - 1))

command -v tea >/dev/null || die "tea ist nicht installiert."

# Name | Art (text, p12, p8) | Erklärung
secrets=(
  "GH_PUSH_TOKEN|text|GitHub-Token (fine-grained, nur Marcel-B/Synth; Contents und Workflows: Read and write)"
  "APPLE_TEAM_ID|text|Team-ID des Entwicklerkontos (zehn Zeichen)"
  "APPLE_ID|text|Apple-ID für die Notarisierung (E-Mail-Adresse)"
  "APPLE_APP_PASSWORD|text|App-spezifisches Passwort der Apple-ID (appleid.apple.com)"
  "MACOS_CERTIFICATE_P12|p12|Developer ID Application als .p12 (Dateipfad)"
  "MACOS_CERTIFICATE_PASSWORD|text|Passwort dieser .p12"
  "MACOS_INSTALLER_CERTIFICATE_P12|p12|Developer ID Installer als .p12 (Dateipfad)"
  "MACOS_INSTALLER_CERTIFICATE_PASSWORD|text|Passwort dieser .p12"
  "ASC_KEY_ID|text|Key-ID des App-Store-Connect-Schlüssels (zehn Zeichen, aus AuthKey_<id>.p8)"
  "ASC_ISSUER_ID|text|Issuer-ID (App Store Connect → Integrationen, über der Schlüsselliste)"
  "ASC_KEY_P8|p8|AuthKey_<id>.p8 (Dateipfad)"
)

put() { tea actions secrets create "$1" --stdin --login "$login" --repo "$repo" >/dev/null && echo "  $1 gesetzt"; }

for entry in "${secrets[@]}"; do
  IFS='|' read -r name kind text <<<"$entry"
  if [[ $# -gt 0 ]] && [[ " $* " != *" $name "* ]]; then continue; fi
  echo "$name: $text"
  case $kind in
    text)
      read -rsp "  Wert (leer überspringt): " value; echo
      [[ -n $value ]] || { echo "  übersprungen"; continue; }
      printf '%s' "$value" | put "$name"
      unset value
      ;;
    p12|p8)
      read -rp "  Datei (leer überspringt): " file
      [[ -n $file ]] || { echo "  übersprungen"; continue; }
      file=${file/#\~/$HOME}
      [[ -f $file ]] || die "$file gibt es nicht."
      if [[ $kind == p12 ]]; then
        base64 < "$file" | tr -d '\n' | put "$name"
      else
        grep -q "BEGIN PRIVATE KEY" "$file" || die "$file sieht nicht wie ein .p8-Schlüssel aus."
        put "$name" < "$file"
      fi
      ;;
  esac
done

echo
echo "In $repo liegen jetzt:"
tea actions secrets list --login "$login" --repo "$repo" --output simple
