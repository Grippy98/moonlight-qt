/* Offscreen interoperability probe. Never opens a display or changes settings.
 * Real mode requires VAAPI for the tested frame; software decoding is only a
 * separately rendered color oracle. --software-reference validates that oracle
 * off-device and does not claim to exercise hardware decoding or DMA-BUF. */
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#define PL_LIBAV_IMPLEMENTATION 1
#include <libplacebo/utils/libav.h>
#include "hevc-fixtures.h"

static void require(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAILED: %s\n", what);
        exit(1);
    }
}

static enum AVPixelFormat require_vaapi(AVCodecContext *ctx, const enum AVPixelFormat *formats)
{
    (void) ctx;
    for (const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++)
        if (*p == AV_PIX_FMT_VAAPI)
            return *p;
    return AV_PIX_FMT_NONE;
}

static AVFrame *decode(const uint8_t *data, size_t size, AVBufferRef *device)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    require(codec != NULL, "HEVC decoder available");
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    require(ctx != NULL, "allocate decoder context");
    ctx->thread_count = 1;
    if (device) {
        ctx->hw_device_ctx = av_buffer_ref(device);
        ctx->get_format = require_vaapi;
    }
    require(avcodec_open2(ctx, codec, NULL) >= 0, "open HEVC decoder");
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    require(packet && frame && av_new_packet(packet, size) >= 0, "allocate packet/frame");
    memcpy(packet->data, data, size);
    require(avcodec_send_packet(ctx, packet) >= 0, "submit HEVC packet");
    int status = avcodec_receive_frame(ctx, frame);
    if (status == AVERROR(EAGAIN)) {
        require(avcodec_send_packet(ctx, NULL) >= 0, "flush decoder");
        status = avcodec_receive_frame(ctx, frame);
    }
    require(status >= 0, "receive decoded HEVC frame");
    if (device)
        require(frame->format == AV_PIX_FMT_VAAPI, "hardware frame is VAAPI; software fallback refused");
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    return frame;
}

static void set_hdr_test_metadata(AVFrame *frame)
{
    frame->color_primaries = AVCOL_PRI_BT2020;
    frame->color_trc = AVCOL_TRC_SMPTE2084;
    frame->colorspace = AVCOL_SPC_BT2020_NCL;
    frame->color_range = AVCOL_RANGE_JPEG;
    av_frame_remove_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    AVMasteringDisplayMetadata *hdr = av_mastering_display_metadata_create_side_data(frame);
    require(hdr != NULL, "allocate HDR test metadata");
    hdr->has_luminance = 1;
    hdr->max_luminance = (AVRational){1000,1};
    hdr->min_luminance = (AVRational){1,200};
}

static void inspect_drm(AVFrame *frame)
{
    const AVHWFramesContext *ctx = (void *)frame->hw_frames_ctx->data;
    printf("Decoded VAAPI surface: %dx%d sw_format=%s\n", frame->width, frame->height,
           av_get_pix_fmt_name(ctx->sw_format));
    AVFrame *drm_frame = av_frame_alloc();
    require(drm_frame != NULL, "allocate direct-map frame");
    drm_frame->format = AV_PIX_FMT_DRM_PRIME;
    drm_frame->hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
    require(av_hwframe_map(drm_frame, frame, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT) >= 0,
            "direct VAAPI to DMA-BUF map (no CPU copyback)");
    const AVDRMFrameDescriptor *drm = (void *)drm_frame->data[0];
    printf("DMA-BUF objects=%d layers=%d\n", drm->nb_objects, drm->nb_layers);
    for (int l = 0; l < drm->nb_layers; l++) {
        uint32_t f = drm->layers[l].format;
        printf("  layer%d fourcc=%c%c%c%c planes=%d\n", l,
               f & 255, (f >> 8) & 255, (f >> 16) & 255, (f >> 24) & 255,
               drm->layers[l].nb_planes);
        for (int p = 0; p < drm->layers[l].nb_planes; p++) {
            const AVDRMPlaneDescriptor *plane = &drm->layers[l].planes[p];
            require(plane->object_index >= 0 && plane->object_index < drm->nb_objects,
                    "valid DRM object index");
            printf("    plane%d offset=%td pitch=%td modifier=%016" PRIx64 "\n", p,
                   plane->offset, plane->pitch, drm->objects[plane->object_index].format_modifier);
        }
    }
    av_frame_free(&drm_frame);
}

static float *render(pl_gpu gpu, pl_renderer renderer, AVFrame *frame)
{
    pl_tex backing[4] = {0};
    struct pl_frame src = {0};
    require(pl_map_avframe_ex(gpu, &src, pl_avframe_params(
        .frame = frame, .tex = backing, .map_dovi = false)), "map decoded frame into Vulkan renderer");
    require(src.repr.sys == PL_COLOR_SYSTEM_BT_2020_NC && src.color.transfer == PL_COLOR_TRC_PQ,
            "BT.2020/PQ metadata preserved");
    require(fabs(src.color.hdr.max_luma - 1000) < 0.001, "mastering luminance preserved");
    printf("Mapped plane0=%s planes=%d depth=%d/%d shift=%d map=%d,%d,%d\n",
           src.planes[0].texture->params.format->name, src.num_planes,
           src.repr.bits.color_depth, src.repr.bits.sample_depth, src.repr.bits.bit_shift,
           src.planes[0].component_mapping[0], src.planes[0].component_mapping[1],
           src.planes[0].component_mapping[2]);
    pl_fmt format = pl_find_named_fmt(gpu, "rgba32f");
    require(format != NULL, "RGBA32F output supported");
    pl_tex output = pl_tex_create(gpu, pl_tex_params(
        .w = frame->width, .h = frame->height, .format = format,
        .renderable = true, .host_readable = true));
    require(output != NULL, "allocate offscreen float target");
    struct pl_frame dst = {
        .num_planes = 1,
        .planes[0] = {.texture = output, .components = 3, .component_mapping = {0,1,2}},
        .repr = {.sys = PL_COLOR_SYSTEM_RGB, .levels = PL_COLOR_LEVELS_FULL,
                 .alpha = PL_ALPHA_NONE, .bits = {.sample_depth = 32, .color_depth = 32}},
        .color = src.color,
    };
    struct pl_render_params params = pl_render_default_params;
    params.color_map_params = pl_color_map_params(
        .tone_mapping_function = &pl_tone_map_clip,
        .gamut_mapping = &pl_gamut_map_clip);
    require(pl_render_image(renderer, &src, &dst, &params), "render offscreen BT.2020/PQ frame");
    require(pl_renderer_get_errors(renderer).errors == PL_RENDER_ERR_NONE, "no renderer errors");
    float *pixels = calloc((size_t)frame->width * frame->height * 4, sizeof(*pixels));
    require(pixels != NULL, "allocate output readback for comparison");
    require(pl_tex_download(gpu, pl_tex_transfer_params(.tex = output, .ptr = pixels)), "read test output");
    pl_tex_destroy(gpu, &output);
    pl_unmap_avframe(gpu, &src);
    for (int p = 0; p < 4; p++)
        pl_tex_destroy(gpu, &backing[p]);
    return pixels;
}

int main(int argc, char **argv)
{
    bool software = argc == 2 && !strcmp(argv[1], "--software-reference");
    const char *node = argc > 1 && !software ? argv[1] : "/dev/dri/renderD128";
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [RENDER_NODE|--software-reference]\n", argv[0]);
        return 2;
    }
    av_log_set_level(AV_LOG_WARNING);
    setbuf(stdout, NULL);
    printf("Mode: %s\n", software ? "SOFTWARE REFERENCE ONLY (not a hardware test)" : "REQUIRED VAAPI + direct DMA-BUF import");
    AVBufferRef *device = NULL;
    if (!software)
        require(av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, node, NULL, 0) >= 0,
                "create VAAPI render-node context");
    pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = pl_log_simple, .log_level = PL_LOG_WARN));
    pl_vulkan vk = pl_vulkan_create(log, pl_vulkan_params(.allow_software = software));
    require(vk != NULL, "create offscreen Vulkan context");
    VkPhysicalDeviceDrmPropertiesEXT drm_props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drm_props};
    vkGetPhysicalDeviceProperties2(vk->phys_device, &props);
    printf("Vulkan device: %s\n", props.properties.deviceName);
    if (!software) {
        struct stat info;
        require(stat(node, &info) == 0 && S_ISCHR(info.st_mode), "render node is a character device");
        require(drm_props.hasRender && drm_props.renderMajor == major(info.st_rdev) &&
                drm_props.renderMinor == minor(info.st_rdev), "VAAPI and Vulkan use the same render device");
        require(props.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU, "Vulkan device is hardware");
    }
    pl_renderer renderer = pl_renderer_create(log, vk->gpu);
    require(renderer != NULL, "create offscreen renderer");
    const uint8_t *packets[] = {k_HEVCRExt8_444TestFrame, k_HEVCRExt10_444TestFrame};
    const size_t sizes[] = {sizeof(k_HEVCRExt8_444TestFrame), sizeof(k_HEVCRExt10_444TestFrame)};
    for (int t = 0; t < 2; t++) {
        printf("HEVC %d-bit 4:4:4 test\n", t ? 10 : 8);
        AVFrame *reference = decode(packets[t], sizes[t], NULL);
        set_hdr_test_metadata(reference);
        float *expected = render(vk->gpu, renderer, reference);
        if (!software) {
            AVFrame *hardware = decode(packets[t], sizes[t], device);
            require(hardware->width == reference->width && hardware->height == reference->height,
                    "hardware/reference dimensions agree");
            set_hdr_test_metadata(hardware);
            inspect_drm(hardware);
            float *actual = render(vk->gpu, renderer, hardware);
            size_t count = (size_t)reference->width * reference->height * 4;
            double max_error = 0;
            for (size_t p = 0; p < count; p++) {
                require(isfinite(actual[p]) && isfinite(expected[p]), "finite rendered samples");
                double error = fabs(actual[p] - expected[p]);
                if (error > max_error)
                    max_error = error;
            }
            printf("Hardware versus software-reference maximum output error: %.8f\n", max_error);
            require(max_error < 0.003, "hardware import/render matches color oracle");
            free(actual);
            av_frame_free(&hardware);
        }
        free(expected);
        av_frame_free(&reference);
    }
    pl_renderer_destroy(&renderer);
    pl_vulkan_destroy(&vk);
    pl_log_destroy(&log);
    av_buffer_unref(&device);
    puts(software ? "Software oracle passed. Hardware remains untested." :
         "VAAPI hardware decode + direct Vulkan DMA-BUF import + PQ color comparison passed.");
    return 0;
}
