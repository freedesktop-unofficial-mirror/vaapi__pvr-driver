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
 *    Zeng Li <zeng.li@intel.com>
 *    Jason Hu <jason.hu@intel.com>
 *    Shengquan Yuan  <shengquan.yuan@intel.com>
 */

#include <va/va.h>
#include <va/va_backend.h>
#include <va/va_backend_tpi.h>
#include <va/va_backend_egl.h>
#include <va/va_dricommon.h>

#include "psb_drv_video.h"
#include "psb_output.h"
#include "vc1_defs.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <wsbm/wsbm_pool.h>
#include <wsbm/wsbm_manager.h>
#include <wsbm/wsbm_util.h>
#include <wsbm/wsbm_fencemgr.h>

#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData

#define SET_SURFACE_INFO_rotate(psb_surface, rotate) psb_surface->extra_info[5] = (uint32_t) rotate;
#define GET_SURFACE_INFO_rotate(psb_surface) ((int) psb_surface->extra_info[5])
#define CHECK_SURFACE_REALLOC(psb_surface, msvdx_rotate, need)  \
do {                                                            \
    int old_rotate = GET_SURFACE_INFO_rotate(psb_surface);      \
    switch (msvdx_rotate) {                                     \
    case 2: /* 180 */                                           \
        if (old_rotate == 180)                                  \
            need = 0;                                           \
        else                                                    \
            need = 1;                                           \
        break;                                                  \
    case 1: /* 90 */                                            \
    case 3: /* 270 */                                           \
        if (old_rotate == 1 || old_rotate == 3)                 \
            need = 0;                                           \
        else                                                    \
            need = 1;                                           \
        break;                                                  \
    }                                                           \
} while (0)

void psb_InitRotate(VADriverContextP ctx)
{
    char env_value[64];
    INIT_DRIVER_DATA;

    /* VA rotate from APP */
    driver_data->va_rotate = VA_ROTATION_NONE;

    /* window manager rotation from OS */
    driver_data->mipi0_rotation = VA_ROTATION_NONE;
    driver_data->mipi1_rotation = VA_ROTATION_NONE;
    driver_data->hdmi_rotation = VA_ROTATION_NONE;

    /* final rotation of VA rotate+WM rotate */
    driver_data->local_rotation = VA_ROTATION_NONE;
    driver_data->extend_rotation = VA_ROTATION_NONE;

    /* MSVDX rotate */
    driver_data->msvdx_rotate_want = ROTATE_VA2MSVDX(VA_ROTATION_NONE);

    if (psb_parse_config("PSB_VIDEO_NOROTATE", &env_value[0]) == 0) {
        psb__information_message("MSVDX: disable MSVDX rotation\n");
        driver_data->disable_msvdx_rotate = 1;
    }
}

void psb_RecalcRotate(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    int angle, new_rotate;
    int old_rotate = driver_data->msvdx_rotate_want;

    /* calc VA rotation and WM rotation, and assign to the final rotation degree */
    angle = Rotation2Angle(driver_data->va_rotate) + Rotation2Angle(driver_data->mipi0_rotation);
    driver_data->local_rotation = Angle2Rotation(angle);
    angle = Rotation2Angle(driver_data->va_rotate) + Rotation2Angle(driver_data->hdmi_rotation);
    driver_data->extend_rotation = Angle2Rotation(angle);

    /* for any case that local and extened rotation are not same, fallback to GPU */
    if ((driver_data->mipi1_rotation != VA_ROTATION_NONE) ||
        ((driver_data->local_rotation != VA_ROTATION_NONE) &&
         (driver_data->extend_rotation != VA_ROTATION_NONE) &&
         (driver_data->local_rotation != driver_data->extend_rotation))) {
        new_rotate = ROTATE_VA2MSVDX(driver_data->local_rotation);
        if (driver_data->is_android == 0) /*fallback to texblit path*/
            driver_data->output_method = PSB_PUTSURFACE_CTEXTURE;
    } else {
        if (driver_data->local_rotation == VA_ROTATION_NONE)
            new_rotate = driver_data->extend_rotation;
        else
            new_rotate = driver_data->local_rotation;

        if (driver_data->is_android == 0) {
            if (driver_data->output_method != PSB_PUTSURFACE_FORCE_CTEXTURE)
                driver_data->output_method = PSB_PUTSURFACE_COVERLAY;
        }
    }

    if (old_rotate != new_rotate) {
        psb__information_message("MSVDX: new rotation %d desired\n", new_rotate);
        driver_data->msvdx_rotate_want = new_rotate;
    }

}


void psb_CheckInterlaceRotate(object_context_p obj_context, unsigned char *pic_param_tmp)
{
    int interaced_stream;

    switch (obj_context->profile) {
    case VAProfileMPEG2Simple:
    case VAProfileMPEG2Main:
        break;
    case VAProfileMPEG4Simple:
    case VAProfileMPEG4AdvancedSimple:
    case VAProfileMPEG4Main:
    case VAProfileH263Baseline: {
        VAPictureParameterBufferMPEG4 *pic_params = (VAPictureParameterBufferMPEG4 *)pic_param_tmp;

        if (pic_params->vol_fields.bits.interlaced)
            obj_context->interlaced_stream = 1; /* is it the right way to check? */
        break;
    }
    case VAProfileH264Baseline:
    case VAProfileH264Main:
    case VAProfileH264High:
    case VAProfileH264ConstrainedBaseline: {
        VAPictureParameterBufferH264 *pic_params = (VAPictureParameterBufferH264 *)pic_param_tmp;
        /* is it the right way to check? */
        if (pic_params->pic_fields.bits.field_pic_flag || pic_params->seq_fields.bits.mb_adaptive_frame_field_flag)
            obj_context->interlaced_stream = 1;

        break;
    }
    case VAProfileVC1Simple:
    case VAProfileVC1Main:
    case VAProfileVC1Advanced: {
        VAPictureParameterBufferVC1 *pic_params = (VAPictureParameterBufferVC1 *)pic_param_tmp;

        /* is it the right way to check? */
        if (pic_params->sequence_fields.bits.interlace && (pic_params->picture_fields.bits.frame_coding_mode == VC1_FCM_FLDI))
            obj_context->interlaced_stream = 1;

        break;
    }
    default:
        break;
    }

    if (obj_context->interlaced_stream) {
        object_surface_p obj_surface = obj_context->current_render_target;

        psb__information_message("Intelaced stream, no MSVDX rotate\n");

        SET_SURFACE_INFO_rotate(obj_surface->psb_surface, 0);
        obj_context->msvdx_rotate = 0;
    }
}


/*
 * Detach a surface from obj_surface
 */
VAStatus psb_DestroyRotateSurface(
    VADriverContextP ctx,
    object_surface_p obj_surface,
    int rotate
)
{
    INIT_DRIVER_DATA;
    psb_surface_p psb_surface = obj_surface->psb_surface_rotate;
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    /* Allocate alternative output surface */
    if (psb_surface) {
        psb__information_message("Try to allocate surface for alternative rotate output\n");
        psb_surface_destroy(obj_surface->psb_surface_rotate);
        free(psb_surface);

        obj_surface->psb_surface_rotate = NULL;
        obj_surface->width_r = obj_surface->width;
        obj_surface->height_r = obj_surface->height;
    }

    return vaStatus;
}

/*
 * Create and attach a rotate surface to obj_surface
 */
VAStatus psb_CreateRotateSurface(
    VADriverContextP ctx,
    object_surface_p obj_surface,
    int msvdx_rotate
)
{
    int width, height;
    psb_surface_p psb_surface;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int need_realloc = 0, protected = 0;

    INIT_DRIVER_DATA;

    psb_surface = obj_surface->psb_surface_rotate;
    if (psb_surface) {
        CHECK_SURFACE_REALLOC(psb_surface, msvdx_rotate, need_realloc);
        if (need_realloc == 0) {
            SET_SURFACE_INFO_rotate(psb_surface, msvdx_rotate);
            return VA_STATUS_SUCCESS;
        } else { /* free the old rotate surface */
            /*FIX ME: No sync mechanism to hold surface buffer b/w msvdx and display(overlay).
            So Disable dynamic surface destroy/create for avoiding buffer corruption.
            psb_surface_destroy(obj_surface->psb_surface_rotate);
            memset(psb_surface, 0, sizeof(*psb_surface));*/
            return VA_STATUS_SUCCESS;
        }
    } else
        psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));

    psb__information_message("Try to allocate surface for alternative rotate output\n");

    width = obj_surface->width;
    height = obj_surface->height;

    if (msvdx_rotate == 2 /* VA_ROTATION_180 */) {
        vaStatus = psb_surface_create(driver_data, width, height, VA_FOURCC_NV12,
                                      protected, psb_surface);
        obj_surface->width_r = width;
        obj_surface->height_r = height;
    } else {
        vaStatus = psb_surface_create(driver_data, obj_surface->height_origin, ((width + 0x1f) & ~0x1f), VA_FOURCC_NV12,
                                      protected, psb_surface);
        obj_surface->width_r = obj_surface->height_origin;
        obj_surface->height_r = ((width + 0x1f) & ~0x1f);
    }

    if (VA_STATUS_SUCCESS != vaStatus) {
        free(psb_surface);
        obj_surface->psb_surface_rotate = NULL;
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
        return vaStatus;
    }

    SET_SURFACE_INFO_rotate(psb_surface, msvdx_rotate);
    obj_surface->psb_surface_rotate = psb_surface;

    return vaStatus;
}


