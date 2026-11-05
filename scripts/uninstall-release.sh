#!/bin/bash
# Entfernt alle Tonwerk-Plugins und -Apps von diesem Mac, ob sie aus dem Disk-Image (Paket nach /Library, Apps nach
# /Applications/Tonwerk), aus dem Zip (install.sh nach ~/Library und ~/Applications) oder aus scripts/install-macos.sh
# kommen. Liegt im Zip als uninstall.sh und im Disk-Image als „Tonwerk deinstallieren.command“ (Doppelklick öffnet das
# Terminal).
#
#   zsh uninstall.sh            # fragt vorher nach
#   zsh uninstall.sh --ja       # ohne Rückfrage
#
# Erkannt werden die Bundles an ihrer Kennung (de.bvelop.<plugin>), nicht am Namen, damit auch Plugins späterer
# Versionen gehen. Die App „Tonwerk“ aus dem App Store (de.bvelop.tonwerk und ihre AUv3s) und „Tonwerk-AI“
# (de.bvelop.tonwerk.ai) bleiben, ebenso importierte Klänge und Einstellungen unter ~/Library/Application Support.
# Für /Library fragt sudo nach dem Passwort.
set -euo pipefail

ours() {
  local id
  id="$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$1/Contents/Info.plist" 2>/dev/null || true)"
  case "$id" in
    de.bvelop.tonwerk | de.bvelop.tonwerk.*) return 1 ;;
    de.bvelop.*) return 0 ;;
    *) return 1 ;;
  esac
}

user_found=()
system_found=()
for folder in "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Applications"; do
  for bundle in "$folder"/*.component "$folder"/*.vst3 "$folder"/*.app; do
    if [[ -e "$bundle" ]] && ours "$bundle"; then user_found+=("$bundle"); fi
  done
done
for folder in /Library/Audio/Plug-Ins/Components /Library/Audio/Plug-Ins/VST3 /Applications/Tonwerk /Applications; do
  for bundle in "$folder"/*.component "$folder"/*.vst3 "$folder"/*.app; do
    if [[ -e "$bundle" ]] && ours "$bundle"; then system_found+=("$bundle"); fi
  done
done
receipts="$(pkgutil --pkgs 2>/dev/null | grep '^de\.bvelop\.tonwerk\.plugins\.' || true)"

if [[ ${#user_found[@]} -eq 0 && ${#system_found[@]} -eq 0 && -z "$receipts" ]]; then
  echo "Keine Tonwerk-Plugins oder -Apps gefunden."
  exit 0
fi

echo "Das wird entfernt:"
for bundle in ${user_found[@]+"${user_found[@]}"} ${system_found[@]+"${system_found[@]}"}; do
  echo "  $bundle"
done
if [[ "${1:-}" != "--ja" ]]; then
  printf "Fortfahren? [j/N] "
  read -r answer
  case "$answer" in
    j | J | ja | Ja | y | Y | yes) ;;
    *) echo "Abgebrochen."; exit 1 ;;
  esac
fi

for bundle in ${user_found[@]+"${user_found[@]}"}; do
  rm -rf "$bundle"
done
if [[ ${#system_found[@]} -gt 0 || -n "$receipts" ]]; then
  sudo -v
  for bundle in ${system_found[@]+"${system_found[@]}"}; do
    sudo rm -rf "$bundle"
  done
  # The folder the disk image's apps were dragged into, once it holds nothing else.
  sudo rmdir /Applications/Tonwerk 2>/dev/null || true
  # The installer's receipts, so macOS no longer counts the package as installed.
  for receipt in $receipts; do
    sudo pkgutil --forget "$receipt" >/dev/null || true
  done
fi

# Logic and the other hosts read the Audio Units from this cache; restarting it makes them forget the removed ones.
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Entfernt. Importierte Klänge und Einstellungen liegen weiter unter ~/Library/Application Support."
