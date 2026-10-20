#!/usr/bin/env python3
"""Extract existing Moonlight decoder-probe packets into a C test fixture."""
import argparse
import hashlib
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('moonlight_source', type=Path)
parser.add_argument('output_header', type=Path)
args = parser.parse_args()
source = args.moonlight_source / 'app/streaming/video/ffmpeg_videosamples.cpp'
raw = source.read_bytes()
text = raw.decode()
expected_hashes = {
    'k_HEVCRExt8_444TestFrame': '5091a16f81d2ebbed055de59566bfd9131b302edde1e284870ba8fa12439a831',
    'k_HEVCRExt10_444TestFrame': '7207ef878009d3987359dd8a38dd0c8563e81b1bd4dbd263f8ff6069ba3ec7a7',
}
output = ['/* Test data from Moonlight v6.2.0 de2467e433821664cdd2224aad8c89a625be1ad9.',
          ' * https://github.com/moonlight-stream/moonlight-qt/blob/v6.2.0/app/streaming/video/ffmpeg_videosamples.cpp',
          ' * Original licensing applies; generated only for the offscreen interoperability test. */',
          '#include <stdint.h>']
for name in ('k_HEVCRExt8_444TestFrame', 'k_HEVCRExt10_444TestFrame'):
    match = re.search(r'const uint8_t FFmpegVideoDecoder::' + name + r'\[\]\s*=\s*\{(.*?)\};', text, re.S)
    if not match:
        parser.error(f'Missing {name}')
    values = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', match.group(1)))
    if hashlib.sha256(values).hexdigest() != expected_hashes[name]:
        parser.error(f'{name}: differs from the pinned v6.2.0 packet; review before regenerating')
    output.append(f'static const uint8_t {name}[] = {{')
    for start in range(0, len(values), 16):
        output.append('    ' + ', '.join(f'0x{v:02x}' for v in values[start:start + 16]) + ',')
    output.append('};')
    print(name, len(values), hashlib.sha256(values).hexdigest())
args.output_header.write_text('\n'.join(output) + '\n')
