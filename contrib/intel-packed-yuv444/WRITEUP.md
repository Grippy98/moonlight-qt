# Packed VAAPI 4:4:4 into Vulkan

## Failure point

The OXP3's Intel iHD driver advertises HEVC 4:4:4 decode at 8/10 bits. Intel uses fully packed words for these outputs: XYUV/VUYX at 8 bits and Y410/XV30 at 10 bits. libplacebo's normal DRM frame mapper looks up the exported FourCC in its texture format table, which has no corresponding packed YUV entry. The failure can therefore be reported as unsupported hardware decoding even though decoding itself is supported.

This corresponds to [libplacebo issue 282](https://github.com/haasn/libplacebo/issues/282) and the maintainer's diagnosis of [Moonlight issue 1512](https://github.com/moonlight-stream/moonlight-qt/issues/1512#issuecomment-2644403997).

## Candidate mapping

Vulkan DMA-BUF image import takes a `VkFormat`, a DRM modifier, offsets, pitches and a file descriptor. It does not accept the original DRM FourCC as a separate import parameter. The candidate selects bit-identical, sampleable RGB storage layouts while retaining YUV semantic metadata:

| DRM export | Storage, low to high | Vulkan/libplacebo format | Host-component semantic map |
| --- | --- | --- | --- |
| XYUV | Cr8, Cb8, Y8, X8 | BGRA8 | Cr, Cb, Y, unused |
| Y410 / XV30 | Cb10, Y10, Cr10, X2 | RGB10A2 | Cb, Y, Cr, unused |

`pl_plane.component_mapping` refers to host storage component order; the renderer separately applies the texture format's `sample_order`. An actual Vulkan regression test caught an initially incorrect XYUV map that a descriptor-only test missed. The corrected XYUV map is `{2,1,0,-1}`; Y410 is `{1,0,2,-1}`.

No RGB conversion is applied during import. The existing renderer performs the original YCbCr conversion using retained BT.2020 coefficients, full/limited range and PQ transfer metadata. Packed channels are normalized as 8/8 or 10/10 bits with zero bit shift. The padding/alpha field is ignored.

## Scope and failure behavior

The alias is limited to a Vulkan GPU, one DRM layer with one plane, a non-RGB/non-subsampled three-component software format, matching 8/10-bit depth and sufficient pitch. This permits FFmpeg to describe the original frame as planar YUV444 while exporting a single packed DRM surface. The selected RGB format must exist and advertise the source modifier. Vulkan image creation must also succeed.

The mapper keeps the source descriptor, file descriptor, offset, pitch, modifier and frame context unchanged. Other FourCCs retain the original path. Unsupported layouts or modifiers fail rather than silently copying back to the CPU or guessing another format. It does not support an auxiliary compression plane or a generally arbitrary packed YUV format.

An RGB alias with a matching modifier is a necessary condition, not proof of compatibility with every driver's video surface layout. The OXP3's actual exported modifier must pass direct import and output comparison. If it does not, a different solution such as explicit GPU video processing into an importable planar layout may be necessary. That fallback is not implemented here.

## Validation boundaries

Off-device descriptor tests check metadata, depth, component maps, source immutability and refusal paths. A real Vulkan renderer test compares shader output against BT.2020 full-range YCbCr expectations for gray/asymmetric chroma and padding variations in linear and PQ modes. It runs on Lavapipe for reproducibility and establishes format/channel arithmetic, not Intel DMA-BUF compatibility.

The optional target probe forces real VAAPI decoding, exports/inspects the surface, imports it through the candidate, and compares offscreen PQ output with an independent software-decoded test oracle. It verifies VAAPI and Vulkan select the same render node, rejects a CPU Vulkan device, and never enables runtime software fallback. The oracle's software decoding and output readback are confined to the test.

Even a passing target probe establishes only offscreen decode/import/render interoperability. HDR output additionally needs an HDR-capable presentation path, such as Gamescope WSI, native Wayland or DRM/KMS, and successful swapchain/display negotiation. It does not prove 4K/144 performance, HDR TV signaling or gameplay pacing.

## Upstream direction

The import belongs in libplacebo's FFmpeg/DRM adapter, or a more general upstream packed-format representation if preferred by maintainers. The patch changes no shared-library ABI because this adapter is header-only. Moonlight already compiles the adapter in `plvk_c.c`, so a private header override and rebuild can test it without replacing the system libplacebo installation. Submission should include successful real Intel modifier/color tests and retain the narrow unsupported-layout behavior.
