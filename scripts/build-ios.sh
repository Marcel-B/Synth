#!/bin/bash
# Baut alle Plugins für das iPad: je eine App mit dem AUv3 darin (Logic Pro für iPad, GarageBand, AUM laden das AUv3,
# die App spielt allein). Braucht das volle Xcode (nur dort gibt es das iOS-SDK) und CMake; der Mac-Build bleibt davon
# unberührt und braucht weiter nur die Command Line Tools.
#
#   scripts/build-ios.sh simulator   für den iOS-Simulator, ohne Signatur
#   TONWERK_TEAM_ID=ABCDE12345 scripts/build-ios.sh device   für ein iPad, signiert mit dem Team des Developer-Kontos
#                                     (Xcode legt die Profile selbst an, wenn das Konto in Xcode angemeldet ist)
#
# TONWERK_IOS_BUILD_DIR wählt den Build-Ordner (build-ios), weitere CMake-Argumente stehen in TONWERK_IOS_CMAKE_ARGS.
# Die Apps liegen danach unter <Ordner>/<Target>_artefacts/Release/Standalone/.
set -euo pipefail

cd "$(dirname "$0")/.."
kind="${1:-simulator}"
dir="${TONWERK_IOS_BUILD_DIR:-build-ios}"
read -r -a extra <<< "${TONWERK_IOS_CMAKE_ARGS:-}"

case "$kind" in
    simulator)
        sdk=iphonesimulator
        # Der Simulator nimmt eine Ad-hoc-Signatur ("Sign to Run Locally"), ein Team braucht er nicht.
        signing=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=-)
        ;;
    device)
        sdk=iphoneos
        : "${TONWERK_TEAM_ID:?TONWERK_TEAM_ID fehlt (Team-ID des Apple-Developer-Kontos, 10 Zeichen)}"
        signing=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="Apple Development"
                 -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$TONWERK_TEAM_ID"
                 -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic)
        ;;
    *)
        echo "Aufruf: $0 simulator|device" >&2
        exit 2
        ;;
esac

cmake -S . -B "$dir" -G Xcode -DCMAKE_SYSTEM_NAME=iOS "${signing[@]}" ${extra[@]+"${extra[@]}"}
xcode_args=(-sdk "$sdk")
if [[ "$kind" == device ]]; then
    xcode_args+=(-allowProvisioningUpdates)
fi
cmake --build "$dir" --config Release --parallel "$(sysctl -n hw.ncpu)" -- "${xcode_args[@]}"

find "$dir" -maxdepth 4 -path "*_artefacts/Release/Standalone/*.app" -print
