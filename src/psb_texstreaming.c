/*
 * Copyright (c) 2011 Intel Corporation. All Rights Reserved.
 * Copyright (c) Imagination Technologies Limited, UK
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
 *    Fei Jiang <fei.jiang@intel.com>
 */

#include <va/va.h>
#include <va/va_backend.h>
#include <wsbm/wsbm_manager.h>

#ifdef ANDROID
#include "android/psb_android_glue.h"
#endif

#include "psb_drv_video.h"
#include "psb_surface.h"
#include "psb_texstreaming.h"

#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData;
#define SURFACE(id) ((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))

int psb_register_video_bcd(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    int width, height, stride, num_surfaces;
    VASurfaceID *surface_list;
    BC_Video_ioctl_package ioctl_package;
    bc_buf_params_t buf_param;
    bc_buf_ptr_t buf_pa;
    int ret, i;

    width = driver_data->bcd_buffer_width;
    height = driver_data->bcd_buffer_height;
    stride = driver_data->bcd_buffer_stride;
    num_surfaces = driver_data->bcd_buffer_num;
    surface_list = driver_data->bcd_buffer_surfaces;

    buf_param.count = num_surfaces;
    buf_param.width = width;
    buf_param.stride = stride;
    buf_param.height = height;
    buf_param.fourcc = BC_PIX_FMT_NV12;
    buf_param.type = BC_MEMORY_USERPTR;
    driver_data->bcd_ioctrl_num = driver_data->getParamIoctlOffset + 1;
    psb__information_message("BCD: request buffers in BCD driver\n");
    psb__information_message("BCD: %d buffers, %dx%d, stride %d\n", num_surfaces, width, stride, height);
    ioctl_package.ioctl_cmd = BC_Video_ioctl_request_buffers;
    ioctl_package.inputparam = (int)(&buf_param);
    if (drmCommandWriteRead(driver_data->drm_fd,
                            driver_data->bcd_ioctrl_num,
                            &ioctl_package,
                            sizeof(ioctl_package)) != 0) {
        psb__error_message("BCD: failed to request buffers\n");
        return -1;
    }
    driver_data->bcd_id = ioctl_package.outputparam;
    driver_data->bcd_registered = 1;
    psb__information_message("BCD: allocated bc device id is %d\n", driver_data->bcd_id);
    psb__information_message("BCD: try to get buffer count\n");
    ioctl_package.ioctl_cmd = BC_Video_ioctl_get_buffer_count;
    ioctl_package.device_id = driver_data->bcd_id;
    if (drmCommandWriteRead(driver_data->drm_fd,
                            driver_data->bcd_ioctrl_num,
                            &ioctl_package,
                            sizeof(ioctl_package)) != 0) {
        psb__error_message("BCD: failed to get buffer count\n");
        return -1;
    }

    if (ioctl_package.outputparam != num_surfaces) {
        psb__error_message("BCD: buffer count is not correct (%d expected, actual %d).\n",
                           num_surfaces, ioctl_package.outputparam);
        return -1;
    }
    psb__information_message("BCD:set_buffer_phyaddr to bind buffer id with physical address.\n");
    for (i = 0; i < num_surfaces; i++) {
        psb_surface_p psb_surface;
        object_surface_p obj_surface = SURFACE(surface_list[i]);
        psb_surface = obj_surface->psb_surface;
        psb_surface->bc_buffer = (i & 0xffff) | ((driver_data->bcd_id & 0xffff) << 16);

        /*get ttm buffer handle*/
        buf_pa.handle = wsbmKBufHandle(wsbmKBuf(psb_surface->buf.drm_buf));
        buf_pa.index = i;
        ioctl_package.ioctl_cmd = BC_Video_ioctl_set_buffer_phyaddr;
        ioctl_package.device_id = driver_data->bcd_id;
        ioctl_package.inputparam = (int)(&buf_pa);
        /*bind bcd buffer index with ttm buffer handle and set buffer phyaddr in kernel driver*/
        if (drmCommandWriteRead(driver_data->drm_fd,
                                driver_data->bcd_ioctrl_num,
                                &ioctl_package,
                                sizeof(ioctl_package)) != 0) {
            psb__error_message("BCD: failed to set buffer phyaddr\n");
            return -1;
        }
    }
    psb__information_message("BCD: num_surface = %d, bcd_id = %d\n", num_surfaces, driver_data->bcd_id);
    psb__android_message("BCD: register num_surface = %d, %dx%d, bcd_id = %d\n",
                         num_surfaces, width, height, driver_data->bcd_id);

    return 0;
}

int psb_release_video_bcd(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    BC_Video_ioctl_package ioctl_package;

    /*destroyTextureStreamSource can be called by LayerBuffer::unregisterBuffers automatically
     *But h264vld will not that destroyTextureStreamSource, so still keep it here
     *otherwise, will get "Erroneous page count" error.
     */
#ifdef ANDROID
    if (driver_data->ts_source_created) {
        psb__information_message("BCD:destroy texture streaming source.\n");
        psb_android_texture_streaming_destroy();
        driver_data->ts_source_created = 0;
    }
#endif
    psb__information_message("BCD: release video buffer device id.\n");
    ioctl_package.ioctl_cmd = BC_Video_ioctl_release_buffer_device;
    ioctl_package.device_id = driver_data->bcd_id;
    if (drmCommandWriteRead(driver_data->drm_fd,
                            driver_data->bcd_ioctrl_num,
                            &ioctl_package,
                            sizeof(ioctl_package)) != 0) {
        psb__error_message("BCD: failed to release video buffer class device.\n");
        return -1;
    }
    driver_data->bcd_registered = 0;
    driver_data->bcd_buffer_num = 0;
    if (driver_data->bcd_buffer_surfaces) {
        free(driver_data->bcd_buffer_surfaces);
        driver_data->bcd_buffer_surfaces = NULL;
    }

    return 0;
}

/*
 * return the BCD buffer index,
 * or -1 if the surface is not BCD buffer
 */
int psb_get_video_bcd(
    VADriverContextP ctx,
    VASurfaceID surface)
{
    INIT_DRIVER_DATA;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    VASurfaceID *surface_list;
    int ret = 0;
    unsigned int i;

    if (driver_data->bcd_registered == 0) {
        ret = psb_register_video_bcd(ctx);
        if (ret != 0)
            driver_data->bcd_buffer_num = 0;

        driver_data->bcd_registered = 1;
    }

    surface_list = driver_data->bcd_buffer_surfaces;

    for (i = 0; i < driver_data->bcd_buffer_num; i++) {
        if (surface == surface_list[i])
            break;
    }

    if (i == driver_data->bcd_buffer_num)
        return -1;


    return i;
}

/* keep BCD info for later registing */
int psb_add_video_bcd(
    VADriverContextP ctx,
    int width,
    int height,
    int stride,
    int num_surfaces,
    VASurfaceID *surface_list
)
{
    INIT_DRIVER_DATA;
    int oldbuf_num = driver_data->bcd_buffer_num;

    /* add new buffer, check the resolution with the previous buffers */
    if ((oldbuf_num != 0) &&
        ((driver_data->bcd_buffer_width != width ||
          driver_data->bcd_buffer_height != height ||
          driver_data->bcd_buffer_stride != stride))) {
        psb__error_message("BCD only supports one instance, previous (%dx%d,stride %d), new (%dx%d,stride %d)\n",
                           driver_data->bcd_buffer_width,
                           driver_data->bcd_buffer_height,
                           driver_data->bcd_buffer_stride,
                           width, height, stride);

        return -1;
    } else {
        driver_data->bcd_buffer_width = width;
        driver_data->bcd_buffer_height = height;
        driver_data->bcd_buffer_stride = stride;
    }

    driver_data->bcd_buffer_num += num_surfaces;
    driver_data->bcd_buffer_surfaces = realloc(driver_data->bcd_buffer_surfaces,
                                       driver_data->bcd_buffer_num * sizeof(VASurfaceID));
    memcpy(driver_data->bcd_buffer_surfaces + oldbuf_num,
           &surface_list[0],
           num_surfaces * sizeof(VASurfaceID));

    return 0;
}
