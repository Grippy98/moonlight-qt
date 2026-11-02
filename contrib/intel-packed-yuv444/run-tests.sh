#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
source_dir=${1:-"$root/libplacebo"}
output_dir=${2:-"$root/build-tests"}
source_dir=$(cd -- "$source_dir" && pwd)
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
read -r -a dependencies <<< "$(pkg-config --cflags --libs libavutil libavcodec libavformat vulkan)"
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function)
if [[ ${SANITIZE:-0} == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
for name in packed-drm-test vulkan-sampling-test vaapi-dmabuf-probe; do
    "${CC:-cc}" "${flags[@]}" \
        -I"$source_dir/build/src/include" -I"$source_dir/src/include" \
        -I"$source_dir/3rdparty/Vulkan-Headers/include" \
        "$root/tests/$name.c" -L"$source_dir/build/src" \
        -Wl,-rpath,"$source_dir/build/src" -lplacebo "${dependencies[@]}" -lm \
        -o "$output_dir/$name"
done
ASAN_OPTIONS=detect_leaks=1 "$output_dir/packed-drm-test"
# External Vulkan driver allocations are outside the patched header's lifetime
# contract; ASan/UBSan remain enabled, but driver leak reporting is excluded.
ASAN_OPTIONS=detect_leaks=0 "$output_dir/vulkan-sampling-test"
ASAN_OPTIONS=detect_leaks=0 "$output_dir/vaapi-dmabuf-probe" --software-reference
printf '\nHardware probe was built but not run: %s/vaapi-dmabuf-probe RENDER_NODE\n' "$output_dir"
