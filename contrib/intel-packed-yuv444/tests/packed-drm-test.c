#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <libplacebo/gpu.h>
#include <libplacebo/vulkan.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mastering_display_metadata.h>

static bool fake_vulkan = true;
static bool fail_import;
static int imports;
static struct pl_tex_t imported;
static pl_vulkan test_vulkan_get(pl_gpu gpu)
{
    (void) gpu;
    static const struct pl_vulkan_t vk = {0};
    return fake_vulkan ? &vk : NULL;
}
static pl_tex test_tex_create(pl_gpu gpu, const struct pl_tex_params *params)
{
    (void) gpu;
    imports++;
    imported = (struct pl_tex_t) {.params = *params};
    return fail_import ? NULL : &imported;
}

#define pl_vulkan_get test_vulkan_get
#define pl_tex_create test_tex_create
#define PL_LIBAV_IMPLEMENTATION 1
#include <libplacebo/utils/libav.h>
#undef pl_tex_create
#undef pl_vulkan_get

static uint64_t linear[] = {0};
static const struct pl_fmt_t bgra = {
    .name = "bgra8", .type = PL_FMT_UNORM, .num_components = 4,
    .component_depth = {8,8,8,8}, .modifiers = linear, .num_modifiers = 1,
    .fourcc = MKTAG('A','R','2','4'),
};
static const struct pl_fmt_t rgb10 = {
    .name = "rgb10a2", .type = PL_FMT_UNORM, .num_components = 4,
    .component_depth = {10,10,10,2}, .modifiers = linear, .num_modifiers = 1,
};
static pl_fmt formats[] = {&bgra, &rgb10};
static struct pl_gpu_t fake_gpu = {
    .formats = formats, .num_formats = 2,
    .import_caps.tex = PL_HANDLE_DMA_BUF,
};

static AVFrame *make_frame(uint32_t fourcc, enum AVPixelFormat swfmt)
{
    AVFrame *frame = av_frame_alloc();
    assert(frame);
    frame->format = AV_PIX_FMT_DRM_PRIME;
    frame->width = 13;
    frame->height = 7;
    frame->crop_left = 1;
    frame->color_primaries = AVCOL_PRI_BT2020;
    frame->color_trc = AVCOL_TRC_SMPTE2084;
    frame->colorspace = AVCOL_SPC_BT2020_NCL;
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->hw_frames_ctx = av_buffer_allocz(sizeof(AVHWFramesContext));
    assert(frame->hw_frames_ctx);
    ((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format = swfmt;
    frame->buf[0] = av_buffer_allocz(sizeof(AVDRMFrameDescriptor));
    assert(frame->buf[0]);
    frame->data[0] = frame->buf[0]->data;
    AVDRMFrameDescriptor *drm = (void *)frame->data[0];
    *drm = (AVDRMFrameDescriptor) {
        .nb_objects = 1,
        .objects[0] = {.fd = 13, .size = 4096, .format_modifier = 0},
        .nb_layers = 1,
        .layers[0] = {.format = fourcc, .nb_planes = 1,
            .planes[0] = {.object_index = 0, .offset = 64, .pitch = 128}},
    };
    AVMasteringDisplayMetadata *hdr = av_mastering_display_metadata_create_side_data(frame);
    assert(hdr);
    hdr->has_luminance = 1;
    hdr->max_luminance = (AVRational){1000, 1};
    hdr->min_luminance = (AVRational){1, 200};
    return frame;
}

static bool map(AVFrame *frame, struct pl_frame *out)
{
    AVDRMFrameDescriptor before = *(AVDRMFrameDescriptor *)frame->data[0];
    enum AVPixelFormat before_swfmt = ((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format;
    pl_frame_from_avframe(out, frame);
    imports = 0;
    bool ok = pl_map_avframe_drm(&fake_gpu, out, frame);
    assert(!memcmp(&before, frame->data[0], sizeof(before)));
    assert(before_swfmt == ((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format);
    return ok;
}

static void test_mapping(uint32_t fourcc, enum AVPixelFormat swfmt, int depth)
{
    AVFrame *frame = make_frame(fourcc, swfmt);
    struct pl_frame out;
    assert(map(frame, &out));
    assert(imports == 1);
    assert(out.num_planes == 1 && out.planes[0].components == 3);
    assert(out.planes[0].texture->params.format == (depth == 8 ? &bgra : &rgb10));
    assert(out.planes[0].component_mapping[0] == (depth == 8 ? 2 : 1));
    assert(out.planes[0].component_mapping[1] == (depth == 8 ? 1 : 0));
    assert(out.planes[0].component_mapping[2] == (depth == 8 ? 0 : 2));
    assert(out.planes[0].component_mapping[3] == PL_CHANNEL_NONE);
    assert(out.repr.bits.sample_depth == depth && out.repr.bits.color_depth == depth);
    assert(out.repr.bits.bit_shift == 0 && out.repr.alpha == PL_ALPHA_NONE);
    assert(out.repr.sys == PL_COLOR_SYSTEM_BT_2020_NC);
    assert(out.repr.levels == PL_COLOR_LEVELS_FULL);
    assert(out.color.primaries == PL_COLOR_PRIM_BT_2020);
    assert(out.color.transfer == PL_COLOR_TRC_PQ);
    assert(fabs(out.color.hdr.max_luma - 1000.0) < 0.001);
    assert(out.crop.x0 == 1 && out.crop.x1 == frame->width);
    assert(imported.params.w == 13 && imported.params.h == 7);
    assert(imported.params.import_handle == PL_HANDLE_DMA_BUF);
    assert(imported.params.shared_mem.handle.fd == 13);
    assert(imported.params.shared_mem.offset == 64);
    assert(imported.params.shared_mem.stride_w == 128);
    assert(imported.params.shared_mem.size == 4096);
    assert(imported.params.shared_mem.drm_format_mod == 0);
    av_frame_free(&frame);
}

static void test_failures(void)
{
    AVFrame *frame = make_frame(MKTAG('Y','4','1','0'), AV_PIX_FMT_YUV444P10LE);
    AVDRMFrameDescriptor *drm = (void *)frame->data[0];
    struct pl_frame out;
    fake_vulkan = false;
    assert(!map(frame, &out) && !imports);
    fake_vulkan = true;
    drm->objects[0].format_modifier = 1234;
    assert(!map(frame, &out) && !imports);
    drm->objects[0].format_modifier = 0;
    fake_gpu.num_formats = 1;
    assert(!map(frame, &out) && !imports);
    fake_gpu.num_formats = 2;
    drm->layers[0].planes[0].pitch = 51;
    assert(!map(frame, &out) && !imports);
    drm->layers[0].planes[0].pitch = 128;
    ((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format = AV_PIX_FMT_YUV444P;
    assert(!map(frame, &out) && !imports);
    ((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format = AV_PIX_FMT_YUV420P10LE;
    assert(!map(frame, &out) && !imports);
    ((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format = AV_PIX_FMT_YUV444P10LE;
    fail_import = true;
    assert(!map(frame, &out) && imports == 1);
    fail_import = false;
    av_frame_free(&frame);
}

static void test_existing_rgb_path(void)
{
    AVFrame *frame = make_frame(MKTAG('A','R','2','4'), AV_PIX_FMT_BGRA);
    struct pl_frame out, before;
    pl_frame_from_avframe(&before, frame);
    assert(map(frame, &out) && imports == 1);
    assert(out.num_planes == before.num_planes);
    assert(out.planes[0].components == before.planes[0].components);
    assert(!memcmp(out.planes[0].component_mapping, before.planes[0].component_mapping,
                   sizeof(before.planes[0].component_mapping)));
    assert(out.repr.sys == before.repr.sys);
    av_frame_free(&frame);
}

static void test_unsupported_counts(void)
{
    const uint32_t formats[] = {
        MKTAG('X','Y','U','V'), MKTAG('Y','4','1','0'), MKTAG('X','V','3','0'),
    };
    for (int f = 0; f < 3; f++) {
        AVFrame *frame = make_frame(formats[f], f ? AV_PIX_FMT_YUV444P10LE : AV_PIX_FMT_YUV444P);
        AVDRMFrameDescriptor *drm = (void *)frame->data[0];
        struct pl_frame out;
        // The planar software description has three planes. Recognize the
        // packed export before the legacy mapper asserts about that count.
        drm->layers[0].nb_planes = 2;
        assert(!map(frame, &out) && !imports);
        assert(out.num_planes == 3);
        drm->layers[0].nb_planes = 1;
        drm->nb_layers = 2;
        assert(!map(frame, &out) && !imports);
        assert(out.num_planes == 3);
        av_frame_free(&frame);
    }
}

int main(void)
{
    test_mapping(MKTAG('X','Y','U','V'), AV_PIX_FMT_YUV444P, 8);
    test_mapping(MKTAG('X','Y','U','V'), av_get_pix_fmt("vuyx"), 8);
    test_mapping(MKTAG('Y','4','1','0'), AV_PIX_FMT_YUV444P10LE, 10);
    test_mapping(MKTAG('Y','4','1','0'), av_get_pix_fmt("xv30le"), 10);
    test_mapping(MKTAG('X','V','3','0'), AV_PIX_FMT_YUV444P10LE, 10);
    test_mapping(MKTAG('X','V','3','0'), av_get_pix_fmt("xv30le"), 10);
    test_failures();
    test_unsupported_counts();
    test_existing_rgb_path();
    puts("Packed DRM mapping: 6 packed mappings, 13 failures, unchanged RGB path/source descriptors passed.");
    return 0;
}
