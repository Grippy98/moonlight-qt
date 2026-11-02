#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
hdr444=false
check_only=false
build_dir=

usage() {
    cat <<'HELP'
Usage: scripts/build-cachyos-oxp3.sh [--check] [--hdr444] [BUILD_DIRECTORY]

Build the native Linux preview as your normal user. Default: build/oxp3.
--check   Check prerequisites without writing build files.
--hdr444  Opt into the private, pinned libplacebo 7.360.1 header prototype.
MOONLIGHT_BUILD_JOBS overrides the parallel job count (default: up to 8).
HELP
}

for arg in "$@"; do
    case "$arg" in
        -h|--help) usage; exit 0 ;;
        --check) check_only=true ;;
        --hdr444) hdr444=true ;;
        --*) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
        *)
            if [[ -n "$build_dir" ]]; then usage >&2; exit 2; fi
            build_dir=$arg
            ;;
    esac
done

[[ $(uname -s) == Linux ]] || { printf 'This helper builds on Linux.\n' >&2; exit 1; }
[[ $(id -u) != 0 ]] || { printf 'Run this build as your normal user, without sudo.\n' >&2; exit 1; }

missing=()
for tool in qmake6 make pkg-config python3 cc c++; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if $hdr444; then command -v patch >/dev/null || missing+=(patch); fi
if (( ${#missing[@]} )); then
    printf 'Missing build tools: %s\n' "${missing[*]}" >&2
    exit 1
fi

modules=(Qt6Core Qt6Network Qt6Quick Qt6QuickControls2 Qt6Svg openssl sdl2 SDL2_ttf
         opus libavcodec libavutil libswscale libva libva-drm libdrm libplacebo egl)
missing=()
for module in "${modules[@]}"; do
    pkg-config --exists "$module" || missing+=("$module")
done
if (( ${#missing[@]} )); then
    printf 'Missing pkg-config modules: %s\n' "${missing[*]}" >&2
    printf '%s\n' 'CachyOS prerequisites (review before running):' \
        'sudo pacman -S --needed base-devel git qt6-base qt6-declarative qt6-svg qt6-wayland sdl2-compat sdl2_ttf ffmpeg opus openssl libva libdrm libplacebo mesa wayland-protocols' >&2
    exit 1
fi
pkg-config --atleast-version=7.349.0 libplacebo || {
    printf 'Moonlight Vulkan rendering requires libplacebo 7.349.0 or newer.\n' >&2; exit 1;
}
if $hdr444 && [[ $(pkg-config --modversion libplacebo) != 7.360.1 ]]; then
    printf 'The HDR/4:4:4 prototype requires exactly libplacebo 7.360.1. Review/rebase for another version.\n' >&2
    exit 1
fi

for source in moonlight-common-c/moonlight-common-c/src/Limelight-internal.h \
              qmdnsengine/qmdnsengine/src/include/qmdnsengine/server.h \
              app/SDL_GameControllerDB/gamecontrollerdb.txt; do
    [[ -f "$root/$source" ]] || {
        printf 'Missing submodule source: %s\nRun git submodule update --init --recursive first.\n' "$source" >&2
        exit 1
    }
done
if $check_only; then
    printf 'Build prerequisites passed. libplacebo: %s; HDR/4:4:4 prototype: %s\n' \
        "$(pkg-config --modversion libplacebo)" "$hdr444"
    exit 0
fi

build_dir=${build_dir:-"$root/build/oxp3"}
build_dir=$(python3 -c 'import os,sys; print(os.path.abspath(os.path.expanduser(sys.argv[1])))' "$build_dir")
mkdir -p -- "$build_dir"
jobs=${MOONLIGHT_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { printf 'MOONLIGHT_BUILD_JOBS must be positive.\n' >&2; exit 2; }
if [[ -z ${MOONLIGHT_BUILD_JOBS:-} ]] && (( jobs > 8 )); then jobs=8; fi

mode=standard
if $hdr444; then mode=hdr444-libplacebo-7.360.1; fi
previous_mode=$(cat "$build_dir/.oxp3-build-mode" 2>/dev/null || true)
if [[ -f "$build_dir/Makefile" && "$previous_mode" != "$mode" ]]; then
    # Header-search-path changes must not reuse objects from the other mode.
    make -C "$build_dir" clean
fi

unset MOONLIGHT_INTEL_PACKED_YUV444_HEADERS
if $hdr444; then
    private_stage=$(mktemp -d "$build_dir/private-libplacebo.XXXXXX")
    python3 "$root/contrib/intel-packed-yuv444/prepare-private-headers.py" \
        "$(pkg-config --variable=includedir libplacebo)" "$private_stage/include"
    export MOONLIGHT_INTEL_PACKED_YUV444_HEADERS="$private_stage/include"
    # A new -I directory alone may leave qmake's old adapter object reusable.
    rm -f -- "$build_dir/app/release/plvk_c.o" "$build_dir/app/debug/plvk_c.o" \
        "$build_dir/app/plvk_c.o"
fi

printf 'Building %s in %s with %s jobs\n' "$mode" "$build_dir" "$jobs"
(
    cd -- "$build_dir"
    qmake6 "$root/moonlight-qt.pro" 'CONFIG+=release'
    make qmake_all
    make -j "$jobs"
)
printf '%s\n' "$mode" > "$build_dir/.oxp3-build-mode"
printf 'Built preview: %s/app/moonlight\n' "$build_dir"
printf 'Launch with: MOONLIGHT_OXP3_BINARY="%s/app/moonlight" "%s/scripts/run-cachyos-oxp3.sh"\n' "$build_dir" "$root"
