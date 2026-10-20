#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if command -v qmake6 >/dev/null 2>&1; then
    qmake_cmd=qmake6
elif command -v qmake >/dev/null 2>&1; then
    qmake_cmd=qmake
else
    printf 'Qt qmake and the Qt Core/Network development libraries are required.\n' >&2
    exit 1
fi
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/moonlight-bitrate-test.XXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT
cd -- "$build_dir"
"$qmake_cmd" "$source_dir/bitrate-policy.pro"
make -j2
./bitrate-policy-test
