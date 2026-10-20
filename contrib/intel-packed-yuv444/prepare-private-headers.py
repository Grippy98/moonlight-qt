#!/usr/bin/env python3
"""Create a private, pinned libplacebo libav header override without system writes."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

PIN = 'cee9b076f2c63104ccfd497fa79c39a867293ec4'  # libplacebo v7.360.1
EXPECTED = {
    'libav.h': 'ae3b41465099e071c0187dca3ddf6f216bf6a5fc497b86ad7d81aad254c50e50',
    'libav_internal.h': '95aa081a1040491b8870c6f901c31dfd3edbaf5da8f2f1638792e80df214142d',
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('include_root', type=Path, help='Unmodified include root containing libplacebo/utils')
    parser.add_argument('output', type=Path, help='New private include directory; must not already exist')
    args = parser.parse_args()
    contents = {}
    for name, expected in EXPECTED.items():
        source = args.include_root / 'libplacebo' / 'utils' / name
        raw = source.read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            parser.error(f'{source}: not the pinned v7.360.1 header; review/rebase before proceeding')
        contents[name] = raw
    args.output.mkdir(parents=True, exist_ok=False)
    try:
        target = args.output / 'libplacebo' / 'utils'
        target.mkdir(parents=True)
        for name, raw in contents.items():
            (target / name).write_bytes(raw)
        patch = Path(__file__).resolve().parent / 'patches/libplacebo-7.360.1-vulkan-packed-yuv444.patch'
        command = ['patch', '--batch', '--forward', '-p3', '-d', str(args.output.resolve()), '-i', str(patch)]
        subprocess.run(command + ['--dry-run'], check=True)
        subprocess.run(command, check=True)
    except BaseException:
        shutil.rmtree(args.output)
        raise
    print(f'Private headers ready: {args.output.resolve()}')
    print(f'Base source: libplacebo v7.360.1 {PIN}')
    print('Add this directory before the system include directory when rebuilding Moonlight.')

if __name__ == '__main__':
    main()
