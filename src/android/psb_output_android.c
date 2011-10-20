/*
 * Copyright (c) 2011 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Zhaohan Ren  <zhaohan.ren@intel.com>
 *    Shengquan Yuan  <shengquan.yuan@intel.com>
 *    Jiang Fei <jiang.fei@intel.com>
 *    Binglin Chen <binglin.chen@intel.com>
 *
 */

#include <va/va_backend.h>
#include "psb_output.h"
#include "psb_surface.h"
#include "psb_buffer.h"
#include "psb_overlay.h"
#include "psb_texture.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "psb_android_glue.h"
#include "psb_texstreaming.h"
#include "psb_output_android.h"
#include "psb_HDMIExtMode.h"
#include "pnw_rotate.h"
#include <wsbm/wsbm_manager.h>
#include <psb_drm.h>
#include <hardware.h>

#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData;
#define INIT_OUTPUT_PRIV    psb_android_output_p output = (psb_android_output_p)(((psb_driver_data_p)ctx->pDriverData)->ws_priv)

#define SURFACE(id) ((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))
#define IMAGE(id)  ((object_image_p) object_heap_lookup( &driver_data->image_heap, id ))
#define SUBPIC(id)  ((object_subpic_p) object_heap_lookup( &driver_data->subpic_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))

#define GET_SURFACE_INFO_rotate(psb_surface) ((int) psb_surface->extra_info[5])
#define GET_SURFACE_INFO_protect(psb_surface) ((int) psb_surface->extra_info[6])
#define MAX_OVERLAY_IDLE_FRAME 4

enum {
    eWidiOff             = 1,
    eWidiClone           = 2,
    eWidiExtendedVideo   = 3,
};
extern unsigned int update_forced;

inline int va2hw_rotation(int va_rotate)
{
    switch (va_rotate) {
    case VA_ROTATION_90:
        return HAL_TRANSFORM_ROT_270;
    case VA_ROTATION_180:
        return HAL_TRANSFORM_ROT_180;
    case VA_ROTATION_270:
        return HAL_TRANSFORM_ROT_90;
defaut:
        return 0;
    }

    return 0;
}

unsigned char *psb_android_output_init(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    char put_surface[1024];
    struct drm_psb_register_rw_arg regs;
    psb_android_output_p output = calloc(1, sizeof(psb_android_output_s));
    struct fb_var_screeninfo vinfo;
    int fbfd = -1;
    int ret;

    if (output == NULL) {
        psb__error_message("Can't malloc memory\n");
        return NULL;
    }
    memset(output, 0, sizeof(psb_android_output_s));

    /* Guess the screen size */
    output->screen_width = 800;
    output->screen_height = 480;

    // Open the frame buffer for reading
    memset(&vinfo, 0, sizeof(vinfo));
    fbfd = open("/dev/graphics/fb0", O_RDONLY);
    if (fbfd) {
        if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo))
            psb__information_message("Error reading screen information.\n");
    }
    close(fbfd);
    output->screen_width = vinfo.xres;
    output->screen_height = vinfo.yres;

    /* TS by default */
    driver_data->output_method = PSB_PUTSURFACE_TEXSTREAMING;
    driver_data->color_key = 0x000001; /*light blue*/
    driver_data->overlay_idle_frame = 1;

    if (psb_parse_config("PSB_VIDEO_CTEXTURES", &put_surface[0]) == 0) {
        psb__information_message("PSB_VIDEO_CTEXTURES is enabled for vaPutSurfaceBuf\n");
        driver_data->ctexture = 1; /* Init CTEXTURE for vaPutSurfaceBuf */
    }

    if (psb_parse_config("PSB_VIDEO_TS", &put_surface[0]) == 0) {
        psb__information_message("Putsurface use texstreaming\n");
        driver_data->output_method = PSB_PUTSURFACE_FORCE_TEXSTREAMING;
    }

    if (psb_parse_config("PSB_VIDEO_COVERLAY", &put_surface[0]) == 0) {
        psb__information_message("Putsurface use client overlay\n");
        driver_data->output_method = PSB_PUTSURFACE_FORCE_COVERLAY;
    }

    if (IS_MFLD(driver_data)) {
        driver_data->coverlay = 1;
        output->psb_HDMIExt_info = psb_HDMIExt_init(ctx, output);
        if (!output->psb_HDMIExt_info) {
            psb__error_message("Failed to init psb_HDMIExt.\n");
            free(output);
            return NULL;
        }
    }

    return (unsigned char *)output;
}

VAStatus psb_android_output_deinit(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    INIT_OUTPUT_PRIV;
    //psb_android_output_p output = GET_OUTPUT_DATA(ctx);
    if (IS_MFLD(driver_data)) {
        psb_HDMIExt_deinit(output);
    }

    return VA_STATUS_SUCCESS;
}

static VAStatus psb_putsurface_ctexture(
    VADriverContextP ctx,
    VASurfaceID surface,
    unsigned char* data,
    short srcx,
    short srcy,
    unsigned short srcw,
    unsigned short srch,
    short destx,
    short desty,
    unsigned short destw,
    unsigned short desth,
    unsigned int flags /* de-interlacing flags */
)
{
    INIT_DRIVER_DATA;
    INIT_OUTPUT_PRIV;
    object_surface_p obj_surface = SURFACE(surface);
    int offset = 0;
    psb_surface_p psb_surface;

    obj_surface = SURFACE(surface);
    psb_surface = obj_surface->psb_surface;

    //    psb_surface->buf.drm_buf;
    //    psb_surface->buf.pl_flags;
    psb_putsurface_textureblit(ctx, data, surface, srcx, srcy, srcw, srch,
                               destx, desty, destw, desth, 0, /* no subtitle */
                               obj_surface->width, obj_surface->height,
                               psb_surface->stride, psb_surface->buf.drm_buf,
                               psb_surface->buf.pl_flags, 1 /* need wrap dst */);

    psb_android_postBuffer(offset);

    return VA_STATUS_SUCCESS;
}

VAStatus psb_putsurface_coverlay(
    VADriverContextP ctx,
    VASurfaceID surface,
    short srcx,
    short srcy,
    unsigned short srcw,
    unsigned short srch,
    short destx, /* screen cooridination */
    short desty,
    unsigned short destw,
    unsigned short desth,
    unsigned int flags /* de-interlacing flags */
)
{
    INIT_OUTPUT_PRIV;
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    /* USE_FIT_SCR_SIZE */
    /* calculate fit screen size of frame */
    unsigned short _scr_x = output->screen_width;
    unsigned short _scr_y = output->screen_height;
    float _slope_xy = (float)srch / srcw;
    unsigned short _destw = (short)(_scr_y / _slope_xy);
    unsigned short _desth = (short)(_scr_x * _slope_xy);
    short _pos_x, _pos_y;

    if (_destw <= _scr_x) {
        _desth = _scr_y;
        _pos_x = (_scr_x - _destw) >> 1;
        _pos_y = 0;
    } else {
        _destw = _scr_x;
        _pos_x = 0;
        _pos_y = (_scr_y - _desth) >> 1;
    }
    destx += _pos_x;
    desty += _pos_y;
    destw = _destw;
    desth = _desth;

    psb__information_message("psb_putsurface_overlay: src (%d, %d, %d, %d), destx (%d, %d, %d, %d).\n",
                             srcx, srcy, srcw, srch, destx, desty, destw, desth);
    /* display by overlay */
    vaStatus = psb_putsurface_overlay(
                   ctx, surface, srcx, srcy, srcw, srch,
                   destx, desty, destw, desth, /* screen coordinate */
                   flags, OVERLAY_A, PIPEA);

    return vaStatus;
}


VAStatus psb_putsurface_ts(
    VADriverContextP ctx,
    VASurfaceID surface,
    unsigned char *android_isurface,
    int buffer_index,
    short srcx,
    short srcy,
    unsigned short srcw,
    unsigned short srch,
    short destx,
    short desty,
    unsigned short destw,
    unsigned short desth,
    VARectangle *cliprects, /* client supplied clip list */
    unsigned int number_cliprects, /* number of clip rects in the clip list */
    unsigned int flags /* de-interlacing flags */
)
{
    INIT_DRIVER_DATA;
    INIT_OUTPUT_PRIV;
    object_surface_p obj_surface = SURFACE(surface);

    if (driver_data->overlay_idle_frame == 0) {
        psb_android_texture_streaming_resetParams();
        update_forced = 1;
    }

    /* blend/positioning setting can be called by app directly, or enable VA_ENABLE_BLEND flag to let driver call */
    if (flags & VA_ENABLE_BLEND)
        psb_android_texture_streaming_set_blend(destx, desty, destw, desth,
                                                driver_data->clear_color,
                                                driver_data->blend_color,
                                                driver_data->blend_mode);
    /*cropping can be also used for dynamic resolution change feature, only high to low resolution*/
    /*by default, srcw and srch is set to video width and height*/
    if ((0 == srcw) || (0 == srch)) {
        srcw = obj_surface->width;
        srch = obj_surface->height_origin;
    }
    psb_android_texture_streaming_set_texture_dim(srcw, srch);
    if (driver_data->va_rotate)
        psb_android_texture_streaming_set_rotate(va2hw_rotation(driver_data->va_rotate));

#if 0
    /* use cliprect for crop */
    if (cliprects && (number_cliprects == 1))
        psb_android_texture_streaming_set_crop(cliprects->x, cliprects->y, cliprects->width, cliprects->height);
#endif

    psb_android_texture_streaming_display(buffer_index);

    driver_data->overlay_idle_frame++;
    update_forced = 0;

    /* current surface is being displayed */
    if (driver_data->cur_displaying_surface != VA_INVALID_SURFACE)
        driver_data->last_displaying_surface = driver_data->cur_displaying_surface;

    obj_surface->display_timestamp = GetTickCount();
    driver_data->cur_displaying_surface = surface;

    return VA_STATUS_SUCCESS;
}


static int psb_update_destbox(
    VADriverContextP ctx
)
{
    INIT_DRIVER_DATA;
    INIT_OUTPUT_PRIV;
    short destx;
    short desty;
    unsigned short destw;
    unsigned short desth;
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    psb_android_get_destbox(&destx, &desty, &destw, &desth);
    /*psb__information_message("destbox = (%d,%d,%d,%d)\n", destx, desty, destw, desth);*/
    if ((destx >= 0) && (desty >= 0) &&
        ((destx + destw) <= output->screen_width) &&
        ((desty + desth) <= output->screen_height) &&
        (output->destx != destx ||
         output->desty != desty ||
         output->destw != destw ||
         output->desth != desth)) {
        output->destx = destx;
        output->desty = desty;
        output->destw = destw;
        output->desth = desth;
        output->new_destbox = 1;

        LOGD("==========New Destbox=============\n");
        LOGD("output->destbox = (%d,%d,%d,%d)\n", output->destx, output->desty, output->destw, output->desth);
    }

    return vaStatus;
}

static int psb_check_outputmethod(
    VADriverContextP ctx,
    VASurfaceID surface,
    unsigned short srcw,
    unsigned short srch,
    void *android_isurface,
    psb_hdmi_mode *hdmi_mode
)
{
    INIT_DRIVER_DATA;
    INIT_OUTPUT_PRIV;
    psb_HDMIExt_info_p psb_HDMIExt_info = (psb_HDMIExt_info_p)output->psb_HDMIExt_info;
    object_surface_p obj_surface;
    int rotation = 0, widi = 0;
    int delta_rotation = 0;
    int srf_rotate; /* primary surface rotation */
    psb_surface_p rotate_surface; /* rotate surface */
    int rotate_srf_rotate = -1; /* degree of the rotate surface */

    if ((srcw >= 2048) || (srch >= 2048)) {
        psb__information_message("Clip size extend overlay hw limit, use texstreaming\n");
        driver_data->output_method = PSB_PUTSURFACE_TEXSTREAMING;
        return 0;
    }

    /* use saved status to avoid per-frame checking */
    if ((driver_data->frame_count % driver_data->outputmethod_checkinterval) != 0) {
        *hdmi_mode = psb_HDMIExt_get_mode(output);
        return 0;
    }

    /* check the status at outputmethod_checkinterval frequency */
    /* at first check HDMI status */
    if (psb_HDMIExt_update(ctx, psb_HDMIExt_info)) {
        psb__error_message("%s: Failed to update HDMIExt info.\n", __FUNCTION__);
        return -1;
    }

    *hdmi_mode = psb_HDMIExt_get_mode(output);
    if ((*hdmi_mode == EXTENDED_VIDEO) || (*hdmi_mode == CLONE)) {
        unsigned short _destw, _desth;
        short _pos_x, _pos_y;
        unsigned short crtc_width = 0, crtc_height = 0;
        float _slope_xy;

        /* need to handle VA rotation, and set WM rotate to 0
         * for Android, MIPI0/HDMI has the same WM rotation always
         */
        if (driver_data->mipi0_rotation != 0) {
            driver_data->mipi0_rotation = 0;
            driver_data->hdmi_rotation = 0;
            output->new_destbox = 1;
            psb_RecalcRotate(ctx);
        }

        psb_HDMIExt_get_prop(output, &crtc_width, &crtc_height);

        /*recalculate the render box to fit the ratio of height/width*/
        if ((driver_data->extend_rotation == VA_ROTATION_90) ||
            (driver_data->extend_rotation == VA_ROTATION_270))
            _slope_xy = (float)srcw / srch;
        else
            _slope_xy = (float)srch / srcw;

        _destw = (short)(crtc_height / _slope_xy);
        _desth = (short)(crtc_width * _slope_xy);
        if (_destw <= crtc_width) {
            _desth = crtc_height;
            _pos_x = (crtc_width - _destw) >> 1;
            _pos_y = 0;
        } else {
            _destw = crtc_width;
            _pos_x = 0;
            _pos_y = (crtc_height - _desth) >> 1;
        }
        driver_data->render_rect.x = _pos_x;
        driver_data->render_rect.y = _pos_y;
        driver_data->render_rect.width = _destw;
        driver_data->render_rect.height = _desth;
        psb__information_message("HDMI mode is on (%d), Render Rect: (%d,%d,%d,%d)\n",
                                 *hdmi_mode,
                                 driver_data->render_rect.x, driver_data->render_rect.y,
                                 driver_data->render_rect.width, driver_data->render_rect.height);
        return 0;
    }

    /*Update output destbox using layerbuffer's visible region*/
    psb_update_destbox(ctx);

    if ((driver_data->output_method == PSB_PUTSURFACE_FORCE_COVERLAY)
        || (driver_data->output_method == PSB_PUTSURFACE_FORCE_TEXSTREAMING))
        return 0;

    /*If overlay can not get correct destbox, use texstreaming.*/
    if (output->destw == 0 || output->desth == 0 ||
        ((output->destw == srcw) && (output->desth == srch))) {
        psb__information_message("No proper destbox, use texstreaming (%dx%d+%d+%d)\n",
                                 output->destw, output->desth, output->destx, output->desty);
        driver_data->output_method = PSB_PUTSURFACE_TEXSTREAMING;
        return 0;
    }

    /* HDMI is not enabled */
    psb_android_surfaceflinger_status(android_isurface, &output->sf_composition, &rotation, &widi);
    if (widi == eWidiClone) {
        psb__information_message("WIDI in clone mode, use texstreaming\n");
        driver_data->output_method = PSB_PUTSURFACE_TEXSTREAMING;
        driver_data->msvdx_rotate_want = 0;/* disable msvdx rotae */

        return 0;
    }
    if (widi == eWidiExtendedVideo) {
        psb__information_message("WIDI in extend video mode, disable local displaying\n");
        driver_data->output_method = PSB_PUTSURFACE_NONE;
        driver_data->msvdx_rotate_want = 0;/* disable msvdx rotae */

        return 0;
    }

    /* only care local rotation */
    delta_rotation = Rotation2Angle(driver_data->mipi0_rotation) - Rotation2Angle(rotation);
    if ((((abs(delta_rotation) == 90) || (abs(delta_rotation) == 270)) && output->new_destbox) ||
        (abs(delta_rotation) == 180)) {
        psb__information_message("New rotation degree %d of MIPI0 WM, Recalc rotation\n", rotation);
        driver_data->mipi0_rotation = rotation;
        driver_data->hdmi_rotation = rotation;

        psb_RecalcRotate(ctx);
    }
    output->new_destbox = 0;

    obj_surface = SURFACE(surface);
    if (GET_SURFACE_INFO_protect(obj_surface->psb_surface)) {
        psb__information_message("Protected surface, use overlay\n");
        driver_data->output_method = PSB_PUTSURFACE_COVERLAY;

        return 0;
    }

    if (output->sf_composition) {
        psb__information_message("Composition is detected, use texstreaming\n");
        driver_data->output_method = PSB_PUTSURFACE_TEXSTREAMING;
        return 0;
    }

    srf_rotate = GET_SURFACE_INFO_rotate(obj_surface->psb_surface);
    rotate_surface = obj_surface->psb_surface_rotate;
    if (rotate_surface != NULL)
        rotate_srf_rotate = GET_SURFACE_INFO_rotate(rotate_surface);

    psb__information_message("SF rotation %d, VA rotation %d, final MSVDX rotation %d\n",
                             rotation, driver_data->va_rotate, driver_data->local_rotation);
    psb__information_message("Primary surface rotation %d, rotated surface rotation %d\n",
                             srf_rotate, rotate_srf_rotate);

    /* The surface rotation is not same with the final rotation */
    if ((driver_data->local_rotation != 0) &&
        ((srf_rotate != driver_data->local_rotation) || (rotate_srf_rotate != driver_data->local_rotation))) {
        psb__information_message("Use texstreaming due to different VA surface rotation and final rotaion\n",
                                 srf_rotate, rotate_srf_rotate);
        driver_data->output_method = PSB_PUTSURFACE_TEXSTREAMING;
        return 0;
    }

    driver_data->output_method = PSB_PUTSURFACE_COVERLAY;

    return 0;
}

VAStatus psb_PutSurface(
    VADriverContextP ctx,
    VASurfaceID surface,
    void *android_isurface,
    short srcx,
    short srcy,
    unsigned short srcw,
    unsigned short srch,
    short destx,
    short desty,
    unsigned short destw,
    unsigned short desth,
    VARectangle *cliprects, /* client supplied clip list */
    unsigned int number_cliprects, /* number of clip rects in the clip list */
    unsigned int flags /* de-interlacing flags */
)
{
    INIT_DRIVER_DATA;
    INIT_OUTPUT_PRIV;
    object_surface_p obj_surface;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    PsbPortPrivPtr pPriv = (PsbPortPrivPtr)(&driver_data->coverlay_priv);
    psb_hdmi_mode hdmi_mode = OFF;
    int sf_composition = 0, buffer_index, i = 0;
    int ret = 0;

    obj_surface = SURFACE(surface);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if ((NULL == cliprects) && (0 != number_cliprects)) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if ((srcx < 0) || (srcx > obj_surface->width) || (srcw > (obj_surface->width - srcx)) ||
        (srcy < 0) || (srcy > obj_surface->height_origin) || (srch > (obj_surface->height_origin - srcy))) {
        psb__error_message("vaPutSurface: source rectangle passed from upper layer is not correct.\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }
    if ((destx < 0) || (desty < 0)) {
        psb__error_message("vaPutSurface: dest rectangle passed from upper layer is not correct.\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }

    if (driver_data->dummy_putsurface) {
        psb__information_message("vaPutSurface: dummy mode, return directly\n");
        return VA_STATUS_SUCCESS;
    }

    /* init overlay */
    if (!driver_data->coverlay_init &&
        (driver_data->output_method != PSB_PUTSURFACE_FORCE_TEXSTREAMING)) {
        ret = psb_coverlay_init(ctx);
        if (ret != 0) {
            psb__information_message("vaPutSurface: psb_coverlay_init failed. Fallback to texture streaming.\n");
            driver_data->output_method = PSB_PUTSURFACE_FORCE_TEXSTREAMING;
            driver_data->coverlay_init = 0;
        } else
            driver_data->coverlay_init = 1;
    }

    /* set the current displaying video frame into kernel */
    psb_surface_set_displaying(driver_data, obj_surface->width,
                               obj_surface->height_origin,
                               obj_surface->psb_surface);

    /* get the BCD index of current surface */
    buffer_index = psb_get_video_bcd(ctx, surface);
    if (buffer_index == -1)
        psb__error_message("The surface is not registered in BCD, shoud use overlay\n");

    /* exit MRST path at first */
    if (IS_MRST(driver_data)) {
        if (driver_data->output_method == PSB_PUTSURFACE_FORCE_COVERLAY) { /* overlay is for testing, not POR */
            psb__information_message("Force overlay to display\n");
            vaStatus = psb_putsurface_coverlay(ctx, surface,
                                               srcx, srcy, srcw, srch,
                                               destx, desty, destw, desth,
                                               flags);
        } else {
            psb__information_message("Use texstreaming to display.\n");
            vaStatus = psb_putsurface_ts(ctx, surface, android_isurface, buffer_index,
                                         srcx, srcy, srcw, srch,
                                         destx, desty, destw, desth,
                                         cliprects, number_cliprects, /* number of clip rects in the clip list */
                                         flags);
        }

        return vaStatus;
    }

    if (psb_android_register_isurface(android_isurface, driver_data->bcd_id, srcw, srch)) {
        psb__error_message("In psb_PutSurface, android_isurface is not a valid isurface object.\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }
    driver_data->ts_source_created = 1;

    /* time for MFLD platform */
    psb_check_outputmethod(ctx, surface, srcw, srch, android_isurface, &hdmi_mode);
    if (driver_data->output_method == PSB_PUTSURFACE_NONE)
        return VA_STATUS_SUCCESS;

    if (hdmi_mode == UNDEFINED) {
        psb__information_message("HDMI: Undefined mode, drop the frame.\n");
        return vaStatus;
    }

    /* Extvideo: Use overlay to render external HDMI display */
    if (hdmi_mode == EXTENDED_VIDEO) {
        psb__information_message("HDMI: ExtVideo mode enabled, use overlay to render external HDMI display.\n");
        /*we also need to clear local display if colorkey dirty.*/
        if (driver_data->overlay_idle_frame != 0) {
            psb_android_texture_streaming_set_background_color(driver_data->color_key | 0xff000000);
            psb_android_texture_streaming_display(buffer_index);
            driver_data->overlay_idle_frame = 0;
        }
        vaStatus = psb_putsurface_overlay(ctx, surface,
                                          srcx, srcy, srcw, srch,
                                          driver_data->render_rect.x, driver_data->render_rect.y,
                                          driver_data->render_rect.width, driver_data->render_rect.height,
                                          flags, OVERLAY_A, PIPEB);

        return vaStatus;
    }

    /* Clone mode: Use TS to render both MIPI and HDMI display */
    if (hdmi_mode == CLONE) {
        psb__information_message("HDMI: Clone mode enabled, use texsteaming for both devices\n");
        vaStatus = psb_putsurface_ts(ctx, surface, android_isurface, buffer_index,
                                     srcx, srcy, srcw, srch,
                                     destx, desty, destw, desth,
                                     cliprects, number_cliprects, /* number of clip rects in the clip list */
                                     flags);
        return vaStatus;
    }

    /* local video playback */
    if ((driver_data->output_method == PSB_PUTSURFACE_TEXSTREAMING) ||
        (driver_data->output_method == PSB_PUTSURFACE_FORCE_TEXSTREAMING)) {
        psb__information_message("MIPI: Use texstreaming to display.\n");

        vaStatus = psb_putsurface_ts(ctx, surface, android_isurface, buffer_index,
                                     srcx, srcy, srcw, srch,
                                     destx, desty, destw, desth,
                                     cliprects, number_cliprects, /* number of clip rects in the clip list */
                                     flags);
    } else {
        psb__information_message("MIPI: Use overlay to display.\n");

        /*initialize output destbox using default destbox if it has not been initialized until here.*/
        if (output->destw == 0 || output->desth == 0) {
            output->destx = (destx > 0) ? destx : 0;
            output->desty = (desty > 0) ? desty : 0;
            output->destw = ((output->destx + destw) > output->screen_width) ? (output->screen_width - output->destx) : destw;
            output->desth = ((output->desty + desth) > output->screen_height) ? (output->screen_height - output->desty) : desth;
        }

        /* Hack for repaint color key to black(0,0,0). */
        if (driver_data->overlay_idle_frame != 0) {
            psb__information_message("Paint color key to 0x%x\n", driver_data->color_key);
            psb_android_texture_streaming_set_background_color(driver_data->color_key | 0xff000000);
            psb_android_texture_streaming_display(buffer_index);
            driver_data->overlay_idle_frame = 0;
        }

        psb__information_message("Overlay position = (%d,%d,%d,%d)\n", output->destx, output->desty, output->destw, output->desth);
        vaStatus = psb_putsurface_overlay(ctx, surface,
                                          srcx, srcy, srcw, srch,
                                          output->destx, output->desty, output->destw, output->desth,
                                          flags, OVERLAY_A, PIPEA);
    }

    if (driver_data->overlay_idle_frame == MAX_OVERLAY_IDLE_FRAME)
        psb_coverlay_stop(ctx);

    driver_data->frame_count++;


    return vaStatus;
}
