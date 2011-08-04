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
 *    Waldo Bastian <waldo.bastian@intel.com>
 *
 */

#include "psb_buffer.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <wsbm/wsbm_manager.h>

#include "psb_drm.h"
#include "psb_def.h"

#include <pnw_cmdbuf.h>

#include "pnw_jpeg.h"
#include "pnw_H264ES.h"
#include "lnc_H264ES.h"

/*
 * Create buffer
 */
VAStatus psb_buffer_create(psb_driver_data_p driver_data,
                           unsigned int size,
                           psb_buffer_type_t type,
                           psb_buffer_p buf
                          )
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int allignment;
    uint32_t placement;
    int ret;

    /* reset rar_handle to NULL */
    buf->rar_handle = 0;
    buf->buffer_ofs = 0;

    buf->type = type;
    buf->driver_data = driver_data; /* only for RAR buffers */

    /* TODO: Mask values are a guess */
    switch (type) {
    case psb_bt_cpu_vpu:
        allignment = 1;
        placement = DRM_PSB_FLAG_MEM_MMU;
        break;
    case psb_bt_cpu_vpu_shared:
        allignment = 1;
        placement = DRM_PSB_FLAG_MEM_MMU | WSBM_PL_FLAG_SHARED;
        break;
    case psb_bt_surface:
        allignment = 0;
        /* Xvideo will share surface buffer, set SHARED flag
         */
        if (getenv("PSB_VIDEO_SURFACE_MMU")) {
            psb__information_message("Allocate surface from MMU heap\n");
            placement = DRM_PSB_FLAG_MEM_MMU | WSBM_PL_FLAG_SHARED;
        } else {
            psb__information_message("Allocate surface from TT heap\n");
            placement = WSBM_PL_FLAG_TT | WSBM_PL_FLAG_SHARED;
        }
        break;
    case psb_bt_vpu_only:
        allignment = 1;
        placement = DRM_PSB_FLAG_MEM_MMU;
        break;

    case psb_bt_cpu_only:
        allignment = 1;
        placement = WSBM_PL_FLAG_SYSTEM | WSBM_PL_FLAG_CACHED;
        break;
    case psb_bt_camera:
        allignment = 1;
        placement = DRM_PSB_FLAG_MEM_CI | WSBM_PL_FLAG_SHARED;
        break;
    case psb_bt_rar:
        allignment = 1;
        placement = DRM_PSB_FLAG_MEM_RAR | WSBM_PL_FLAG_SHARED;
        break;
    default:
        vaStatus = VA_STATUS_ERROR_UNKNOWN;
        DEBUG_FAILURE;
        return vaStatus;
    }
    ret = LOCK_HARDWARE(driver_data);
    if (ret) {
        UNLOCK_HARDWARE(driver_data);
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE_RET;
        return vaStatus;
    }

#ifdef VA_EMULATOR
    placement |= WSBM_PL_FLAG_SHARED;
#endif

#ifdef MSVDX_VA_EMULATOR
    placement |= WSBM_PL_FLAG_SHARED;
#endif

    allignment = 4096; /* temporily more safe */

    //psb__error_message("FIXME: should use geetpagesize() ?\n");
    ret = wsbmGenBuffers(driver_data->main_pool, 1, &buf->drm_buf,
                         allignment, placement);
    if (!buf->drm_buf) {
        psb__error_message("failed to gen wsbm buffers\n");
        UNLOCK_HARDWARE(driver_data);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    /* here use the placement when gen buffer setted */
    ret = wsbmBOData(buf->drm_buf, size, NULL, NULL, 0);
    UNLOCK_HARDWARE(driver_data);
    if (ret) {
        psb__error_message("failed to alloc wsbm buffers\n");
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    if (placement & WSBM_PL_FLAG_TT)
        psb__information_message("Create BO with TT placement (%d byte),BO GPU offset hint=0x%08x\n",
                                 size, wsbmBOOffsetHint(buf->drm_buf));

    buf->pl_flags = placement;
    buf->status = psb_bs_ready;
    buf->wsbm_synccpu_flag = 0;

    return VA_STATUS_SUCCESS;
}


/*
 * buffer setstatus
 *
 * Returns 0 on success
 */
int psb_buffer_setstatus(psb_buffer_p buf, uint32_t set_placement, uint32_t clr_placement)
{
    int ret = 0;

    ASSERT(buf);
    ASSERT(buf->driver_data);

    ret = wsbmBOSetStatus(buf->drm_buf, set_placement, clr_placement);
    if (ret == 0)
        buf->pl_flags = set_placement;

    return ret;
}


VAStatus psb_buffer_reference(psb_driver_data_p driver_data,
                              psb_buffer_p buf,
                              psb_buffer_p reference_buf
                             )
{
    int ret = 0;
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    memcpy(buf, reference_buf, sizeof(*buf));
    buf->drm_buf = NULL;

    ret = LOCK_HARDWARE(driver_data);
    if (ret) {
        UNLOCK_HARDWARE(driver_data);
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE_RET;
        return vaStatus;
    }

    ret = wsbmGenBuffers(driver_data->main_pool,
                         1,
                         &buf->drm_buf,
                         4096,  /* page alignment */
                         0);
    if (!buf->drm_buf) {
        psb__error_message("failed to gen wsbm buffers\n");
        UNLOCK_HARDWARE(driver_data);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    ret = wsbmBOSetReferenced(buf->drm_buf, wsbmKBufHandle(wsbmKBuf(reference_buf->drm_buf)));
    UNLOCK_HARDWARE(driver_data);
    if (ret) {
        psb__error_message("failed to alloc wsbm buffers\n");
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}


/*
 * Destroy buffer
 */
void psb_buffer_destroy(psb_buffer_p buf)
{
    ASSERT(buf);
    if (buf->drm_buf == NULL)
        return;
    if (psb_bs_unfinished != buf->status) {
        ASSERT(buf->driver_data);
        wsbmBOUnreference(&buf->drm_buf);
        if (buf->rar_handle)
            psb_buffer_destroy_rar(buf->driver_data, buf);
        buf->driver_data = NULL;
        buf->status = psb_bs_unfinished;
    }
}

/*
 * Map buffer
 *
 * Returns 0 on success
 */
int psb_buffer_map(psb_buffer_p buf, void **address /* out */)
{
    int ret;

    ASSERT(buf);
    ASSERT(buf->driver_data);

    /* multiple mapping not allowed */
    if (buf->wsbm_synccpu_flag) {
        psb__information_message("Multiple mapping request detected, unmap previous mapping\n");
        psb__information_message("Need to fix application to unmap at first, then request second mapping request\n");

        psb_buffer_unmap(buf);
    }

    /* don't think TG deal with READ/WRITE differently */
    buf->wsbm_synccpu_flag = WSBM_SYNCCPU_READ | WSBM_SYNCCPU_WRITE;
#ifdef DEBUG_TRACE
    wsbmBOWaitIdle(buf->drm_buf, 0);
#else
    ret = wsbmBOSyncForCpu(buf->drm_buf, buf->wsbm_synccpu_flag);
    if (ret) {
        psb__error_message("faild to sync bo for cpu\n");
        return ret;
    }
#endif

    if (buf->type == psb_bt_user_buffer)
        *address = buf->user_ptr;
    else
        *address = wsbmBOMap(buf->drm_buf, buf->wsbm_synccpu_flag);

    if (*address == NULL) {
        psb__error_message("failed to map buffer\n");
        return -1;
    }

    return 0;

}

/*
 * Unmap buffer
 *
 * Returns 0 on success
 */
int psb_buffer_unmap(psb_buffer_p buf)
{
    ASSERT(buf);
    ASSERT(buf->driver_data);

    if (buf->wsbm_synccpu_flag)
        (void) wsbmBOReleaseFromCpu(buf->drm_buf, buf->wsbm_synccpu_flag);

    buf->wsbm_synccpu_flag = 0;

    if (buf->type != psb_bt_user_buffer)
        wsbmBOUnmap(buf->drm_buf);

    return 0;
}


/*
 * Return special data structure for codedbuffer
 *
 * Returns 0 on success
 */
#define CONFIG(id)  ((object_config_p) object_heap_lookup( &driver_data->config_heap, id ))
#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData;
int psb_codedbuf_map_mangle(
    VADriverContextP ctx,
    object_buffer_p obj_buffer,
    void **pbuf /* out */
)
{
    object_context_p obj_context = obj_buffer->context;
    INIT_DRIVER_DATA;
    VACodedBufferSegment *p = &obj_buffer->codedbuf_mapinfo[0];
    void *raw_codedbuf;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    unsigned int next_buf_off;
    int i;

    if (NULL == pbuf) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        return vaStatus;
    }

    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;

        psb_buffer_unmap(obj_buffer->psb_buffer);
        obj_buffer->buffer_data = NULL;

        return vaStatus;
    }

    raw_codedbuf = *pbuf;
    /* reset the mapinfo */
    memset(obj_buffer->codedbuf_mapinfo, 0, sizeof(obj_buffer->codedbuf_mapinfo));

    *pbuf = p = &obj_buffer->codedbuf_mapinfo[0];
    if (IS_MRST(driver_data)) {
        /* one segment */
        p->size = *((unsigned long *) raw_codedbuf); /* 1st DW is the size */
        p->status = *((unsigned long *) raw_codedbuf + 1); /* 2nd DW
                                                        * is rc status */
        p->reserved = 0;
        p->buf = (void *)((unsigned long *) raw_codedbuf + 4); /* skip 4DWs */
        lnc_H264_append_aux_info(obj_context,
                                 obj_buffer,
                                 (unsigned char *)p->buf,
                                 &(p->size));
    } else { /* MFLD */
        object_config_p obj_config = CONFIG(obj_context->config_id);

        if (NULL == obj_config) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            DEBUG_FAILURE;

            psb_buffer_unmap(obj_buffer->psb_buffer);
            obj_buffer->buffer_data = NULL;

            return vaStatus;
        }

        if (VAProfileJPEGBaseline != obj_config->profile
            && (*((unsigned long *) raw_codedbuf + 1) & SKIP_NEXT_FRAME) != 0) {
            /*Set frame skip flag*/
            pnw_set_frame_skip_flag(obj_context);
        }
        switch (obj_config->profile) {
        case VAProfileMPEG4Simple:
        case VAProfileMPEG4AdvancedSimple:
        case VAProfileMPEG4Main:
            /* one segment */
            p->size = *((unsigned long *) raw_codedbuf);
            p->buf = (void *)((unsigned long *) raw_codedbuf + 4); /* skip 4DWs */
            psb__information_message("coded buffer size %d\n", p->size);
            break;

        case VAProfileH264Baseline:
        case VAProfileH264Main:
        case VAProfileH264High:
        case VAProfileH264ConstrainedBaseline:
            /* 1st segment */
            p->size = *((unsigned long *) raw_codedbuf);
            p->buf = (void *)((unsigned long *) raw_codedbuf + 4); /* skip 4DWs */

            psb__information_message("1st segment coded buffer size %d\n", p->size);
            if (pnw_get_parallel_core_number(obj_context) == 2) {
                /*The second part of coded buffer which generated by core 2 is the
                 * first part of encoded clip, while the first part of coded buffer
                 * is the second part of encoded clip.*/
                next_buf_off = ~0xf & (obj_buffer->size / pnw_get_parallel_core_number(obj_context));
                p->next = &p[1];
                p[1].size = p->size;
                p[1].buf = p->buf;
                p[1].next = NULL;
                p->size = *(unsigned long *)((unsigned long)raw_codedbuf + next_buf_off);
                p->buf = (void *)(((unsigned long *)((unsigned long)raw_codedbuf + next_buf_off)) + 4); /* skip 4DWs */
                psb__information_message("2nd segment coded buffer offset: 0x%08x,  size: %d\n",
                                         next_buf_off, p->size);
            } else
                p->next = NULL;
            break;

        case VAProfileH263Baseline:
            /* one segment */
            p->size = *((unsigned long *) raw_codedbuf);
            p->buf = (void *)((unsigned long *) raw_codedbuf + 4); /* skip 4DWs */
            psb__information_message("coded buffer size %d\n", p->size);
            break;

        case VAProfileJPEGBaseline:
            /* 3~6 segment
                 */
            pnw_jpeg_AppendMarkers(obj_context, raw_codedbuf);
            next_buf_off = 0;
            /*Max resolution 4096x4096 use 6 segments*/
            for (i = 0; i < PNW_JPEG_MAX_SCAN_NUM + 1; i++) {
                p->size = *(unsigned long *)((unsigned long)raw_codedbuf + next_buf_off);
                p->buf = (void *)((unsigned long *)((unsigned long)raw_codedbuf + next_buf_off) + 4);  /* skip 4DWs */
                next_buf_off = *((unsigned long *)((unsigned long)raw_codedbuf + next_buf_off) + 3);

                psb__information_message("JPEG coded buffer segment %d size: %d\n", i, p->size);
                psb__information_message("JPEG coded buffer next segment %d offset: %d\n", i + 1, next_buf_off);

                if (next_buf_off == 0) {
                    p->next = NULL;
                    break;
                } else
                    p->next = &p[1];
                p++;
            }
            break;

        default:
            psb__error_message("unexpected case\n");

            psb_buffer_unmap(obj_buffer->psb_buffer);
            obj_buffer->buffer_data = NULL;
            break;
        }
    }

    return 0;
}

int psb_buffer_sync(psb_buffer_p buf)
{
    int ret;

    ASSERT(buf);
    ASSERT(buf->driver_data);

    ret = wsbmBOSyncForCpu(buf->drm_buf, buf->wsbm_synccpu_flag);
    if (ret) {
        psb__error_message("faild to sync bo for cpu\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }

    return VA_STATUS_SUCCESS;
}
