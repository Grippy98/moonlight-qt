#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
object=
standalone=false
usage() {
    cat <<'HELP'
Usage: tests/run-native-display-probe.sh [--object STREAMUTILS_OBJECT | --standalone] [PROBE_ARGUMENTS...]

The default links the actual production StreamUtils object from
$MOONLIGHT_OXP3_BUILD_DIR/app/{release/,}streamutils.o. The default build directory
is build/oxp3. Use --object to select another existing production object.
The production object must include the physical DRM detector.

--standalone explicitly recompiles the detector; it does not validate the
production build flags. This mode is for isolated development only.

Probe arguments: [WIDTH HEIGHT FPS] or [--watch SECONDS].
The GUI backends are offscreen/dummy; physical output detection is read-only.
HELP
}
while (( $# )); do
    case "$1" in
        --object)
            (( $# >= 2 )) || { usage >&2; exit 2; }
            object=$2
            shift 2
            ;;
        --standalone) standalone=true; shift ;;
        --help|-h) usage; exit 0 ;;
        --) shift; break ;;
        *) break ;;
    esac
done
if $standalone && [[ -n "$object" ]]; then
    printf 'Choose either --object or --standalone.\n' >&2
    exit 2
fi
if ! $standalone; then
    if [[ -z "$object" ]]; then
        production_build=${MOONLIGHT_OXP3_BUILD_DIR:-"$root/build/oxp3"}
        for candidate in "$production_build/app/release/streamutils.o" \
                         "$production_build/app/streamutils.o"; do
            if [[ -f "$candidate" ]]; then object=$candidate; break; fi
        done
    fi
    [[ -n "$object" && -f "$object" ]] || {
        printf 'Build Moonlight first, set MOONLIGHT_OXP3_BUILD_DIR, or pass --object PATH.\n' >&2
        printf 'Use --standalone only when intentionally testing an isolated rebuild.\n' >&2
        exit 1
    }
    # A separately forced HAVE_DRM could hide missing production build flags.
    nm -u "$object" | awk '$NF == "drmModeGetResources" { found=1 } END { exit !found }' || {
        printf 'The production StreamUtils object has no physical DRM detector; check qmake HAVE_DRM configuration and rebuild.\n' >&2
        exit 1
    }
    printf 'Linking production StreamUtils object: %s\n' "$object" >&2
    detector=("$object")
else
    printf 'Standalone detector rebuild: production application flags are NOT validated.\n' >&2
    detector=(-DHAVE_DRM "$root/app/streaming/streamutils.cpp")
fi
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT
read -r -a flags <<< "$(pkg-config --cflags --libs Qt6Gui sdl2 libdrm)"
"${CXX:-c++}" -std=c++17 -fPIC -ffunction-sections -fdata-sections \
    -DMOONLIGHT_NATIVE_PROBE_GLOBAL_STUB -I"$root/app" \
    "$root/tests/native-display-probe.cpp" "${detector[@]}" -Wl,--gc-sections \
    "${flags[@]}" -o "$build/native-display-probe"
# Dummy GUI backends make the real DRM override distinguishable from a virtual
# desktop, without opening a window or taking display ownership.
QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy XDG_CURRENT_DESKTOP=gamescope \
    "$build/native-display-probe" "$@"
