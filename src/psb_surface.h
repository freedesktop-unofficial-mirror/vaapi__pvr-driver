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
 */

#ifndef _PSB_SURFACE_H_
#define _PSB_SURFACE_H_

#include <va/va.h>
#include "psb_buffer.h"
//#include "xf86mm.h"

/* MSVDX specific */
typedef enum {
    STRIDE_352  = 0,
    STRIDE_720  = 1,
    STRIDE_1280 = 2,
    STRIDE_1920 = 3,
    STRIDE_512  = 4,
    STRIDE_1024 = 5,
    STRIDE_2048 = 6,
    STRIDE_4096 = 7,
    STRIDE_NA,
    STRIDE_UNDEFINED,
} psb_surface_stride_t;

typedef struct psb_surface_s *psb_surface_p;

struct psb_surface_s {
    struct psb_buffer_s buf;
    struct psb_buffer_s *in_loop_buf;
    struct psb_buffer_s *ref_buf;
    psb_surface_stride_t stride_mode;
    int stride;
    unsigned int luma_offset;
    unsigned int chroma_offset;
    /* Used to store driver private data, e.g. decoder specific intermediate status data
     * extra_info[0-3]: used for decode
     * extra_info[4]: surface fourcc
     * extra_info[5]: surface skippeded or not for encode
     */
    int extra_info[6];
    int size;
    unsigned int bc_buffer;
};

/*
 * Create surface
 */
VAStatus psb_surface_create(psb_driver_data_p driver_data,
                            int width, int height, int fourcc, int protected,
                            psb_surface_p psb_surface /* out */
                           );

VAStatus psb_surface_create_for_userptr(
    psb_driver_data_p driver_data,
    int width, int height,
    unsigned size, /* total buffer size need to be allocated */
    unsigned int fourcc, /* expected fourcc */
    unsigned int luma_stride, /* luma stride, could be width aligned with a special value */
    unsigned int chroma_u_stride, /* chroma stride */
    unsigned int chroma_v_stride,
    unsigned int luma_offset, /* could be 0 */
    unsigned int chroma_u_offset, /* UV offset from the beginning of the memory */
    unsigned int chroma_v_offset,
    psb_surface_p psb_surface /* out */
);



VAStatus psb_surface_create_camera(psb_driver_data_p driver_data,
                                   int width, int height, int stride, int size,
                                   psb_surface_p psb_surface, /* out */
                                   int is_v4l2,
                                   unsigned int id_or_ofs
                                  );

/* id_or_ofs: it is frame ID or frame offset in camear device memory
 *     for CI frame: it it always frame offset currently
 *     for v4l2 buf: it is offset used in V4L2 buffer mmap
 * user_ptr: virtual address of user buffer.
 */
VAStatus psb_surface_create_camera_from_ub(psb_driver_data_p driver_data,
        int width, int height, int stride, int size,
        psb_surface_p psb_surface, /* out */
        int is_v4l2,
        unsigned int id_or_ofs,
        const unsigned long *user_ptr);

/*
 * Temporarily map surface and set all chroma values of surface to 'chroma'
 */
VAStatus psb_surface_set_chroma(psb_surface_p psb_surface, int chroma);

/*
 * Destroy surface
 */
void psb_surface_destroy(psb_surface_p psb_surface);

/*
 * Wait for surface to become idle
 */
VAStatus psb_surface_sync(psb_surface_p psb_surface);

/*
 * Return surface status
 */
VAStatus psb_surface_query_status(psb_surface_p psb_surface, VASurfaceStatus *status);

#endif /* _PSB_SURFACE_H_ */
