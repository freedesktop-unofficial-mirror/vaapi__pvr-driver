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
#include <cutils/log.h>
#include "psb_android_glue.h"
#include "psb_texstreaming.h"
#include <wsbm/wsbm_manager.h>

#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData;
#define INIT_OUTPUT_PRIV    psb_android_output_p output = (psb_android_output_p)(((psb_driver_data_p)ctx->pDriverData)->ws_priv)

#define SURFACE(id) ((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))
#define IMAGE(id)  ((object_image_p) object_heap_lookup( &driver_data->image_heap, id ))
#define SUBPIC(id)  ((object_subpic_p) object_heap_lookup( &driver_data->subpic_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))


typedef struct _psb_android_output_s {
    /* information of output display */
    unsigned short screen_width;
    unsigned short screen_height;

    /* for memory heap base used by putsurface */
    unsigned char* heap_addr;
} psb_android_output_s, *psb_android_output_p;


void *psb_android_output_init(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    char put_surface[1024];
    struct drm_psb_register_rw_arg regs;
    psb_android_output_p output = calloc(1, sizeof(psb_android_output_s));

    struct fb_var_screeninfo vinfo = {0};
    int fbfd = -1;
    int ret;

    if (output == NULL) {
        psb__error_message("Can't malloc memory\n");
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    /* Guess the screen size */
    output->screen_width = 800;
    output->screen_height = 480;

    // Open the frame buffer for reading
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
    driver_data->color_key = 0x0; /*black*/

    /* Init CTEXTURE for vaPutSurfaceBuf */
    driver_data->ctexture = 1;

    if (psb_parse_config("PSB_VIDEO_COVERLAY", &put_surface[0]) == 0) {
        psb__information_message("Putsurface use client overlay\n");
        driver_data->output_method = PSB_PUTSURFACE_FORCE_COVERLAY;
    }


    if (getenv("PSB_VIDEO_COVERLAY")) {
        psb__information_message("Putsurface use client overlay\n");
        driver_data->output_method = PSB_PUTSURFACE_FORCE_COVERLAY;
    }
    /*Alway init coverlay for MDFLD*/
    if (IS_MFLD(driver_data))
        driver_data->coverlay = 1;
    /*set PIPEB(HDMI)source format as RGBA*/
    memset(&regs, 0, sizeof(regs));
    regs.subpicture_enable_mask = REGRWBITS_DSPBCNTR;
    drmCommandWriteRead(driver_data->drm_fd, DRM_PSB_REGISTER_RW, &regs, sizeof(regs));

    return output;
}

VAStatus psb_android_output_deinit(VADriverContextP ctx)
{
    INIT_OUTPUT_PRIV;
    //psb_android_output_p output = GET_OUTPUT_DATA(ctx);

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

#if 0
    printf("pl_flags %x\n", psb_surface->buf.pl_flags);

    printf("FIXME: not sure how Android app handle rotation?\n"
           "need to revise width & height here?\n");

    printf("FIXME: need to prepare a rotation/RAR surface here?\n");

    printf("FIXME: camera preview surface is different, all is \n"
           "just one buffer, so a pre_add is needed\n");
    psb__error_message("srcx %d, srcy %d, srcw %d, srch %d, destx %d, desty %d, destw %d,\n"
                       "desth %d, width %d height %d, stride %d drm_buf %x\n",
                       srcx, srcy, srcw, srch, destx, desty, destw, desth, obj_surface->width,
                       obj_surface->height, psb_surface->stride, psb_surface->buf.drm_buf);
#endif
    psb_putsurface_textureblit(ctx, data, surface, srcx, srcy, srcw, srch, destx, desty, destw, desth,
                               obj_surface->width, obj_surface->height,
                               psb_surface->stride, psb_surface->buf.drm_buf,
                               psb_surface->buf.pl_flags);

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

    /* display by overlay */
    vaStatus = psb_putsurface_overlay(
                   ctx, surface, srcx, srcy, srcw, srch,
                   destx, desty, destw, desth, /* screen coordinate */
                   flags, OVERLAY_A, PIPEA);

    return vaStatus;
}

VAStatus psb_PutSurfaceBuf(
    VADriverContextP ctx,
    VASurfaceID surface,
    unsigned char* data,
    int* data_len,
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
    int offset = 0;
    psb_surface_p psb_surface;

    obj_surface = SURFACE(surface);
    psb_surface = obj_surface->psb_surface;

    psb_putsurface_textureblit(ctx, data, surface, srcx, srcy, srcw, srch, destx, desty, destw, desth,
                               obj_surface->width, obj_surface->height,
                               psb_surface->stride, psb_surface->buf.drm_buf,
                               psb_surface->buf.pl_flags);

    return VA_STATUS_SUCCESS;
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

    obj_surface = SURFACE(surface);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (driver_data->dummy_putsurface) {
        LOGD("vaPutSurface: dummy mode, return directly\n");
        return VA_STATUS_SUCCESS;
    }

    if (driver_data->render_device == VA_RENDER_DEVICE_EXTERNAL) {
        /*Use overlay to render external HDMI display*/
        vaStatus = psb_putsurface_overlay(ctx, surface,
                                          srcx, srcy, srcw, srch,
                                          driver_data->render_rect.x, driver_data->render_rect.y,
                                          driver_data->render_rect.width, driver_data->render_rect.height,
                                          flags, OVERLAY_A, PIPEB);
    } else if ((driver_data->render_mode & VA_RENDER_MODE_LOCAL_OVERLAY) ||
               (driver_data->output_method == PSB_PUTSURFACE_FORCE_COVERLAY) ||
               (driver_data->output_method == PSB_PUTSURFACE_COVERLAY)) {
        LOGD("In psb_PutSurface, use overlay to display video.\n");
        LOGD("srcx is %d, srcy is %d, srcw is %d, srch is %d, destx is %d, desty is %d, destw is %d, desth is %d.\n", \
             srcx, srcy, srcw, srch, destx, desty, destw, desth);
        /*Use overlay to render local display*/
        if (destw > output->screen_width)
            destw = output->screen_width;
        if (desth > output->screen_height)
            desth = output->screen_height;
        vaStatus = psb_putsurface_overlay(ctx, surface,
                                          srcx, srcy, srcw, srch,
                                          destx, desty, destw, desth,
                                          flags, OVERLAY_A, PIPEA);
        /*Use overlay to render external HDMI display*/
        if (driver_data->render_device & VA_RENDER_DEVICE_EXTERNAL) {
            vaStatus = psb_putsurface_overlay(ctx, surface,
                                              srcx, srcy, srcw, srch,
                                              driver_data->render_rect.x, driver_data->render_rect.y,
                                              driver_data->render_rect.width, driver_data->render_rect.height,
                                              flags, OVERLAY_C, PIPEB);
        }
    } else {
        LOGD("In psb_PutSurface, use texture streaming to display video.\n");
        LOGD("In psb_PutSurface, call psb_android_register_isurface to create texture streaming source, srcw is %d, srch is %d.\n", srcw, srch);
        if (psb_android_register_isurface(android_isurface, driver_data->bcd_id, srcw, srch)) {
            LOGE("In psb_PutSurface, android_isurface is not a valid isurface object.\n");
            return VA_STATUS_ERROR_UNKNOWN;
        }
        /*blend/positioning setting can be called by app directly, or enable VA_ENABLE_BLEND flag to let driver call*/
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
        psb_android_texture_streaming_set_crop(srcx, srcy, srcw, srch);

        BC_Video_ioctl_package ioctl_package;
        psb_surface_p psb_surface;
        psb_surface = obj_surface->psb_surface;
        ioctl_package.ioctl_cmd = BC_Video_ioctl_get_buffer_index;
        ioctl_package.device_id = driver_data->bcd_id;
        ioctl_package.inputparam = (int)(wsbmKBufHandle(wsbmKBuf(psb_surface->buf.drm_buf)));

        if (drmCommandWriteRead(driver_data->drm_fd, driver_data->bcd_ioctrl_num, &ioctl_package, sizeof(ioctl_package)) != 0) {
            psb__error_message("Failed to get buffer index from buffer class video driver (errno=%d).\n", errno);
            return VA_STATUS_ERROR_UNKNOWN;
        }
        LOGD("buffer handle is %d and buffer index is %d.\n", ioctl_package.inputparam, ioctl_package.outputparam);
        psb_android_texture_streaming_display(ioctl_package.outputparam);

        /*Use overlay to render external HDMI display*/
        if (driver_data->render_device & VA_RENDER_DEVICE_EXTERNAL) {
            vaStatus = psb_putsurface_overlay(ctx, surface,
                                              srcx, srcy, srcw, srch,
                                              driver_data->render_rect.x, driver_data->render_rect.y,
                                              driver_data->render_rect.width, driver_data->render_rect.height,
                                              flags, OVERLAY_A, PIPEB);
        }
    }

    return vaStatus;
}
