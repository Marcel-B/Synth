#!/bin/bash
# Macht aus einem Release-Ordner (Audio Units, VST3 und Apps nebeneinander, wie ihn release.yml packt) ein Disk-Image,
# wie man es auf dem Mac erwartet: die Apps in einem Ordner „Tonwerk“ neben einem Verweis auf Programme zum Hineinziehen,
# und für die Plugins, die nach /Library/Audio/Plug-Ins gehören und sich nicht ziehen lassen, ein Installationspaket.
#
#   scripts/make-macos-installer.sh ORDNER VERSION ZIEL.dmg
#
# TONWERK_INSTALLER_IDENTITY ("Developer ID Installer: …") signiert das Paket, TONWERK_SIGN_IDENTITY ("Developer ID
# Application: …") das Disk-Image; ohne sie bleiben beide unsigniert. Notarisiert wird danach mit
# scripts/notarize-macos.sh ZIEL.dmg. Die Bundles im Ordner müssen schon signiert (und notarisiert) sein.
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Aufruf: $0 ORDNER VERSION ZIEL.dmg" >&2
  exit 2
fi
src="$1"
version="$2"
dmg="$3"
here="$(cd "$(dirname "$0")" && pwd)"
installer_identity="${TONWERK_INSTALLER_IDENTITY:-}"
image_identity="${TONWERK_SIGN_IDENTITY:-}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

shopt -s nullglob
components=("$src"/*.component)
vst3s=("$src"/*.vst3)
apps=("$src"/*.app)
shopt -u nullglob
if [[ ${#components[@]} -eq 0 || ${#vst3s[@]} -eq 0 || ${#apps[@]} -eq 0 ]]; then
  echo "In $src fehlen Audio Units, VST3 oder Apps." >&2
  exit 1
fi

# One package per format, so the installer's "Anpassen" can leave one out (a Logic user needs no VST3).
# $1 kind, $2 folder under /Library/Audio/Plug-Ins, $3 extension, then the bundles.
make_package() {
  local kind="$1" folder="$2" extension="$3"
  shift 3
  local root="$work/root-$kind" scripts="$work/scripts-$kind" plist="$work/$kind.plist"
  local target="$root/Library/Audio/Plug-Ins/$folder"
  mkdir -p "$target" "$scripts"
  local names=()
  for bundle in "$@"; do
    ditto "$bundle" "$target/$(basename "$bundle")"
    names+=("$(basename "$bundle" ".$extension")")
  done
  local list=""
  for name in "${names[@]}"; do
    list+="$(printf '%q' "$name") "
  done

  # The installer writes into an existing bundle file by file; a file a new version no longer has would stay behind.
  cat >"$scripts/preinstall" <<EOF
#!/bin/bash
for name in $list; do
  rm -rf "/Library/Audio/Plug-Ins/$folder/\$name.$extension"
done
exit 0
EOF
  # install.sh and install-macos.sh put the plugins into the user's own Library. A second copy there with the same
  # codes would make Logic pick one of them (or call both unavailable), so the installed ones replace those.
  cat >"$scripts/postinstall" <<EOF
#!/bin/bash
user="\$(stat -f %Su /dev/console)"
if [[ -n "\$user" && "\$user" != root && "\$user" != loginwindow ]]; then
  home="\$(dscl . -read "/Users/\$user" NFSHomeDirectory 2>/dev/null | awk '{print \$2}')"
  if [[ -n "\$home" && -d "\$home/Library/Audio/Plug-Ins/$folder" ]]; then
    for name in $list; do
      rm -rf "\$home/Library/Audio/Plug-Ins/$folder/\$name.$extension"
    done
  fi
fi
# macOS caches Audio Units; without this Logic sees a new version only after logging out.
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
  chmod 755 "$scripts/preinstall" "$scripts/postinstall"

  # By default pkgbuild makes bundles relocatable: the installer would update a copy it finds elsewhere on the disk
  # (say an old plugin in a backup folder) instead of writing to /Library.
  pkgbuild --analyze --root "$root" "$plist" >/dev/null
  local i=0
  while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null; do
    i=$((i + 1))
  done
  pkgbuild --root "$root" --component-plist "$plist" --scripts "$scripts" --install-location / \
    --identifier "de.bvelop.tonwerk.plugins.$kind" --version "$version" "$work/packages/$kind.pkg" >/dev/null
}

mkdir -p "$work/packages"
make_package au Components component "${components[@]}"
make_package vst3 VST3 vst3 "${vst3s[@]}"

# The installer's texts in German and English; Installer picks the system's language.
resources="$work/resources"
mkdir -p "$resources/de.lproj" "$resources/en.lproj"
for lproj in de en; do
  cp "$src/LICENSE" "$resources/$lproj.lproj/LICENSE.txt" 2>/dev/null || cp "$here/../LICENSE" "$resources/$lproj.lproj/LICENSE.txt"
done
cat >"$resources/de.lproj/welcome.txt" <<EOF
Tonwerk Synth $version

Dieses Paket installiert die Plugins für alle Benutzer dieses Macs:

• Audio Units (Logic Pro, GarageBand, MainStage) nach /Library/Audio/Plug-Ins/Components
• VST3 (Ableton Live, Bitwig, Reaper, …) nach /Library/Audio/Plug-Ins/VST3

Unter „Anpassen“ lässt sich eines davon abwählen. Logic sollte während der Installation geschlossen sein. Ältere Kopien, die das Skript install.sh in deinen eigenen Library-Ordner gelegt hat, werden dabei ersetzt.

Die Apps liegen im Disk-Image im Ordner „Tonwerk“: einfach auf „Applications“ ziehen.
EOF
cat >"$resources/en.lproj/welcome.txt" <<EOF
Tonwerk Synth $version

This package installs the plugins for every user of this Mac:

• Audio Units (Logic Pro, GarageBand, MainStage) into /Library/Audio/Plug-Ins/Components
• VST3 (Ableton Live, Bitwig, Reaper, …) into /Library/Audio/Plug-Ins/VST3

“Customize” lets you leave one of them out. Please quit Logic while installing. Older copies that the install.sh script put into your own Library folder are replaced.

The apps are in the disk image's “Tonwerk” folder: just drag it onto “Applications”.
EOF
cat >"$resources/de.lproj/Localizable.strings" <<'EOF'
"AU_TITLE" = "Audio Units";
"AU_TEXT" = "Für Logic Pro, GarageBand und MainStage.";
"VST3_TITLE" = "VST3";
"VST3_TEXT" = "Für Ableton Live, Bitwig, Reaper und andere VST3-Hosts.";
EOF
cat >"$resources/en.lproj/Localizable.strings" <<'EOF'
"AU_TITLE" = "Audio Units";
"AU_TEXT" = "For Logic Pro, GarageBand and MainStage.";
"VST3_TITLE" = "VST3";
"VST3_TEXT" = "For Ableton Live, Bitwig, Reaper and other VST3 hosts.";
EOF

cat >"$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Tonwerk Synth $version</title>
  <welcome file="welcome.txt" mime-type="text/plain"/>
  <license file="LICENSE.txt" mime-type="text/plain"/>
  <options customize="allow" require-scripts="false" rootVolumeOnly="true" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true" enable_anywhere="false" enable_currentUserHome="false"/>
  <allowed-os-versions>
    <os-version min="11.0"/>
  </allowed-os-versions>
  <choices-outline>
    <line choice="au"/>
    <line choice="vst3"/>
  </choices-outline>
  <choice id="au" title="AU_TITLE" description="AU_TEXT">
    <pkg-ref id="de.bvelop.tonwerk.plugins.au"/>
  </choice>
  <choice id="vst3" title="VST3_TITLE" description="VST3_TEXT">
    <pkg-ref id="de.bvelop.tonwerk.plugins.vst3"/>
  </choice>
  <pkg-ref id="de.bvelop.tonwerk.plugins.au" version="$version" onConclusion="none">au.pkg</pkg-ref>
  <pkg-ref id="de.bvelop.tonwerk.plugins.vst3" version="$version" onConclusion="none">vst3.pkg</pkg-ref>
</installer-gui-script>
EOF

# What goes into the disk image.
stage="$work/image"
mkdir -p "$stage/Tonwerk"
for app in "${apps[@]}"; do
  ditto "$app" "$stage/Tonwerk/$(basename "$app")"
done
ln -s /Applications "$stage/Applications"
package="$stage/Tonwerk Plugins.pkg"
sign=()
if [[ -n "$installer_identity" ]]; then sign=(--sign "$installer_identity" --timestamp); fi
productbuild --distribution "$work/distribution.xml" --package-path "$work/packages" --resources "$resources" \
  ${sign[@]+"${sign[@]}"} "$package"
for file in LICENSE NOTICE; do
  if [[ -f "$src/$file" ]]; then cp "$src/$file" "$stage/$file.txt"; fi
done
# Double-clicked, a .command opens in Terminal.
cp "$here/uninstall-release.sh" "$stage/Tonwerk deinstallieren.command"
chmod 755 "$stage/Tonwerk deinstallieren.command"
cat >"$stage/Lies mich - Read me.txt" <<EOF
Tonwerk Synth $version

1. Den Ordner „Tonwerk“ auf „Applications“ ziehen: die Apps (jedes Plugin auch allein spielbar).
2. „Tonwerk Plugins.pkg“ doppelklicken: Audio Units und VST3 für Logic und andere Hosts.

1. Drag the “Tonwerk” folder onto “Applications”: the apps (every plugin playable on its own).
2. Double-click “Tonwerk Plugins.pkg”: Audio Units and VST3 for Logic and other hosts.

„Tonwerk deinstallieren.command“ (Doppelklick) entfernt alle Tonwerk-Plugins und -Apps wieder.
“Tonwerk deinstallieren.command” (double-click) removes every Tonwerk plugin and app again.

https://tonwerk.studio
EOF

rm -f "$dmg"
# ULMO (LZMA) needs macOS 10.15, below the plugins' macOS 11; HFS+ so older systems mount it as well.
hdiutil create -volname "Tonwerk Synth $version" -srcfolder "$stage" -fs HFS+ -format ULMO -ov "$dmg" >/dev/null
if [[ -n "$image_identity" ]]; then
  codesign --force --timestamp --sign "$image_identity" "$dmg"
fi
echo "Disk-Image: $dmg"
