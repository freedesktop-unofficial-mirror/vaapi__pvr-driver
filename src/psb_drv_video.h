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
 *    Waldo Bastian <waldo.bastian@intel.com>
 *
 */

#ifndef _PSB_DRV_VIDEO_H_
#define _PSB_DRV_VIDEO_H_

#include <pthread.h> /* POSIX threads headers */

#include <va/va_backend.h>
#include <va/va.h>
#include "object_heap.h"
#include "psb_def.h"
#include "xf86drm.h"
#include "psb_drm.h"
#include "psb_overlay.h"
#include "psb_texture.h"
#include <stdint.h>
#ifndef ANDROID
#include <X11/Xlibint.h>
#include <X11/X.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <X11/Xlib.h>
#else
#define XID unsigned int
#define INT16 unsigned int
#include <cutils/log.h>
#undef  LOG_TAG
#define LOG_TAG "pvr_drv_video"
#endif
#include "hwdefs/dxva_fw_flags.h"
#include <wsbm/wsbm_pool.h>

#ifndef min
#define min(a, b) ((a) < (b)) ? (a) : (b)
#endif

#ifndef max
#define max(a, b) ((a) > (b)) ? (a) : (b)
#endif

/*
 * WORKAROUND_DMA_OFF_BY_ONE: LLDMA requests may access one additional byte which can cause
 * a MMU fault if the next byte after the buffer end is on a different page that isn't mapped.
 */
#define WORKAROUND_DMA_OFF_BY_ONE
#define FOURCC_XVVA     (('A' << 24) + ('V' << 16) + ('V' << 8) + 'X')

#define PSB_MAX_PROFILES                                14
#define PSB_MAX_ENTRYPOINTS                             8
#define PSB_MAX_CONFIG_ATTRIBUTES               10
#define PSB_MAX_BUFFERTYPES                     32

/* Max # of command submission buffers */
#define PSB_MAX_CMDBUFS                         10
#define LNC_MAX_CMDBUFS_ENCODE                  4
#define PNW_MAX_CMDBUFS_ENCODE                  4

#define PSB_SURFACE_DISPLAYING_F (0x1U<<0)
#define PSB_SURFACE_IS_FLAG_SET(flags, mask) (((flags)& PSB_SURFACE_DISPLAYING_F) != 0)

/*xrandr dirty flag*/
#define PSB_NEW_ROTATION        1
#define PSB_NEW_EXTVIDEO        2

#define MAX_SLICES_PER_PICTURE 72
#define MAX_MB_ERRORS 72

typedef struct object_config_s *object_config_p;
typedef struct object_context_s *object_context_p;
typedef struct object_surface_s *object_surface_p;
typedef struct object_buffer_s *object_buffer_p;
typedef struct object_image_s *object_image_p;
typedef struct object_subpic_s *object_subpic_p;
typedef struct format_vtable_s *format_vtable_p;
typedef struct psb_driver_data_s *psb_driver_data_p;

/* post-processing data structure */
enum psb_output_method_t {
    PSB_PUTSURFACE_NONE = 0,
    PSB_PUTSURFACE_X11,/* use x11 method */
    PSB_PUTSURFACE_TEXTURE,/* texture xvideo */
    PSB_PUTSURFACE_OVERLAY,/* overlay xvideo */
    PSB_PUTSURFACE_COVERLAY,/* client overlay */
    PSB_PUTSURFACE_CTEXTURE,/* client textureblit */
    PSB_PUTSURFACE_TEXSTREAMING,/* texsteaming */
    PSB_PUTSURFACE_FORCE_TEXTURE,/* force texture xvideo */
    PSB_PUTSURFACE_FORCE_OVERLAY,/* force overlay xvideo */
    PSB_PUTSURFACE_FORCE_CTEXTURE,/* force client textureblit */
    PSB_PUTSURFACE_FORCE_COVERLAY,/* force client overlay */
    PSB_PUTSURFACE_FORCE_TEXSTREAMING,/* force texstreaming */
};

typedef struct psb_decode_info {
    uint32_t num_surface;
    uint32_t surface_id;
} psb_decode_info_t;
typedef struct msvdx_decode_info *psb_decode_info_p;

struct psb_driver_data_s {
    struct object_heap_s        config_heap;
    struct object_heap_s        context_heap;
    struct object_heap_s        surface_heap;
    struct object_heap_s        buffer_heap;
    struct object_heap_s        image_heap;
    struct object_heap_s        subpic_heap;
    char *                      bus_id;
    uint32_t                    dev_id;
    int                         drm_fd;
    int                         dup_drm_fd;

    /*  PM_QoS */
    int                         pm_qos_fd;
    int                         dri2;
    int                         dri_dummy;
    XID                         context_id;
    drm_context_t               drm_context;
    drmLock                     *drm_lock;
    int                         contended_lock;
    pthread_mutex_t             drm_mutex;
    format_vtable_p             profile2Format[PSB_MAX_PROFILES][PSB_MAX_ENTRYPOINTS];
    uint32_t                    msvdx_context_base;
    int                         video_sd_disabled;
    int                         video_hd_disabled;
    unsigned char *                      camera_bo;
    uint32_t                    camera_phyaddr;
    uint32_t                    camera_size;
    unsigned char *                      rar_bo;
    uint32_t                    rar_phyaddr;
    uint32_t                    rar_size;

    int encode_supported;
    int decode_supported;
    int hd_encode_supported;
    int hd_decode_supported;

    int execIoctlOffset;
    int getParamIoctlOffset;

    struct _WsbmBufferPool *main_pool;
    struct _WsbmFenceMgr *fence_mgr;

    enum psb_output_method_t output_method;

    /* whether the post-processing use client overlay or not */
    int coverlay;
    int coverlay_init;
    PsbPortPrivRec coverlay_priv;


    /* whether the post-processing use client textureblit or not */
    int ctexture;
    struct psb_texture_s ctexture_priv;

    /*
    //whether the post-processing use texstreaing or not
    int ctexstreaing;
    struct psb_texstreaing ctexstreaing_priv;
    */

    unsigned char *ws_priv; /* window system related data structure */


    VASurfaceID cur_displaying_surface;
    VASurfaceID last_displaying_surface;

    VADisplayAttribute ble_black_mode;
    VADisplayAttribute ble_white_mode;

    VADisplayAttribute blueStretch_gain;
    VADisplayAttribute skinColorCorrection_gain;

    VADisplayAttribute brightness;
    VADisplayAttribute hue;
    VADisplayAttribute contrast;
    VADisplayAttribute saturation;
    /*Save RenderMode and RenderRect attribute
     * for medfield android extend video mode.*/
    uint32_t render_device;
    uint32_t render_mode;
    VARectangle  render_rect;

    unsigned int clear_color;

    int  is_oold;

    unsigned int load_csc_matrix;
    signed int   csc_matrix[3][3];

    /* subpic number current buffers support */
    unsigned int max_subpic;

    /* for multi-thread safe */
    int use_xrandr_thread;
    pthread_mutex_t output_mutex;
    pthread_t xrandr_thread_id;
    int extend_fullscreen;

    int drawable_info;
    int dummy_putsurface;
    int fixed_fps;
    unsigned int frame_count;
    unsigned int overlay_idle_frame;

    uint32_t blend_mode;
    uint32_t blend_color;
    uint32_t overlay_auto_paint_color_key;
    uint32_t color_key;

    /*output rotation info*/
    int disable_msvdx_rotate;
    int msvdx_rotate_want; /* msvdx rotate info programed to msvdx */
    int va_rotate; /* VA rotate passed from APP */
    int mipi0_rotation; /* window manager rotation */
    int mipi1_rotation; /* window manager rotation */
    int hdmi_rotation; /* window manager rotation */
    int local_rotation; /* final device rotate: VA rotate+wm rotate */
    int extend_rotation; /* final device rotate: VA rotate+wm rotate */

    unsigned int outputmethod_checkinterval;

    uint32_t bcd_id;
    uint32_t bcd_ioctrl_num;
    uint32_t bcd_registered;
    uint32_t bcd_buffer_num;
    int bcd_buffer_width;
    int bcd_buffer_height;
    int bcd_buffer_stride;
    VASurfaceID *bcd_buffer_surfaces;
    uint32_t ts_source_created;

    uint32_t xrandr_dirty;
    uint32_t xrandr_update;
    /*only VAProfileH264ConstrainedBaseline profile enable error concealment*/
    uint32_t ec_enabled;

    uint32_t pre_surfaceid;
    psb_decode_info_t decode_info;
    drm_psb_msvdx_decode_status_t *msvdx_decode_status;
    VASurfaceDecodeMBErrors *surface_mb_error;

    unsigned char *hPVR2DContext;

    VAGenericID wrapped_surface_id[VIDEO_BUFFER_NUM];
    VAGenericID wrapped_subpic_id[VIDEO_BUFFER_NUM];
    PVR2DMEMINFO *videoBuf[VIDEO_BUFFER_NUM];
    PVR2DMEMINFO *subpicBuf[VIDEO_BUFFER_NUM];

    int is_android;
};

#define IS_MRST(driver_data) ((driver_data->dev_id & 0xFFFC) == 0x4100)
#define IS_MFLD(driver_data) ((driver_data->dev_id & 0xFFFC) == 0x0130)

struct object_config_s {
    struct object_base_s base;
    VAProfile profile;
    VAEntrypoint entrypoint;
    VAConfigAttrib attrib_list[PSB_MAX_CONFIG_ATTRIBUTES];
    int attrib_count;
    format_vtable_p format_vtable;
};

struct object_context_s {
    struct object_base_s base;
    VAContextID context_id;
    VAConfigID config_id;
    VAProfile profile;
    VAEntrypoint entry_point;
    int picture_width;
    int picture_height;
    int num_render_targets;
    VASurfaceID *render_targets;
    int va_flags;

    object_surface_p current_render_target;
    VASurfaceID current_render_surface_id;
    psb_driver_data_p driver_data;
    format_vtable_p format_vtable;
    unsigned char *format_data;
    struct psb_cmdbuf_s *cmdbuf_list[PSB_MAX_CMDBUFS];
    struct lnc_cmdbuf_s *lnc_cmdbuf_list[LNC_MAX_CMDBUFS_ENCODE];
    struct pnw_cmdbuf_s *pnw_cmdbuf_list[PNW_MAX_CMDBUFS_ENCODE];

    struct psb_cmdbuf_s *cmdbuf; /* Current cmd buffer */
    struct lnc_cmdbuf_s *lnc_cmdbuf;
    struct pnw_cmdbuf_s *pnw_cmdbuf;

    int cmdbuf_current;

    /* Buffers */
    object_buffer_p buffers_unused[PSB_MAX_BUFFERTYPES]; /* Linked lists (HEAD) of unused buffers for each buffer type */
    int buffers_unused_count[PSB_MAX_BUFFERTYPES]; /* Linked lists (HEAD) of unused buffers for each buffer type */
    object_buffer_p buffers_unused_tail[PSB_MAX_BUFFERTYPES]; /* Linked lists (TAIL) of unused buffers for each buffer type */
    object_buffer_p buffers_active[PSB_MAX_BUFFERTYPES]; /* Linked lists of active buffers for each buffer type */

    object_buffer_p *buffer_list; /* for vaRenderPicture */
    int num_buffers;

    enum {
        psb_video_none = 0,
        psb_video_mc,
        psb_video_vld,
        psb_video_deblock
    } video_op;

    uint32_t operating_mode;
    uint32_t flags; /* See render flags below */
    uint32_t first_mb;
    uint32_t last_mb;

    int is_oold;
    int msvdx_rotate;
    int interlaced_stream;

    uint32_t msvdx_context;

    /* Debug */
    uint32_t frame_count;
    uint32_t slice_count;
};

#define ROTATE_VA2MSVDX(va_rotate)  (va_rotate)
#define CONTEXT_ROTATE(obj_context) (obj_context->msvdx_rotate != ROTATE_VA2MSVDX(VA_ROTATION_NONE))

struct object_surface_s {
    struct object_base_s base;
    VASurfaceID surface_id;
    VAContextID context_id;
    int width;
    int height;
    int height_origin;
    int width_r;
    int height_r;
    struct psb_surface_s *psb_surface;
    struct psb_surface_s *psb_surface_rotate; /* Alternative output surface for rotation */
    void *subpictures;/* if not NULL, have subpicture information */
    unsigned int subpic_count; /* to ensure output have enough space for PDS & RAST */
    unsigned int derived_imgcnt; /* is the surface derived by a VAImage? */
    unsigned long display_timestamp; /* record the time point of put surface*/
};

struct object_buffer_s {
    struct object_base_s base;
    object_buffer_p ptr_next; /* Generic ptr for linked list */
    object_buffer_p *pptr_prev_next; /* Generic ptr for linked list */
    struct psb_buffer_s *psb_buffer;
    unsigned char *buffer_data;
    VACodedBufferSegment codedbuf_mapinfo[8]; /* for VAEncCodedBufferType */
    unsigned int size;
    unsigned int alloc_size;
    unsigned int max_num_elements;
    unsigned int num_elements;
    object_context_p context;
    VABufferType type;
    uint32_t last_used;
};

struct object_image_s {
    struct object_base_s base;
    VAImage image;
    unsigned int palette[16];
    int subpic_ref;
    VASurfaceID derived_surface;
};

struct object_subpic_s {
    struct object_base_s base;
    VASubpictureID subpic_id;

    VAImageID image_id;

    /* chromakey range */
    unsigned int chromakey_min;
    unsigned int chromakey_max;
    unsigned int chromakey_mask;

    /* global alpha */
    unsigned int global_alpha;

    /* flags */
    unsigned int flags; /* see below */

    unsigned char *surfaces; /* surfaces, associated with this subpicture */
};

#define MEMSET_OBJECT(ptr, data_struct) \
        memset((unsigned char *)ptr + sizeof(struct object_base_s),\
                0,                          \
               sizeof(data_struct) - sizeof(struct object_base_s))

struct format_vtable_s {
    void (*queryConfigAttributes)(
        VAProfile profile,
        VAEntrypoint entrypoint,
        VAConfigAttrib *attrib_list,
        int num_attribs
    );
    VAStatus(*validateConfig)(
        object_config_p obj_config
    );
    VAStatus(*createContext)(
        object_context_p obj_context,
        object_config_p obj_config
    );
    void (*destroyContext)(
        object_context_p obj_context
    );
    VAStatus(*beginPicture)(
        object_context_p obj_context
    );
    VAStatus(*renderPicture)(
        object_context_p obj_context,
        object_buffer_p *buffers,
        int num_buffers
    );
    VAStatus(*endPicture)(
        object_context_p obj_context
    );
};


#define psb__bounds_check(x, max)                                       \
    do { ASSERT(x < max); if (x >= max) x = max - 1; } while(0);

static inline unsigned long GetTickCount()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL))
        return 0;
    return tv.tv_usec / 1000 + tv.tv_sec * 1000;
}

inline static char * buffer_type_to_string(int type)
{
    switch (type) {
    case VAPictureParameterBufferType:
        return "VAPictureParameterBufferType";
    case VAIQMatrixBufferType:
        return "VAIQMatrixBufferType";
    case VABitPlaneBufferType:
        return "VABitPlaneBufferType";
    case VASliceGroupMapBufferType:
        return "VASliceGroupMapBufferType";
    case VASliceParameterBufferType:
        return "VASliceParameterBufferType";
    case VASliceDataBufferType:
        return "VASliceDataBufferType";
    case VAProtectedSliceDataBufferType:
        return "VAProtectedSliceDataBufferType";
    case VAMacroblockParameterBufferType:
        return "VAMacroblockParameterBufferType";
    case VAResidualDataBufferType:
        return "VAResidualDataBufferType";
    case VADeblockingParameterBufferType:
        return "VADeblockingParameterBufferType";
    case VAImageBufferType:
        return "VAImageBufferType";
    case VAEncCodedBufferType:
        return "VAEncCodedBufferType";
    case VAEncSequenceParameterBufferType:
        return "VAEncSequenceParameterBufferType";
    case VAEncPictureParameterBufferType:
        return "VAEncPictureParameterBufferType";
    case VAEncSliceParameterBufferType:
        return "VAEncSliceParameterBufferType";
    default:
        return "UnknowBuffer";
    }
}

inline static int Angle2Rotation(int angle)
{
    angle %= 360;
    switch (angle) {
    case 0:
        return VA_ROTATION_NONE;
    case 90:
        return VA_ROTATION_90;
    case 180:
        return VA_ROTATION_180;
    case 270:
        return VA_ROTATION_270;
    default:
        return -1;
    }
}

inline static int Rotation2Angle(int rotation)
{
    switch (rotation) {
    case VA_ROTATION_NONE:
        return 0;
    case VA_ROTATION_90:
        return 90;
    case VA_ROTATION_180:
        return 180;
    case VA_ROTATION_270:
        return 270;
    default:
        return -1;
    }
}

int psb_parse_config(char *env, char *env_value);

int LOCK_HARDWARE(psb_driver_data_p driver_data);
int UNLOCK_HARDWARE(psb_driver_data_p driver_data);

#endif /* _PSB_DRV_VIDEO_H_ */
