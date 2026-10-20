#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} == --help ]]; then
    cat <<'HELP'
Usage: scripts/run-cachyos-oxp3.sh [MOONLIGHT_ARGUMENTS...]

Launch a private native preview without modifying the installed Flatpak.
MOONLIGHT_OXP3_BINARY: binary path (default: build/oxp3/app/moonlight).
MOONLIGHT_OXP3_STATE_DIR: private profile root (default:
  $XDG_DATA_HOME/moonlight-oxp3-preview, or ~/.local/share/moonlight-oxp3-preview).
MOONLIGHT_OXP3_CONFIG_SOURCE: optional first-run Moonlight.conf to import.
The default import source is the existing Moonlight Flatpak profile.
HELP
    exit 0
fi
[[ $(uname -s) == Linux ]] || { printf 'This preview launcher requires Linux.\n' >&2; exit 1; }
[[ $(id -u) != 0 ]] || { printf 'Run the preview as your normal user, without sudo.\n' >&2; exit 1; }
command -v python3 >/dev/null || { printf 'python3 is required.\n' >&2; exit 1; }
binary=${MOONLIGHT_OXP3_BINARY:-"$root/build/oxp3/app/moonlight"}
binary=$(python3 -c 'import os,sys; print(os.path.abspath(os.path.expanduser(sys.argv[1])))' "$binary")
[[ -f "$binary" && -x "$binary" ]] || { printf 'Preview binary is not executable: %s\n' "$binary" >&2; exit 1; }
state=${MOONLIGHT_OXP3_STATE_DIR:-"${XDG_DATA_HOME:-$HOME/.local/share}/moonlight-oxp3-preview"}
state=$(python3 -c 'import os,sys; print(os.path.abspath(os.path.expanduser(sys.argv[1])))' "$state")
umask 077

python3 - "$state" "${MOONLIGHT_OXP3_CONFIG_SOURCE:-}" <<'PY'
from pathlib import Path
import os
import sys

state = Path(sys.argv[1])
flatpak = Path.home() / '.var/app/com.moonlight_stream.Moonlight'
if state.resolve() == Path.home().resolve() or state.resolve() == Path('/'):
    raise SystemExit('Choose a dedicated preview state directory.')
if state.resolve() == flatpak.resolve() or flatpak.resolve() in state.resolve().parents:
    raise SystemExit('The preview state directory must be outside the installed Flatpak profile.')

def private_directory(path):
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    if path.is_symlink() or not path.is_dir() or path.stat().st_uid != os.getuid():
        raise SystemExit(f'Refusing an unowned or linked preview directory: {path}')
    path.chmod(0o700)

private_directory(state)
for name in ('config', 'cache', 'data', 'tmp'):
    private_directory(state / name)
organization = state / 'config/Moonlight Game Streaming Project'
private_directory(organization)
target = organization / 'Moonlight.conf'
if target.exists() or target.is_symlink():
    if target.is_symlink() or not target.is_file() or target.stat().st_uid != os.getuid():
        raise SystemExit('Refusing an unowned or linked preview configuration.')
    target.chmod(0o600)
    raise SystemExit(0)

if sys.argv[2]:
    source = Path(sys.argv[2]).expanduser().absolute()
    if not source.is_file():
        raise SystemExit('MOONLIGHT_OXP3_CONFIG_SOURCE must name an existing configuration file.')
else:
    source = next((path for path in (
        flatpak / 'config/Moonlight Game Streaming Project/Moonlight.conf',
        flatpak / 'config/MoonlightGameStreamingProject/Moonlight.conf',
    ) if path.is_file()), None)
raw = source.read_bytes() if source is not None else b''
newline = b'\r\n' if b'\r\n' in raw else b'\n'
flags = (b'nativeresolution', b'nativefps')
written = set()
output = []
general = False
found_general = False

def append_missing():
    if output and not output[-1].endswith((b'\n', b'\r')):
        output[-1] += newline
    for key in flags:
        if key not in written:
            output.append(key + b'=true' + newline)
            written.add(key)

for line in raw.splitlines(keepends=True):
    stripped = line.strip()
    if stripped.startswith(b'[') and stripped.endswith(b']'):
        if general:
            append_missing()
        general = stripped == b'[General]'
        found_general |= general
    if general and b'=' in line and line.split(b'=', 1)[0].strip() in flags:
        key = line.split(b'=', 1)[0].strip()
        output.append(key + b'=true' + newline)
        written.add(key)
    else:
        output.append(line)
if general:
    append_missing()
if not found_general:
    output = [b'[General]' + newline,
              *(key + b'=true' + newline for key in flags), newline, *output]

fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
with os.fdopen(fd, 'wb') as file:
    file.write(b''.join(output))
print('Created private preview profile; Native resolution and FPS enabled for the first launch.')
if source is None:
    print('No Flatpak profile found; the preview will create its own host-pairing identity.')
else:
    print('Imported the existing configuration and pairing identity; the source profile is unchanged.')
PY

export XDG_CONFIG_HOME="$state/config"
export XDG_CACHE_HOME="$state/cache"
export XDG_DATA_HOME="$state/data"
export TMPDIR="$state/tmp"
cd -- "$state"
exec "$binary" "$@"
