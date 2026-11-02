#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#define PL_LIBAV_IMPLEMENTATION 1
#include <libplacebo/utils/libav.h>

static int cases;

static void test_sample(pl_vulkan vk, pl_renderer renderer, uint32_t fourcc,
                        int y, int u, int v, unsigned pad, bool hdr)
{
    pl_gpu gpu = vk->gpu;
    struct pl_drm_packed_yuv_layout layout;
    assert(pl_get_drm_packed_yuv_layout(fourcc, &layout));
    pl_fmt in_fmt = pl_find_named_fmt(gpu, layout.format);
    pl_fmt out_fmt = pl_find_named_fmt(gpu, "rgba32f");
    assert(in_fmt && out_fmt);
    uint32_t word = layout.depth == 8 ?
        ((uint32_t)v | ((uint32_t)u << 8) | ((uint32_t)y << 16) | (pad << 24)) :
        ((uint32_t)u | ((uint32_t)y << 10) | ((uint32_t)v << 20) | (pad << 30));
    pl_tex in_tex = pl_tex_create(gpu, pl_tex_params(
        .w = 1, .h = 1, .format = in_fmt, .sampleable = true, .initial_data = &word,
    ));
    pl_tex out_tex = pl_tex_create(gpu, pl_tex_params(
        .w = 1, .h = 1, .format = out_fmt, .renderable = true, .host_readable = true,
    ));
    assert(in_tex && out_tex);
    struct pl_frame src = {
        .num_planes = 1,
        .planes[0] = {.texture = in_tex, .components = 3},
        .repr = {
            .sys = PL_COLOR_SYSTEM_BT_2020_NC,
            .levels = PL_COLOR_LEVELS_FULL, .alpha = PL_ALPHA_NONE,
            .bits = {.sample_depth = layout.depth, .color_depth = layout.depth},
        },
        .color = {.primaries = PL_COLOR_PRIM_BT_2020, .transfer = PL_COLOR_TRC_LINEAR},
    };
    memcpy(src.planes[0].component_mapping, layout.component_mapping,
           sizeof(layout.component_mapping));
    if (hdr) {
        src.color.transfer = PL_COLOR_TRC_PQ;
        src.color.hdr.max_luma = 1000;
        src.color.hdr.min_luma = 0.005;
    }
    struct pl_frame dst = {
        .num_planes = 1,
        .planes[0] = {.texture = out_tex, .components = 3, .component_mapping = {0,1,2}},
        .repr = {.sys = PL_COLOR_SYSTEM_RGB, .levels = PL_COLOR_LEVELS_FULL,
                 .alpha = PL_ALPHA_NONE, .bits = {.sample_depth = 32, .color_depth = 32}},
        .color = src.color,
    };
    struct pl_render_params params = pl_render_default_params;
    params.color_map_params = pl_color_map_params(
        .tone_mapping_function = &pl_tone_map_clip,
        .gamut_mapping = &pl_gamut_map_clip,
    );
    assert(pl_render_image(renderer, &src, &dst, &params));
    assert(pl_renderer_get_errors(renderer).errors == PL_RENDER_ERR_NONE);
    float pixel[4];
    assert(pl_tex_download(gpu, pl_tex_transfer_params(.tex = out_tex, .ptr = pixel)));
    double max = (1 << layout.depth) - 1;
    double yy = y / max, cb = (u - (1 << (layout.depth - 1))) / max;
    double cr = (v - (1 << (layout.depth - 1))) / max;
    // ITU-R BT.2020 non-constant luminance, full-range 8/10-bit YCbCr.
    double expected[3] = {
        yy + 1.4746 * cr,
        yy - (0.0593 * 1.8814 / 0.6780) * cb - (0.2627 * 1.4746 / 0.6780) * cr,
        yy + 1.8814 * cb,
    };
    for (int c = 0; c < 3; c++) {
        if (fabs(pixel[c] - expected[c]) > 0.002) {
            fprintf(stderr, "%s %s YUV=%d,%d,%d pad=%u channel%d expected%.6f got%.6f\n",
                    layout.format, hdr ? "PQ" : "linear", y,u,v,pad,c,expected[c],pixel[c]);
            assert(false);
        }
    }
    assert(fabs(pixel[3] - 1) < 0.001);
    cases++;
    pl_tex_destroy(gpu, &in_tex);
    pl_tex_destroy(gpu, &out_tex);
}

int main(void)
{
    pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = pl_log_simple, .log_level = PL_LOG_WARN));
    pl_vulkan vk = pl_vulkan_create(log, pl_vulkan_params(.allow_software = true));
    assert(vk);
    pl_renderer renderer = pl_renderer_create(log, vk->gpu);
    assert(renderer);
    const uint32_t formats[] = {MKTAG('X','Y','U','V'), MKTAG('Y','4','1','0'), MKTAG('X','V','3','0')};
    for (int f = 0; f < 3; f++) {
        int depth = f ? 10 : 8, max = (1 << depth)-1, mid = 1 << (depth-1);
        const int ys[] = {0, max/4, mid, 3*max/4, max};
        const unsigned pads[] = {0, f ? 1 : 85, f ? 2 : 170, f ? 3 : 255};
        for (int h = 0; h < 2; h++) {
            for (int p = 0; p < 4; p++) {
                for (int i = 0; i < 5; i++)
                    test_sample(vk, renderer, formats[f], ys[i], mid, mid, pads[p], h);
                test_sample(vk, renderer, formats[f], mid, mid-max/10, mid+max/10, pads[p], h);
                test_sample(vk, renderer, formats[f], mid, mid+max/10, mid-max/10, pads[p], h);
            }
        }
    }
    printf("Vulkan packed-format sampling: %d BT.2020 linear/PQ range/channel/padding cases passed.\n", cases);
    pl_renderer_destroy(&renderer);
    pl_vulkan_destroy(&vk);
    pl_log_destroy(&log);
    return 0;
}
