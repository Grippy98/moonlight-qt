# Intel VAAPI HDR / 4:4:4 import prototype

This candidate patches libplacebo's header-only FFmpeg adapter so that Intel's packed 4:4:4 decoded surfaces can be imported into its Vulkan renderer. It keeps VAAPI decoding and GPU rendering; it does not add a software decoding fallback or download streaming frames to CPU memory.

**Status:** the offscreen hardware probe passed on the OXP3 Intel Arc B390 with both 8-bit and 10-bit HEVC 4:4:4. Actual HDR presentation through the compositor and TV remains to be verified; this is an experimental build option.

## Contents

- [Patch](patches/libplacebo-7.360.1-vulkan-packed-yuv444.patch), based on libplacebo v7.360.1 (`cee9b076f2c63104ccfd497fa79c39a867293ec4`).
- [Implementation and limits](WRITEUP.md).
- `prepare-private-headers.py`: creates a private include override from exact, hash-checked v7.360.1 headers. No system installation.
- `tests/packed-drm-test.c`: descriptor, channel, metadata, failure, and unchanged-path tests.
- `tests/vulkan-sampling-test.c`: real Vulkan shader rendering of packed words, including PQ and BT.2020.
- `tests/vaapi-dmabuf-probe.c`: optional offscreen real hardware decoder/import/color probe.

## Build Moonlight with private headers

Moonlight compiles `PL_LIBAV_IMPLEMENTATION` in `plvk_c.c`. Recompiling it against the patched header is sufficient; replacing the shared libplacebo library is unnecessary. These headers target the already installed libplacebo **7.360.1** library ABI. Review/rebase for another version.

```sh
python3 prepare-private-headers.py /usr/include /tmp/moonlight-private-include
# In the Moonlight build, put /tmp/moonlight-private-include before system
# include paths, for example via qmake's INCLUDEPATH. Rebuild plvk_c.c.
```

The preparation script refuses modified or mismatched source headers and an existing output directory. It changes only its new output directory. Use the matching unmodified source include directory if a distribution has applied unrelated header patches; inspect that difference before mixing headers and libraries.

## Offscreen hardware probe

Compile against the private headers and installed libraries:

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-function \
  -I/tmp/moonlight-private-include tests/vaapi-dmabuf-probe.c \
  $(pkg-config --cflags --libs libplacebo libavutil libavcodec libavformat vulkan) \
  -lm -o /tmp/vaapi-dmabuf-probe
/tmp/vaapi-dmabuf-probe /dev/dri/renderD128
```

The real mode requires a hardware Vulkan device corresponding to the same render node as VAAPI. It decodes Moonlight's existing HEVC 8/10-bit 4:4:4 probe packets with `get_format` restricted to VAAPI, exports a direct DMA-BUF, imports/renders through the patched header, and compares output with a separately software-decoded color oracle. The software reference is test-only. Real mode refuses software fallback for the hardware frame.

It creates no window, opens no WSI surface, changes no display mode, and does not restart Steam. It allocates temporary GPU contexts/buffers. It reports the actual decoded software pixel format, exported FourCC/modifier, chosen Vulkan alias, and maximum color error. Successful offscreen PQ rendering still does not establish a working HDR compositor/swapchain or the TV's observed output.

## Intel hardware validation

On the OXP3 with Intel iHD 26.3.5, FFmpeg 9.0.2 and Mesa 26.2.4, both 1280×720 decoder probe packets used real VAAPI decoding and direct Vulkan DMA-BUF import. The exported modifier was `0x0100000000000009`. XYUV used BGRA8 with zero maximum color-oracle error; Y410 used RGB10A2 with maximum normalized output error 0.00115965 (test threshold 0.003). PQ, BT.2020 and mastering luminance survived the import. This does not establish full-resolution throughput or HDR display signaling.

## Reproduce off-device checks

The validation used Debian 13 ARM64, GCC 14.2, FFmpeg 7.1 library APIs (`libavutil 59.39.100`), libplacebo v7.360.1, pinned Vulkan-Headers `450bd2232225d6c7728a4108055ac2e37cef6475`, and Lavapipe for actual Vulkan shader execution.

In a Linux environment with Meson, Ninja, GCC/C++, pkg-config, Python/Jinja2, libavutil/libavcodec/libavformat development packages, Vulkan loader, glslang, and a Vulkan implementation:

```sh
git clone https://github.com/haasn/libplacebo.git libplacebo
git -C libplacebo checkout cee9b076f2c63104ccfd497fa79c39a867293ec4
git -C libplacebo submodule update --init 3rdparty/fast_float 3rdparty/Vulkan-Headers
patch -p1 -d libplacebo < patches/libplacebo-7.360.1-vulkan-packed-yuv444.patch
meson setup libplacebo/build libplacebo -Dtests=true -Ddemos=false \
  -Dopengl=disabled -Dd3d11=disabled -Dvulkan=enabled \
  -Dshaderc=disabled -Dglslang=enabled -Dlibdovi=disabled -Dunwind=disabled \
  -Ddefault_library=both
meson compile -C libplacebo/build
meson test -C libplacebo/build --print-errorlogs
./run-tests.sh
SANITIZE=1 ./run-tests.sh libplacebo build-tests-sanitize
```

`run-tests.sh` builds the hardware probe but runs only its explicitly labeled software reference mode. Run the real hardware probe separately on the target device.

Passed off-device: all 14 configured upstream suites; 6 packed descriptor mappings, 13 rejection/failure cases, unchanged RGB routing and source descriptor checks; 168 real Vulkan rendering cases across XYUV/Y410/XV30, linear/PQ, gray/asymmetric chroma, range endpoints and padding values; software probe oracle. The custom cases also passed ASan/UBSan. Driver leak reporting is excluded for Vulkan cases; the descriptor-only test runs with leak detection enabled.

The patch and source are subject to libplacebo's LGPL-2.1-or-later license. Test HEVC packets are extracted unchanged from [Moonlight v6.2.0](https://github.com/moonlight-stream/moonlight-qt/blob/v6.2.0/app/streaming/video/ffmpeg_videosamples.cpp), under Moonlight's original license. No device captures or compiled artifacts are part of the candidate source.
