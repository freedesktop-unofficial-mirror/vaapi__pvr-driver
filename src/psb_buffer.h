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

#ifndef _PSB_BUFFER_H_
#define _PSB_BUFFER_H_

#include "psb_drv_video.h"

#include <RAR/rar.h>

//#include "xf86mm.h"

/* For TopazSC, it indicates the next frame should be skipped */
#define SKIP_NEXT_FRAME   0x800

typedef struct psb_buffer_s *psb_buffer_p;

/* VPU = MSVDX */
typedef enum psb_buffer_type_e {
    psb_bt_cpu_vpu = 0,                 /* Shared between CPU & Video PU */
    psb_bt_cpu_vpu_shared,              /* CPU/VPU can access, and can shared by other process */
    psb_bt_surface,                     /* Tiled surface */
    psb_bt_vpu_only,                    /* Only used by Video PU */
    psb_bt_cpu_only,                    /* Only used by CPU */
    psb_bt_camera,                      /* memory is camera device memory */
    psb_bt_rar,                         /* global RAR buffer */
    psb_bt_rar_surface,                 /* memory is RAR device memory for protected surface*/
    psb_bt_rar_slice,                   /* memory is RAR device memory for slice data */
    psb_bt_user_buffer                  /* memory is from user buffers */
} psb_buffer_type_t;

typedef enum psb_buffer_status_e {
    psb_bs_unfinished = 0,
    psb_bs_ready,
    psb_bs_queued,
    psb_bs_abandoned
} psb_buffer_status_t;

struct psb_buffer_s {
    struct _WsbmBufferObject *drm_buf;
    int wsbm_synccpu_flag;
    uint64_t pl_flags;

    psb_buffer_type_t type;
    psb_buffer_status_t status;
    uint32_t rar_handle;
    unsigned int buffer_ofs; /* several buffers may share one BO (camera/RAR), and use offset to distinguish it */
    struct psb_buffer_s *next;
    void *user_ptr; /* user pointer for user buffers */
    psb_driver_data_p driver_data; /* for RAR buffer release */
};

/*
 * Create buffer
 */
VAStatus psb_buffer_create(psb_driver_data_p driver_data,
                           unsigned int size,
                           psb_buffer_type_t type,
                           psb_buffer_p buf
                          );

/*
 * Setstatus Buffer
 */
int psb_buffer_setstatus(psb_buffer_p buf, uint32_t set_placement, uint32_t clr_placement);


/*
 * Reference buffer
 */
VAStatus psb_buffer_reference(psb_driver_data_p driver_data,
                              psb_buffer_p buf,
                              psb_buffer_p reference_buf
                             );

/*
 * Suspend buffer
 */
void psb__suspend_buffer(psb_driver_data_p driver_data, object_buffer_p obj_buffer);

/*
 * Destroy buffer
 */
void psb_buffer_destroy(psb_buffer_p buf);

/*
 * Map buffer
 *
 * Returns 0 on success
 */
int psb_buffer_map(psb_buffer_p buf, void **address /* out */);

int psb_buffer_sync(psb_buffer_p buf);

int psb_codedbuf_map_mangle(
    VADriverContextP ctx,
    object_buffer_p obj_buffer,
    void **pbuf /* out */
);


/*
 * Unmap buffer
 *
 * Returns 0 on success
 */
int psb_buffer_unmap(psb_buffer_p buf);

/*
 * Create buffer from camera device memory
 */
VAStatus psb_buffer_create_camera(psb_driver_data_p driver_data,
                                  psb_buffer_p buf,
                                  int is_v4l2,
                                  int id_or_ofs
                                 );

/*
 * Create RAR buffer
 */
VAStatus psb_buffer_create_rar(psb_driver_data_p driver_data,
                               unsigned int size,
                               psb_buffer_p buf
                              );

/*
 * Destroy RAR buffer
 */
VAStatus psb_buffer_destroy_rar(psb_driver_data_p driver_data,
                                psb_buffer_p buf
                               );

/*
 * Reference one RAR buffer from handle
 */
VAStatus psb_buffer_reference_rar(psb_driver_data_p driver_data,
                                  uint32_t rar_handle,
                                  psb_buffer_p buf
                                 );
/*
 * Create one buffer from user buffer
 * id_or_ofs is CI frame ID (actually now is frame offset), or V4L2 buffer offset
 * user_ptr :virtual address of user buffer start.
 */
VAStatus psb_buffer_create_camera_from_ub(psb_driver_data_p driver_data,
        psb_buffer_p buf,
        int id_or_ofs,
        int size,
        const unsigned long * user_ptr);
#ifdef ANDROID
#define DRM_PSB_FLAG_MEM_CI (1<<9)
#define DRM_PSB_FLAG_MEM_RAR (1<<10)
#else
#define DRM_PSB_FLAG_MEM_CI     (1 << 3) /* TTM_PL_FLAG_PRIV0 */
#define DRM_PSB_FLAG_MEM_RAR    (1 << 5) /* TTM_PL_FLAG_PRIV2 */
#endif

#endif /* _PSB_BUFFER_H_ */
