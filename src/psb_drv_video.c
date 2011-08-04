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

#include <va/va_backend.h>
#include <va/va_backend_tpi.h>
#include <va/va_backend_egl.h>
#include <va/va_dricommon.h>

#include "psb_drv_video.h"
#include "psb_cmdbuf.h"
#include "lnc_cmdbuf.h"
#include "pnw_cmdbuf.h"
#include "psb_surface.h"
#include "psb_MPEG2.h"
#include "psb_MPEG4.h"
#include "psb_H264.h"
#include "psb_VC1.h"
#include "pnw_MPEG2.h"
#include "pnw_MPEG4.h"
#include "pnw_H264.h"
#include "pnw_VC1.h"
#include "lnc_MPEG4ES.h"
#include "lnc_H264ES.h"
#include "lnc_H263ES.h"
#include "pnw_MPEG4ES.h"
#include "pnw_H264ES.h"
#include "pnw_H263ES.h"
#include "pnw_jpeg.h"
#include "psb_output.h"
#include "lnc_ospm.h"
#include "psb_texstreaming.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <wsbm/wsbm_pool.h>
#include <wsbm/wsbm_manager.h>
#include <wsbm/wsbm_util.h>
#include <wsbm/wsbm_fencemgr.h>
#include <linux/videodev2.h>
#include <sys/mman.h>

#include "psb_def.h"
#include "psb_ws_driver.h"
#include "ci_va.h"

#ifndef PSB_PACKAGE_VERSION
#define PSB_PACKAGE_VERSION "Undefined"
#endif

#define PSB_DRV_VERSION  PSB_PACKAGE_VERSION
#define PSB_CHG_REVISION "(0X0000005E)"

#define PSB_STR_VENDOR_MRST     "Intel GMA500-MRST-" PSB_DRV_VERSION " " PSB_CHG_REVISION
#define PSB_STR_VENDOR_MFLD     "Intel GMA500-MFLD-" PSB_DRV_VERSION " " PSB_CHG_REVISION

#define MAX_UNUSED_BUFFERS      16

#define PSB_MAX_FLIP_DELAY (1000/30/10)

#ifdef DEBUG_TRACE
#include <signal.h>
#endif

#define EXPORT __attribute__ ((visibility("default")))

#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData;
#define INIT_FORMAT_VTABLE format_vtable_p format_vtable = ((profile < PSB_MAX_PROFILES) && (entrypoint < PSB_MAX_ENTRYPOINTS)) ? driver_data->profile2Format[profile][entrypoint] : NULL;

#define CONFIG(id)  ((object_config_p) object_heap_lookup( &driver_data->config_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))
#define SURFACE(id)    ((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))

#define CONFIG_ID_OFFSET        0x01000000
#define CONTEXT_ID_OFFSET       0x02000000
#define SURFACE_ID_OFFSET       0x03000000
#define BUFFER_ID_OFFSET        0x04000000
#define IMAGE_ID_OFFSET         0x05000000
#define SUBPIC_ID_OFFSET        0x06000000

static int psb_get_device_info(VADriverContextP ctx);

/*
 * read a config "env" for libva.conf or from environment setting
 * liva.conf has higher priority
 * return 0: the "env" is set, and the value is copied into env_value
 *        1: the env is not set
 */
int psb_parse_config(char *env, char *env_value)
{
    char *token, *value, *saveptr;
    char oneline[1024];
    FILE *fp = NULL;
    char *env_ptr;

    if (env_value == NULL)
        return 1;

    fp = fopen("/etc/psbvideo.conf", "r");
    while (fp && (fgets(oneline, 1024, fp) != NULL)) {
        if (strlen(oneline) == 1)
            continue;
        token = strtok_r(oneline, "=\n", &saveptr);
        value = strtok_r(NULL, "=\n", &saveptr);

        if (NULL == token || NULL == value)
            continue;

        if (strcmp(token, env) == 0) {
            if (env_value)
                strcpy(env_value, value);

            fclose(fp);

            return 0;
        }
    }
    if (fp)
        fclose(fp);

    env_ptr = getenv(env);
    if (env_ptr) {
        if (env_value)
            strncpy(env_value, env_ptr, strlen(env_ptr));

        return 0;
    }

    return 1;
}

static FILE *psb_video_debug_fp = NULL;

static void psb__open_log(void)
{
    char log_fn[1024];

    if (psb_parse_config("PSB_VIDEO_DEBUG", &log_fn[0]) == 0) {
        unsigned int suffix = 0xffff & ((unsigned int)time(NULL));
        if (strcmp(log_fn, "/dev/stdout") != 0)
            sprintf(log_fn + strlen(log_fn), ".%d", suffix);
        psb_video_debug_fp = fopen(log_fn, "w");
    }
}

static void psb__close_log(void)
{
    if (psb_video_debug_fp != NULL)
        fclose(psb_video_debug_fp);
}

void psb__error_message(const char *msg, ...)
{
    va_list args;
    FILE *fp;

    if (psb_video_debug_fp == NULL) /* not set the debug */
        fp = stderr;
    else
        fp = psb_video_debug_fp;

    fprintf(fp, "[0x%08lx]psb_drv_video error(%d:0x%08lx) ",
            GetTickCount(), getpid(), pthread_self());
    va_start(args, msg);
    vfprintf(fp, msg, args);
    va_end(args);

    fflush(fp);
    fsync(fileno(fp));
}

void psb__information_message(const char *msg, ...)
{
    if (psb_video_debug_fp) {
        va_list args;

        fprintf(psb_video_debug_fp, "[0x%08lx]psb_drv_video(%d:0x%08lx) ",
                GetTickCount(), getpid(), pthread_self());
        va_start(args, msg);
        vfprintf(psb_video_debug_fp, msg, args);
        va_end(args);
        fflush(psb_video_debug_fp);
        fsync(fileno(psb_video_debug_fp));
    }
}

static int Angle2Rotation(int angle)
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

static int Rotation2Angle(int rotation)
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

#ifdef DEBUG_TRACE
void psb__trace_message(const char *msg, ...)
{
    va_list args;
    static const char *trace_file = 0;
    static FILE *trace = 0;

    if (!trace_file) {
        char trace_fn[1024];

        if (psb_parse_config("PSB_VIDEO_TRACE", &trace_fn[0]) == 0)
            trace_file = trace_fn;

        if (getenv("PSB_VIDEO_TRACE"))
            trace_file = getenv("PSB_VIDEO_TRACE");

        if (trace_file) {
            trace = fopen(trace_file, "w");
            if (trace) {
                time_t curtime;
                time(&curtime);
                fprintf(trace, "---- %s\n---- Start Trace ----\n", ctime(&curtime));
            }
        } else {
            trace_file = "none";
        }
    }
    if (trace) {
        if (msg) {
            va_start(args, msg);
            vfprintf(trace, msg, args);
            va_end(args);
        } else {
            fflush(trace);
        }
    }
}
#endif

VAStatus psb_QueryConfigProfiles(
    VADriverContextP ctx,
    VAProfile *profile_list,    /* out */
    int *num_profiles            /* out */
)
{
    (void) ctx; /* unused */
    int i = 0;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    INIT_DRIVER_DATA

    if (NULL == profile_list) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == num_profiles) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

//    profile_list[i++] = VAProfileMPEG2Simple;
    profile_list[i++] = VAProfileMPEG2Main;
    profile_list[i++] = VAProfileMPEG4Simple;
    profile_list[i++] = VAProfileMPEG4AdvancedSimple;
//    profile_list[i++] = VAProfileMPEG4Main;
    profile_list[i++] = VAProfileH264Baseline;
    profile_list[i++] = VAProfileH264Main;
    profile_list[i++] = VAProfileH264High;
    profile_list[i++] = VAProfileVC1Simple;
    profile_list[i++] = VAProfileVC1Main;
    profile_list[i++] = VAProfileVC1Advanced;

    if (IS_MFLD(driver_data)) {
        profile_list[i++] = VAProfileH263Baseline;
        profile_list[i++] = VAProfileJPEGBaseline;
    } else if (IS_MRST(driver_data))
        profile_list[i++] = VAProfileH263Baseline;
    profile_list[i++] = VAProfileH264ConstrainedBaseline;

    /* If the assert fails then PSB_MAX_PROFILES needs to be bigger */
    ASSERT(i <= PSB_MAX_PROFILES);
    *num_profiles = i;

    return VA_STATUS_SUCCESS;
}


VAStatus psb_QueryConfigEntrypoints(
    VADriverContextP ctx,
    VAProfile profile,
    VAEntrypoint  *entrypoint_list,    /* out */
    int *num_entrypoints        /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    format_vtable_p * format_vtable = ((profile < PSB_MAX_PROFILES)) ? driver_data->profile2Format[profile] : NULL;
    int entrypoints = 0;

    if (NULL == entrypoint_list) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == num_entrypoints) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (format_vtable)

    {
        int i;

        for (i = 0; i < PSB_MAX_ENTRYPOINTS; i++) {
            if (format_vtable[i]) {
                entrypoints++;
                *entrypoint_list++ = i;
            }
        }
    }

    /* If the assert fails then PSB_MAX_ENTRYPOINTS needs to be bigger */
    ASSERT(entrypoints <= PSB_MAX_ENTRYPOINTS);

    if (0 == entrypoints) {
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }

    *num_entrypoints = entrypoints;
    return VA_STATUS_SUCCESS;
}

/*
 * Figure out if we should return VA_STATUS_ERROR_UNSUPPORTED_PROFILE
 * or VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT
 */
static VAStatus psb__error_unsupported_profile_entrypoint(psb_driver_data_p driver_data, VAProfile profile, VAEntrypoint entrypoint)
{
    format_vtable_p * format_vtable = ((profile < PSB_MAX_PROFILES)) ? driver_data->profile2Format[profile] : NULL;

    /* Does the driver support _any_ entrypoint for this profile? */
    if (format_vtable) {
        int i;

        for (i = 0; i < PSB_MAX_ENTRYPOINTS; i++) {
            if (format_vtable[i]) {
                /* There is an entrypoint, so the profile is supported */
                return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
            }
        }
    }
    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

VAStatus psb_GetConfigAttributes(
    VADriverContextP ctx,
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,    /* in/out */
    int num_attribs
)
{
    INIT_DRIVER_DATA
    INIT_FORMAT_VTABLE
    int i;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    if (NULL == format_vtable) {
        return psb__error_unsupported_profile_entrypoint(driver_data, profile, entrypoint);
    }
    if (NULL == attrib_list) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (num_attribs <= 0) {
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    }

    /* Generic attributes */
    for (i = 0; i < num_attribs; i++) {
        switch (attrib_list[i].type) {
        case VAConfigAttribRTFormat:
            attrib_list[i].value = VA_RT_FORMAT_YUV420;
            if (entrypoint == VAEntrypointEncPicture)
                attrib_list[i].value |= VA_RT_FORMAT_YUV422;
            break;

        default:
            attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
            break;
        }
    }
    /* format specific attributes */
    format_vtable->queryConfigAttributes(profile, entrypoint, attrib_list, num_attribs);

    return VA_STATUS_SUCCESS;
}

static VAStatus psb__update_attribute(object_config_p obj_config, VAConfigAttrib *attrib)
{
    int i;
    /* Check existing attributes */
    for (i = 0; i < obj_config->attrib_count; i++) {
        if (obj_config->attrib_list[i].type == attrib->type) {
            /* Update existing attribute */
            obj_config->attrib_list[i].value = attrib->value;
            return VA_STATUS_SUCCESS;
        }
    }
    if (obj_config->attrib_count < PSB_MAX_CONFIG_ATTRIBUTES) {
        i = obj_config->attrib_count;
        obj_config->attrib_list[i].type = attrib->type;
        obj_config->attrib_list[i].value = attrib->value;
        obj_config->attrib_count++;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

static VAStatus psb__validate_config(object_config_p obj_config)
{
    int i;
    /* Check all attributes */
    for (i = 0; i < obj_config->attrib_count; i++) {
        switch (obj_config->attrib_list[i].type) {
        case VAConfigAttribRTFormat:
            if (!(obj_config->attrib_list[i].value == VA_RT_FORMAT_YUV420
                  || (obj_config->attrib_list[i].value == VA_RT_FORMAT_YUV422 &&
                      obj_config->entrypoint == VAEntrypointEncPicture))) {
                return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
            }
            break;

        default:
            /*
             * Ignore unknown attributes here, it
             * may be format specific.
             */
            break;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus psb_CreateConfig(
    VADriverContextP ctx,
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs,
    VAConfigID *config_id        /* out */
)
{
    INIT_DRIVER_DATA
    INIT_FORMAT_VTABLE
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int configID;
    object_config_p obj_config;
    int i;

    /*echo 8 > /sys/module/pvrsrvkm/parameters/no_ec will disable error concealment*/
    if ((profile == VAProfileH264ConstrainedBaseline) && (VAEntrypointVLD == entrypoint)) {
        char ec_disable[2];
        FILE *ec_fp = fopen("/sys/module/pvrsrvkm/parameters/no_ec", "r");
        if (ec_fp) {
            if (fgets(ec_disable, 2, ec_fp) != NULL) {
                /* force profile to VAProfileH264High */
                if (strcmp(ec_disable, "8") == 0) {
                    psb__information_message("disabled error concealment by setting profile to VAProfileH264High\n");
                    profile = VAProfileH264High;
                }
            }
            fclose(ec_fp);
        }
    }

    if (num_attribs < 0) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
    }

    if (NULL == format_vtable) {
        vaStatus = psb__error_unsupported_profile_entrypoint(driver_data, profile, entrypoint);
    }

    if (VA_STATUS_SUCCESS != vaStatus) {
        return vaStatus;
    }

    configID = object_heap_allocate(&driver_data->config_heap);
    obj_config = CONFIG(configID);
    if (NULL == obj_config) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }
    MEMSET_OBJECT(obj_config, struct object_config_s);

    obj_config->profile = profile;
    obj_config->format_vtable = format_vtable;
    obj_config->entrypoint = entrypoint;
    obj_config->attrib_list[0].type = VAConfigAttribRTFormat;
    obj_config->attrib_list[0].value = VA_RT_FORMAT_YUV420;
    obj_config->attrib_count = 1;

    for (i = 0; i < num_attribs; i++) {
        if (attrib_list[i].type < VAConfigAttribRTFormat || attrib_list[i].type > VAConfigAttribRateControl)
            return VA_STATUS_ERROR_ATTR_NOT_SUPPORTED;

        vaStatus = psb__update_attribute(obj_config, &(attrib_list[i]));
        if (VA_STATUS_SUCCESS != vaStatus) {
            break;
        }
    }

    if (VA_STATUS_SUCCESS == vaStatus) {
        vaStatus = psb__validate_config(obj_config);
    }

    if (VA_STATUS_SUCCESS == vaStatus) {
        vaStatus = format_vtable->validateConfig(obj_config);
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        object_heap_free(&driver_data->config_heap, (object_base_p) obj_config);
    } else {
        *config_id = configID;
    }

    /* only VAProfileH264ConstrainedBaseline profile enable error concealment*/
    if ((getenv("PSB_VIDEO_NOEC") == NULL) && (profile == VAProfileH264ConstrainedBaseline)) {
        psb__information_message("profile is VAProfileH264ConstrainedBaseline, error concealment is enabled. \n");
        driver_data->ec_enabled = 1;
    } else {
        driver_data->ec_enabled = 0;
    }
    return vaStatus;
}

VAStatus psb_DestroyConfig(
    VADriverContextP ctx,
    VAConfigID config_id
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;

    obj_config = CONFIG(config_id);
    if (NULL == obj_config) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        DEBUG_FAILURE;
        return vaStatus;
    }

    object_heap_free(&driver_data->config_heap, (object_base_p) obj_config);
    return vaStatus;
}

VAStatus psb_QueryConfigAttributes(
    VADriverContextP ctx,
    VAConfigID config_id,
    VAProfile *profile,        /* out */
    VAEntrypoint *entrypoint,     /* out */
    VAConfigAttrib *attrib_list,    /* out */
    int *num_attribs        /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int i;

    if (NULL == profile) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == entrypoint) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == attrib_list) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == num_attribs) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    obj_config = CONFIG(config_id);
    if (NULL == obj_config) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        DEBUG_FAILURE;
        return vaStatus;
    }

    *profile = obj_config->profile;
    *entrypoint = obj_config->entrypoint;
    *num_attribs =  obj_config->attrib_count;
    for (i = 0; i < obj_config->attrib_count; i++) {
        attrib_list[i] = obj_config->attrib_list[i];
    }

    return vaStatus;
}

static void psb__destroy_surface(psb_driver_data_p driver_data, object_surface_p obj_surface)
{
    if (NULL != obj_surface) {
        /* delete subpicture association */
        psb_SurfaceDeassociateSubpict(driver_data, obj_surface);

        psb_surface_destroy(obj_surface->psb_surface);

        if (obj_surface->psb_surface_rotate) {
            psb_surface_destroy(obj_surface->psb_surface_rotate);
        }

        free(obj_surface->psb_surface);
        object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
    }
}

static
VAStatus psb__checkSurfaceDimensions(psb_driver_data_p driver_data, int width, int height)
{
    if (driver_data->video_sd_disabled) {
        return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
    }
    if ((width <= 0) || (width > 5120) || (height <= 0) || (height > 5120)) {
        return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
    }
    if (driver_data->video_hd_disabled) {
        if ((width > 1024) || (height > 576)) {
            return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
        }
    }

    return VA_STATUS_SUCCESS;
}


VAStatus psb_CreateSurfaces(
    VADriverContextP ctx,
    int width,
    int height,
    int format,
    int num_surfaces,
    VASurfaceID *surface_list        /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int i, height_origin, buffer_stride = 0;
    unsigned long fourcc;

    if (num_surfaces <= 0) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == surface_list) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    /* We only support one format */
    if ((VA_RT_FORMAT_YUV420 != format)
        && (VA_RT_FORMAT_YUV422 != format)) {
        vaStatus = VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = psb__checkSurfaceDimensions(driver_data, width, height);
    if (VA_STATUS_SUCCESS != vaStatus) {
        DEBUG_FAILURE;
        return vaStatus;
    }

    /* Adjust height to be a multiple of 32 (height of macroblock in interlaced mode) */
    height_origin = height;
    height = (height + 0x1f) & ~0x1f;

    for (i = 0; i < num_surfaces; i++) {
        int surfaceID;
        object_surface_p obj_surface;
        psb_surface_p psb_surface;

        surfaceID = object_heap_allocate(&driver_data->surface_heap);
        obj_surface = SURFACE(surfaceID);
        if (NULL == obj_surface) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            DEBUG_FAILURE;
            break;
        }
        MEMSET_OBJECT(obj_surface, struct object_surface_s);

        obj_surface->surface_id = surfaceID;
        surface_list[i] = surfaceID;
        obj_surface->context_id = -1;
        obj_surface->width = width;
        obj_surface->height = height;
        obj_surface->width_r = width;
        obj_surface->height_r = height;
        obj_surface->height_origin = height_origin;

        psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));
        if (NULL == psb_surface) {
            object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
            obj_surface->surface_id = VA_INVALID_SURFACE;

            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;

            DEBUG_FAILURE;
            break;
        }

        switch (format) {
        case VA_RT_FORMAT_YUV422:
            fourcc = VA_FOURCC_YV16;
            break;
        case VA_RT_FORMAT_YUV420:
        default:
            fourcc = VA_FOURCC_NV12;
            break;
        }

        vaStatus = psb_surface_create(driver_data, width, height, fourcc,
                                      (VA_RT_FORMAT_PROTECTED & format), psb_surface
                                     );

        if (VA_STATUS_SUCCESS != vaStatus) {
            free(psb_surface);
            object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
            obj_surface->surface_id = VA_INVALID_SURFACE;

            DEBUG_FAILURE;
            break;
        }
        buffer_stride = psb_surface->stride;
        /* by default, surface fourcc is NV12 */
        memset(psb_surface->extra_info, 0, sizeof(psb_surface->extra_info));
        psb_surface->extra_info[4] = fourcc;

        obj_surface->psb_surface = psb_surface;

        /* Allocate alternative output surface */
        if (driver_data->rotate != VA_ROTATION_NONE) {
            psb__information_message("Try to allocate surface for alternative rotate output\n");
            psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));
            if (NULL == psb_surface) {
                psb_surface_destroy(obj_surface->psb_surface);
                obj_surface->surface_id = VA_INVALID_SURFACE;
                /* object_heap_free( &driver_data->surface_heap, (object_base_p) obj_surface); */

                vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;

                DEBUG_FAILURE;
                break;
            }

            if (driver_data->rotate == VA_ROTATION_180)
                vaStatus = psb_surface_create(driver_data, width, height, VA_FOURCC_NV12,
                                              (VA_RT_FORMAT_PROTECTED & format), psb_surface);
            else {
                vaStatus = psb_surface_create(driver_data, height_origin, ((width + 0x1f) & ~0x1f), VA_FOURCC_NV12,
                                              (VA_RT_FORMAT_PROTECTED & format), psb_surface
                                             );
                obj_surface->width_r = height_origin;
                obj_surface->height_r = ((width + 0x1f) & ~0x1f);
            }
            if (VA_STATUS_SUCCESS != vaStatus) {
                free(psb_surface);
                object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
                obj_surface->surface_id = VA_INVALID_SURFACE;
                psb_surface_destroy(obj_surface->psb_surface);

                DEBUG_FAILURE;
                break;
            }
            /* by default, surface fourcc is NV12 */
            memset(psb_surface->extra_info, 0, sizeof(psb_surface->extra_info));
            psb_surface->extra_info[4] = VA_FOURCC_NV12;
            psb_surface->extra_info[5] = driver_data->rotate;

            obj_surface->psb_surface_rotate = psb_surface;
        }
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        /* surface_list[i-1] was the last successful allocation */
        for (; i--;) {
            object_surface_p obj_surface = SURFACE(surface_list[i]);
            psb__destroy_surface(driver_data, obj_surface);
            surface_list[i] = VA_INVALID_SURFACE;
        }
    }

    vaStatus = psb_register_video_bcd(ctx, width, height, buffer_stride, num_surfaces, surface_list);

    return vaStatus;
}


VAStatus psb_CreateSurfaceFromCIFrame(
    VADriverContextP ctx,
    unsigned long frame_id,
    VASurfaceID *surface        /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int surfaceID;
    object_surface_p obj_surface;
    psb_surface_p psb_surface;
    struct ci_frame_info frame_info;
    int ret = 0, fd = -1;
    char *camera_dev = NULL;

    if (IS_MRST(driver_data) == 0)
        return VA_STATUS_ERROR_UNKNOWN;

    camera_dev = getenv("PSB_VIDEO_CAMERA_DEVNAME");

    if (camera_dev)
        fd = open_device(camera_dev);
    else
        fd = open_device("/dev/video0");
    if (fd == -1)
        return  VA_STATUS_ERROR_UNKNOWN;

    frame_info.frame_id = frame_id;
    ret = ci_get_frame_info(fd, &frame_info);
    close_device(fd);

    if (ret != 0)
        return  VA_STATUS_ERROR_UNKNOWN;

    psb__information_message("CI Frame: id=0x%08x, %dx%d, stride=%d, offset=0x%08x, fourcc=0x%08x\n",
                             frame_info.frame_id, frame_info.width, frame_info.height,
                             frame_info.stride, frame_info.offset, frame_info.fourcc);

    if (frame_info.stride & 0x3f) {
        psb__error_message("CI Frame must be 64byte aligned!\n");
        /* return  VA_STATUS_ERROR_UNKNOWN; */
    }

    if (frame_info.fourcc != VA_FOURCC_NV12) {
        psb__error_message("CI Frame must be NV12 format!\n");
        return  VA_STATUS_ERROR_UNKNOWN;
    }
    if (frame_info.offset & 0xfff) {
        psb__error_message("CI Frame offset must be page aligned!\n");
        /* return  VA_STATUS_ERROR_UNKNOWN; */
    }

    /* all sanity check passed */
    surfaceID = object_heap_allocate(&driver_data->surface_heap);
    obj_surface = SURFACE(surfaceID);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
        return vaStatus;
    }

    MEMSET_OBJECT(obj_surface, struct object_surface_s);

    obj_surface->surface_id = surfaceID;
    *surface = surfaceID;
    obj_surface->context_id = -1;
    obj_surface->width = frame_info.width;
    obj_surface->height = frame_info.height;

    psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));
    if (NULL == psb_surface) {
        object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
        obj_surface->surface_id = VA_INVALID_SURFACE;

        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;

        DEBUG_FAILURE;

        object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
        obj_surface->surface_id = VA_INVALID_SURFACE;

        return vaStatus;
    }

    vaStatus = psb_surface_create_camera(driver_data, frame_info.width, frame_info.height,
                                         frame_info.stride, frame_info.stride * frame_info.height,
                                         psb_surface,
                                         0, /* not V4L2 */
                                         frame_info.offset);
    if (VA_STATUS_SUCCESS != vaStatus) {
        free(psb_surface);
        object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
        obj_surface->surface_id = VA_INVALID_SURFACE;

        DEBUG_FAILURE;

        return vaStatus;
    }

    memset(psb_surface->extra_info, 0, sizeof(psb_surface->extra_info));
    psb_surface->extra_info[4] = VA_FOURCC_NV12;

    obj_surface->psb_surface = psb_surface;

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        object_surface_p obj_surface = SURFACE(*surface);
        psb__destroy_surface(driver_data, obj_surface);
        *surface = VA_INVALID_SURFACE;
    }

    return vaStatus;
}

VAStatus psb_DestroySurfaces(
    VADriverContextP ctx,
    VASurfaceID *surface_list,
    int num_surfaces
)
{
    INIT_DRIVER_DATA
    int i;

    if (num_surfaces <= 0) {
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    }

    if (NULL == surface_list) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    if (driver_data->bcd_registered != 0)
        if (VA_STATUS_SUCCESS != psb_release_video_bcd(ctx))
            return VA_STATUS_ERROR_UNKNOWN;

    /* This is work around.
       Add sufficient delay for gfx to release surface pages,
       Avoid page leak message in TTM */
    usleep(1000*100);

    /* Make validation happy */
    for (i = 0; i < num_surfaces; i++) {
        object_surface_p obj_surface = SURFACE(surface_list[i]);
        if (obj_surface == NULL) {
            return VA_STATUS_ERROR_INVALID_SURFACE;
        }
        if (obj_surface->derived_imgcnt > 0) {
            psb__error_message("Some surface is deriving by images\n");
            return VA_STATUS_ERROR_OPERATION_FAILED;
        }
    }

    for (i = 0; i < num_surfaces; i++) {
        object_surface_p obj_surface = SURFACE(surface_list[i]);

        if (driver_data->cur_displaying_surface == surface_list[i]) {
            /* Surface is being displaying. Need to stop overlay here */
        }

        psb__destroy_surface(driver_data, obj_surface);
        surface_list[i] = VA_INVALID_SURFACE;
    }

    return VA_STATUS_SUCCESS;
}

int psb_new_context(psb_driver_data_p driver_data, int ctx_type)
{
    struct drm_lnc_video_getparam_arg arg;
    int ret = 0;

    arg.key = IMG_VIDEO_NEW_CONTEXT;
    arg.value = (uint64_t)((unsigned long) & ctx_type);
    ret = drmCommandWriteRead(driver_data->drm_fd, driver_data->getParamIoctlOffset,
                              &arg, sizeof(arg));
    if (ret != 0)
        psb__error_message("Set context %d failed\n", ctx_type);

    return ret;
}

int psb_rm_context(psb_driver_data_p driver_data)
{
    struct drm_lnc_video_getparam_arg arg;
    int tmp;
    int ret = 0;

    arg.key = IMG_VIDEO_RM_CONTEXT;
    arg.value = (uint64_t)((unsigned long) & tmp); /* value is ignored */
    ret = drmCommandWriteRead(driver_data->drm_fd, driver_data->getParamIoctlOffset,
                              &arg, sizeof(arg));
    if (ret != 0)
        psb__error_message("Remove context failed\n");

    return ret;
}

VAStatus psb_CreateContext(
    VADriverContextP ctx,
    VAConfigID config_id,
    int picture_width,
    int picture_height,
    int flag,
    VASurfaceID *render_targets,
    int num_render_targets,
    VAContextID *context        /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int cmdbuf_num, encode = 0;
    int i;

    if (num_render_targets <= 0) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (NULL == render_targets) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == context) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONTEXT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = psb__checkSurfaceDimensions(driver_data, picture_width, picture_height);
    if (VA_STATUS_SUCCESS != vaStatus) {
        DEBUG_FAILURE;
        return vaStatus;
    }

    obj_config = CONFIG(config_id);
    if (NULL == obj_config) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        DEBUG_FAILURE;
        return vaStatus;
    }

    int contextID = object_heap_allocate(&driver_data->context_heap);
    object_context_p obj_context = CONTEXT(contextID);
    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
        return vaStatus;
    }

    *context = contextID;

    MEMSET_OBJECT(obj_context, struct object_context_s);

    obj_context->driver_data = driver_data;
    obj_context->current_render_target = NULL;
    obj_context->is_oold = driver_data->is_oold;
    obj_context->rotate = driver_data->rotate;

    obj_context->context_id = contextID;
    obj_context->config_id = config_id;
    obj_context->picture_width = picture_width;
    obj_context->picture_height = picture_height;
    obj_context->num_render_targets = num_render_targets;
    obj_context->render_targets = (VASurfaceID *) calloc(1, num_render_targets * sizeof(VASurfaceID));
    if (obj_context->render_targets == NULL) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;

        object_heap_free(&driver_data->context_heap, (object_base_p) obj_context);

        return vaStatus;
    }

    /* allocate buffer points for vaRenderPicture */
    obj_context->num_buffers = 10;
    obj_context->buffer_list = (object_buffer_p *) calloc(1, sizeof(object_buffer_p) * obj_context->num_buffers);
    if (obj_context->buffer_list == NULL) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;

        free(obj_context->render_targets);
        object_heap_free(&driver_data->context_heap, (object_base_p) obj_context);

        return vaStatus;
    }

    memset(obj_context->buffers_unused, 0, sizeof(obj_context->buffers_unused));
    memset(obj_context->buffers_unused_count, 0, sizeof(obj_context->buffers_unused_count));
    memset(obj_context->buffers_unused_tail, 0, sizeof(obj_context->buffers_unused_tail));
    memset(obj_context->buffers_active, 0, sizeof(obj_context->buffers_active));

    if (obj_config->entrypoint == VAEntrypointEncSlice
        || obj_config->entrypoint == VAEntrypointEncPicture) {
        encode = 1;
        cmdbuf_num = LNC_MAX_CMDBUFS_ENCODE;
    } else
        cmdbuf_num = PSB_MAX_CMDBUFS;

    for (i = 0; i < num_render_targets; i++) {
        object_surface_p obj_surface = SURFACE(render_targets[i]);
        psb_surface_p psb_surface;
        unsigned char *p;
        int ret;

        if (NULL == obj_surface) {
            vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
            DEBUG_FAILURE;
            break;
        }

        psb_surface = obj_surface->psb_surface;

        /* Clear format specific surface info */
        obj_context->render_targets[i] = render_targets[i];
        obj_surface->context_id = contextID; /* Claim ownership of surface */
#if 0
        /* for decode, move the surface into |TT */
        if ((encode == 0) && /* decode */
            ((psb_surface->buf.pl_flags & DRM_PSB_FLAG_MEM_RAR) == 0)) /* surface not in RAR */
            psb_buffer_setstatus(&obj_surface->psb_surface->buf,
                                 WSBM_PL_FLAG_TT | WSBM_PL_FLAG_SHARED, DRM_PSB_FLAG_MEM_MMU);
#endif
    }

    obj_context->va_flags = flag;
    obj_context->format_vtable = obj_config->format_vtable;
    obj_context->format_data = NULL;

    if (VA_STATUS_SUCCESS == vaStatus) {
        vaStatus = obj_context->format_vtable->createContext(obj_context, obj_config);
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        obj_context->context_id = -1;
        obj_context->config_id = -1;
        obj_context->picture_width = 0;
        obj_context->picture_height = 0;
        free(obj_context->render_targets);
        free(obj_context->buffer_list);
        obj_context->num_buffers = 0;
        obj_context->render_targets = NULL;
        obj_context->num_render_targets = 0;
        obj_context->va_flags = 0;
        object_heap_free(&driver_data->context_heap, (object_base_p) obj_context);

        return vaStatus;
    }

    /* initialize cmdbuf */
    for (i = 0; i < LNC_MAX_CMDBUFS_ENCODE; i++) {
        obj_context->lnc_cmdbuf_list[i] = NULL;
    }

    for (i = 0; i < PNW_MAX_CMDBUFS_ENCODE; i++) {
        obj_context->pnw_cmdbuf_list[i] = NULL;
    }

    for (i = 0; i < PSB_MAX_CMDBUFS; i++) {
        obj_context->cmdbuf_list[i] = NULL;
    }

    for (i = 0; i < cmdbuf_num; i++) {
        void  *cmdbuf;

        if (encode) { /* Topaz encode context */
            if (IS_MFLD(obj_context->driver_data))
                cmdbuf = calloc(1, sizeof(struct pnw_cmdbuf_s));
            else
                cmdbuf = calloc(1, sizeof(struct lnc_cmdbuf_s));
        } else /* MSVDX decode context */
            cmdbuf =  calloc(1, sizeof(struct psb_cmdbuf_s));

        if (NULL == cmdbuf) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            DEBUG_FAILURE;
            break;
        }

        if (encode) { /* Topaz encode context */
            if (IS_MFLD(obj_context->driver_data))
                vaStatus = pnw_cmdbuf_create(obj_context, driver_data, (pnw_cmdbuf_p)cmdbuf);
            else
                vaStatus = lnc_cmdbuf_create(obj_context, driver_data, (lnc_cmdbuf_p)cmdbuf);
        } else /* MSVDX decode context */
            vaStatus = psb_cmdbuf_create(obj_context, driver_data, (psb_cmdbuf_p)cmdbuf);

        if (VA_STATUS_SUCCESS != vaStatus) {
            free(cmdbuf);
            DEBUG_FAILURE;
            break;
        }
        if (encode) {
            if (IS_MFLD(obj_context->driver_data))
                obj_context->pnw_cmdbuf_list[i] = (pnw_cmdbuf_p)cmdbuf;
            else
                obj_context->lnc_cmdbuf_list[i] = (lnc_cmdbuf_p)cmdbuf;
        } else
            obj_context->cmdbuf_list[i] = (psb_cmdbuf_p)cmdbuf;
    }
    obj_context->cmdbuf_current = -1;
    obj_context->cmdbuf = NULL;
    obj_context->lnc_cmdbuf = NULL;
    obj_context->pnw_cmdbuf = NULL;
    obj_context->frame_count = 0;
    obj_context->slice_count = 0;
    obj_context->msvdx_context = ((driver_data->msvdx_context_base & 0xff0000) >> 16) |
                                 ((contextID & 0xff000000) >> 16);

    obj_context->entry_point = obj_config->entrypoint;

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        if (cmdbuf_num > LNC_MAX_CMDBUFS_ENCODE)
            cmdbuf_num = LNC_MAX_CMDBUFS_ENCODE;
        for (i = 0; i < cmdbuf_num; i++) {
            if (obj_context->lnc_cmdbuf_list[i]) {
                lnc_cmdbuf_destroy(obj_context->lnc_cmdbuf_list[i]);
                free(obj_context->lnc_cmdbuf_list[i]);
                obj_context->lnc_cmdbuf_list[i] = NULL;
            }
            if (obj_context->pnw_cmdbuf_list[i]) {
                pnw_cmdbuf_destroy(obj_context->pnw_cmdbuf_list[i]);
                free(obj_context->pnw_cmdbuf_list[i]);
                obj_context->pnw_cmdbuf_list[i] = NULL;
            }
            if (obj_context->cmdbuf_list[i]) {
                psb_cmdbuf_destroy(obj_context->cmdbuf_list[i]);
                free(obj_context->cmdbuf_list[i]);
                obj_context->cmdbuf_list[i] = NULL;
            }
        }

        obj_context->cmdbuf = NULL;
        obj_context->lnc_cmdbuf = NULL;

        obj_context->context_id = -1;
        obj_context->config_id = -1;
        obj_context->picture_width = 0;
        obj_context->picture_height = 0;
        free(obj_context->render_targets);
        free(obj_context->buffer_list);
        obj_context->num_buffers = 0;
        obj_context->render_targets = NULL;
        obj_context->num_render_targets = 0;
        obj_context->va_flags = 0;
        object_heap_free(&driver_data->context_heap, (object_base_p) obj_context);
    } else
        lnc_ospm_start(driver_data, encode);

    psb_new_context(driver_data, (obj_config->profile << 8) | obj_config->entrypoint);

    return vaStatus;
}

static VAStatus psb__allocate_malloc_buffer(object_buffer_p obj_buffer, int size)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    obj_buffer->buffer_data = realloc(obj_buffer->buffer_data, size);
    if (NULL == obj_buffer->buffer_data) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
    }
    return vaStatus;
}

static VAStatus psb__unmap_buffer(object_buffer_p obj_buffer);

static VAStatus psb__allocate_BO_buffer(psb_driver_data_p driver_data, object_buffer_p obj_buffer, int size, void *data, VABufferType type)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    ASSERT(NULL == obj_buffer->buffer_data);

    if (obj_buffer->psb_buffer && (psb_bs_queued == obj_buffer->psb_buffer->status)) {
        psb__information_message("Abandoning BO for buffer %08x type %s\n", obj_buffer->base.id,
                                 buffer_type_to_string(obj_buffer->type));
        /* need to set psb_buffer aside and get another one */
        obj_buffer->psb_buffer->status = psb_bs_abandoned;
        obj_buffer->psb_buffer = NULL;
        obj_buffer->size = 0;
        obj_buffer->alloc_size = 0;
    }

    if (type == VAProtectedSliceDataBufferType) {
        if (obj_buffer->psb_buffer) {
            psb__information_message("RAR: old RAR slice buffer with RAR handle 0%08x, current RAR handle 0x%08x\n",
                                     obj_buffer->psb_buffer->rar_handle, (uint32_t)data);
            psb__information_message("RAR: force old RAR buffer destroy and new buffer re-allocation by set size=0\n");
            obj_buffer->alloc_size = 0;
        }
    }

    if (obj_buffer->alloc_size < size) {
        psb__information_message("Buffer size mismatch: Need %d, currently have %d\n", size, obj_buffer->alloc_size);
        if (obj_buffer->psb_buffer) {
            if (obj_buffer->buffer_data) {
                psb__unmap_buffer(obj_buffer);
            }
            psb_buffer_destroy(obj_buffer->psb_buffer);
            obj_buffer->alloc_size = 0;
        } else {
            obj_buffer->psb_buffer = (psb_buffer_p) calloc(1, sizeof(struct psb_buffer_s));
            if (NULL == obj_buffer->psb_buffer) {
                vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
                DEBUG_FAILURE;
            }
        }
        if (VA_STATUS_SUCCESS == vaStatus) {
            psb__information_message("Allocate new GPU buffers for vaCreateBuffer:type=%s,size=%d.\n",
                                     buffer_type_to_string(obj_buffer->type), size);

            size = (size + 0x7fff) & ~0x7fff; /* Round up */
            if (obj_buffer->type == VAImageBufferType) /* Xserver side PutSurface, Image/subpicture buffer
                                                        * should be shared between two process
                                                        */
                vaStatus = psb_buffer_create(driver_data, size, psb_bt_cpu_vpu_shared, obj_buffer->psb_buffer);
            else if (obj_buffer->type == VAProtectedSliceDataBufferType)
                vaStatus = psb_buffer_reference_rar(driver_data, (uint32_t)data, obj_buffer->psb_buffer);
            else
                vaStatus = psb_buffer_create(driver_data, size, psb_bt_cpu_vpu, obj_buffer->psb_buffer);
            if (VA_STATUS_SUCCESS != vaStatus) {
                free(obj_buffer->psb_buffer);
                obj_buffer->psb_buffer = NULL;
                DEBUG_FAILURE;
            } else {
                obj_buffer->alloc_size = size;
            }
        }
    }
    return vaStatus;
}

static VAStatus psb__map_buffer(object_buffer_p obj_buffer)
{
    if (obj_buffer->psb_buffer) {
        return psb_buffer_map(obj_buffer->psb_buffer, &obj_buffer->buffer_data);
    }
    return VA_STATUS_SUCCESS;
}

static VAStatus psb__unmap_buffer(object_buffer_p obj_buffer)
{
    if (obj_buffer->psb_buffer) {
        obj_buffer->buffer_data = NULL;
        return psb_buffer_unmap(obj_buffer->psb_buffer);
    }
    return VA_STATUS_SUCCESS;
}

static void psb__destroy_buffer(psb_driver_data_p driver_data, object_buffer_p obj_buffer)
{
    if (obj_buffer->psb_buffer) {
        if (obj_buffer->buffer_data) {
            psb__unmap_buffer(obj_buffer);
        }
        psb_buffer_destroy(obj_buffer->psb_buffer);
        free(obj_buffer->psb_buffer);
        obj_buffer->psb_buffer = NULL;
    }

    if (NULL != obj_buffer->buffer_data) {
        free(obj_buffer->buffer_data);
        obj_buffer->buffer_data = NULL;
        obj_buffer->size = 0;
    }

    object_heap_free(&driver_data->buffer_heap, (object_base_p) obj_buffer);
}

void psb__suspend_buffer(psb_driver_data_p driver_data, object_buffer_p obj_buffer)
{
    if (obj_buffer->context) {
        VABufferType type = obj_buffer->type;
        object_context_p obj_context = obj_buffer->context;

        /* Remove buffer from active list */
        *obj_buffer->pptr_prev_next = obj_buffer->ptr_next;

        /* Add buffer to tail of unused list */
        obj_buffer->ptr_next = NULL;
        obj_buffer->last_used = obj_context->frame_count;
        if (obj_context->buffers_unused_tail[type]) {
            obj_buffer->pptr_prev_next = &(obj_context->buffers_unused_tail[type]->ptr_next);
        } else {
            obj_buffer->pptr_prev_next = &(obj_context->buffers_unused[type]);
        }
        *obj_buffer->pptr_prev_next = obj_buffer;
        obj_context->buffers_unused_tail[type] = obj_buffer;
        obj_context->buffers_unused_count[type]++;

        psb__information_message("Adding buffer %08x type %s to unused list. unused count = %d\n", obj_buffer->base.id,
                                 buffer_type_to_string(obj_buffer->type), obj_context->buffers_unused_count[type]);

        object_heap_suspend_object((object_base_p) obj_buffer, 1); /* suspend */
        return;
    }

    if (obj_buffer->psb_buffer && (psb_bs_queued == obj_buffer->psb_buffer->status)) {
        /* need to set psb_buffer aside */
        obj_buffer->psb_buffer->status = psb_bs_abandoned;
        obj_buffer->psb_buffer = NULL;
    }

    psb__destroy_buffer(driver_data, obj_buffer);
}

static void psb__destroy_context(psb_driver_data_p driver_data, object_context_p obj_context)
{
    int encode, i;

    if (obj_context->entry_point == VAEntrypointEncSlice)
        encode = 1;
    else
        encode = 0;

    lnc_ospm_stop(driver_data, encode);

    obj_context->format_vtable->destroyContext(obj_context);

    for (i = 0; i < PSB_MAX_BUFFERTYPES; i++) {
        object_buffer_p obj_buffer;
        obj_buffer = obj_context->buffers_active[i];
        for (; obj_buffer; obj_buffer = obj_buffer->ptr_next) {
            psb__information_message("%s: destroying active buffer %08x\n", __FUNCTION__, obj_buffer->base.id);
            psb__destroy_buffer(driver_data, obj_buffer);
        }
        obj_buffer = obj_context->buffers_unused[i];
        for (; obj_buffer; obj_buffer = obj_buffer->ptr_next) {
            psb__information_message("%s: destroying unused buffer %08x\n", __FUNCTION__, obj_buffer->base.id);
            psb__destroy_buffer(driver_data, obj_buffer);
        }
        obj_context->buffers_unused_count[i] = 0;
    }

    for (i = 0; i < LNC_MAX_CMDBUFS_ENCODE; i++) {
        if (obj_context->lnc_cmdbuf_list[i]) {
            lnc_cmdbuf_destroy(obj_context->lnc_cmdbuf_list[i]);
            free(obj_context->lnc_cmdbuf_list[i]);
            obj_context->lnc_cmdbuf_list[i] = NULL;
        }
    }

    for (i = 0; i < PSB_MAX_CMDBUFS; i++) {
        if (obj_context->cmdbuf_list[i]) {
            psb_cmdbuf_destroy(obj_context->cmdbuf_list[i]);
            free(obj_context->cmdbuf_list[i]);
            obj_context->cmdbuf_list[i] = NULL;
        }
    }
    obj_context->cmdbuf = NULL;
    obj_context->lnc_cmdbuf = NULL;

    obj_context->context_id = -1;
    obj_context->config_id = -1;
    obj_context->picture_width = 0;
    obj_context->picture_height = 0;
    if (obj_context->render_targets)
        free(obj_context->render_targets);
    obj_context->render_targets = NULL;
    obj_context->num_render_targets = 0;
    obj_context->va_flags = 0;

    obj_context->current_render_target = NULL;
    if (obj_context->buffer_list)
        free(obj_context->buffer_list);
    obj_context->num_buffers = 0;

    object_heap_free(&driver_data->context_heap, (object_base_p) obj_context);

    psb_rm_context(driver_data);
}

VAStatus psb_DestroyContext(
    VADriverContextP ctx,
    VAContextID context
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context = CONTEXT(context);
    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONTEXT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    psb__destroy_context(driver_data, obj_context);

    return vaStatus;
}

VAStatus psb__CreateBuffer(
    psb_driver_data_p driver_data,
    object_context_p obj_context,       /* in */
    VABufferType type,  /* in */
    unsigned int size,          /* in */
    unsigned int num_elements, /* in */
    void *data,         /* in */
    VABufferID *buf_desc    /* out */
)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int bufferID;
    object_buffer_p obj_buffer = obj_context ? obj_context->buffers_unused[type] : NULL;
    int unused_count = obj_context ? obj_context->buffers_unused_count[type] : 0;


    /*
     * Buffer Management
     * For each buffer type, maintain
     *   - a LRU sorted list of unused buffers
     *   - a list of active buffers
     * We only create a new buffer when
     *   - no unused buffers are available
     *   - the last unused buffer is still queued
     *   - the last unused buffer was used very recently and may still be fenced
     *      - used recently is defined as within the current frame_count (subject to tweaks)
     *
     * The buffer that is returned will be moved to the list of active buffers
     *   - vaDestroyBuffer and vaRenderPicture will move the active buffer back to the list of unused buffers
    */
    psb__information_message("Requesting buffer creation, size=%d,elements=%d,type=%s\n", size, num_elements,
                             buffer_type_to_string(type));

    if ((type == VAProtectedSliceDataBufferType) && (data == NULL)) {
        psb__error_message("RAR: Create protected slice buffer, but RAR handle is NULL\n");
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE ;
    }

    if (obj_buffer && obj_buffer->psb_buffer) {
        if (psb_bs_queued == obj_buffer->psb_buffer->status) {
            /* Buffer is still queued, allocate new buffer instead */
            psb__information_message("Skipping idle buffer %08x, still queued\n", obj_buffer->base.id);
            obj_buffer = NULL;
        } else if ((obj_buffer->last_used == obj_context->frame_count) && (unused_count < MAX_UNUSED_BUFFERS)) {
            /* Buffer was used for this frame, allocate new buffer instead */
            psb__information_message("Skipping idle buffer %08x, recently used. Unused = %d\n", obj_buffer->base.id, unused_count);
            obj_buffer = NULL;
        }
    }

    if (obj_buffer) {
        bufferID = obj_buffer->base.id;
        psb__information_message("Reusing buffer %08x type %s from unused list. Unused = %d\n", bufferID,
                                 buffer_type_to_string(type), unused_count);

        /* Remove from unused list */
        obj_context->buffers_unused[type] = obj_buffer->ptr_next;
        if (obj_context->buffers_unused[type]) {
            obj_context->buffers_unused[type]->pptr_prev_next = &(obj_context->buffers_unused[type]);
            ASSERT(obj_context->buffers_unused_tail[type] != obj_buffer);
        } else {
            ASSERT(obj_context->buffers_unused_tail[type] == obj_buffer);
            obj_context->buffers_unused_tail[type] = 0;
        }
        obj_context->buffers_unused_count[type]--;

        object_heap_suspend_object((object_base_p)obj_buffer, 0); /* Make BufferID valid again */
        ASSERT(type == obj_buffer->type);
        ASSERT(obj_context == obj_buffer->context);
    } else {
        bufferID = object_heap_allocate(&driver_data->buffer_heap);
        obj_buffer = BUFFER(bufferID);
        if (NULL == obj_buffer) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            DEBUG_FAILURE;
            return vaStatus;
        }

        MEMSET_OBJECT(obj_buffer, struct object_buffer_s);

        psb__information_message("Allocating new buffer %08x type %s.\n", bufferID, buffer_type_to_string(type));
        obj_buffer->type = type;
        obj_buffer->buffer_data = NULL;
        obj_buffer->psb_buffer = NULL;
        obj_buffer->size = 0;
        obj_buffer->max_num_elements = 0;
        obj_buffer->alloc_size = 0;
        obj_buffer->context = obj_context;
    }
    if (obj_context) {
        /* Add to front of active list */
        obj_buffer->ptr_next = obj_context->buffers_active[type];
        if (obj_buffer->ptr_next) {
            obj_buffer->ptr_next->pptr_prev_next = &(obj_buffer->ptr_next);
        }
        obj_buffer->pptr_prev_next = &(obj_context->buffers_active[type]);
        *obj_buffer->pptr_prev_next = obj_buffer;
    }

    switch (obj_buffer->type) {
    case VABitPlaneBufferType:
    case VASliceDataBufferType:
    case VAResidualDataBufferType:
    case VAImageBufferType:
    case VASliceGroupMapBufferType:
    case VAEncCodedBufferType:
    case VAProtectedSliceDataBufferType:
        vaStatus = psb__allocate_BO_buffer(driver_data, obj_buffer, size * num_elements, data, obj_buffer->type);
        DEBUG_FAILURE;
        break;
    case VAPictureParameterBufferType:
    case VAIQMatrixBufferType:
    case VASliceParameterBufferType:
    case VAMacroblockParameterBufferType:
    case VADeblockingParameterBufferType:
    case VAEncSequenceParameterBufferType:
    case VAEncPictureParameterBufferType:
    case VAEncSliceParameterBufferType:
    case VAQMatrixBufferType:
    case VAEncMiscParameterBufferType:
        psb__information_message("Allocate new malloc buffers for vaCreateBuffer:type=%s,size=%d, buffer_data=%p.\n",
                                 buffer_type_to_string(type), size, obj_buffer->buffer_data);
        vaStatus = psb__allocate_malloc_buffer(obj_buffer, size * num_elements);
        DEBUG_FAILURE;
        break;

    default:
        vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
        DEBUG_FAILURE;
        break;;
    }

    if (VA_STATUS_SUCCESS == vaStatus) {
        obj_buffer->size = size;
        obj_buffer->max_num_elements = num_elements;
        obj_buffer->num_elements = num_elements;
        if (data && (obj_buffer->type != VAProtectedSliceDataBufferType)) {
            vaStatus = psb__map_buffer(obj_buffer);
            if (VA_STATUS_SUCCESS == vaStatus) {
                memcpy(obj_buffer->buffer_data, data, size * num_elements);

                psb__unmap_buffer(obj_buffer);
            }
        }
    }
    if (VA_STATUS_SUCCESS == vaStatus) {
        *buf_desc = bufferID;
    } else {
        psb__destroy_buffer(driver_data, obj_buffer);
    }

    return vaStatus;
}

VAStatus psb_CreateBuffer(
    VADriverContextP ctx,
    VAContextID context,        /* in */
    VABufferType type,  /* in */
    unsigned int size,          /* in */
    unsigned int num_elements, /* in */
    void *data,         /* in */
    VABufferID *buf_desc    /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    if (num_elements <= 0) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    switch (type) {
    case VABitPlaneBufferType:
    case VASliceDataBufferType:
    case VAProtectedSliceDataBufferType:
    case VAResidualDataBufferType:
    case VASliceGroupMapBufferType:
    case VAPictureParameterBufferType:
    case VAIQMatrixBufferType:
    case VASliceParameterBufferType:
    case VAMacroblockParameterBufferType:
    case VADeblockingParameterBufferType:
    case VAEncCodedBufferType:
    case VAEncSequenceParameterBufferType:
    case VAEncPictureParameterBufferType:
    case VAEncSliceParameterBufferType:
    case VAQMatrixBufferType:
    case VAEncMiscParameterBufferType:
        break;

    default:
        vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    object_context_p obj_context = CONTEXT(context);
    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONTEXT;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == buf_desc) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    return psb__CreateBuffer(driver_data, obj_context, type, size, num_elements, data, buf_desc);
}


VAStatus psb_BufferInfo(
    VADriverContextP ctx,
    VAContextID context,        /* in */
    VABufferID buf_id,  /* in */
    VABufferType *type, /* out */
    unsigned int *size,         /* out */
    unsigned int *num_elements /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    object_buffer_p obj_buffer = BUFFER(buf_id);
    if (NULL == obj_buffer) {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    *type = obj_buffer->type;
    *size = obj_buffer->size;
    *num_elements = obj_buffer->num_elements;
    return VA_STATUS_SUCCESS;
}


VAStatus psb_BufferSetNumElements(
    VADriverContextP ctx,
    VABufferID buf_id,    /* in */
    unsigned int num_elements    /* in */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    if (NULL == obj_buffer) {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if ((num_elements <= 0) || (num_elements > obj_buffer->max_num_elements)) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (VA_STATUS_SUCCESS == vaStatus) {
        obj_buffer->num_elements = num_elements;
    }

    return vaStatus;
}

VAStatus psb_MapBuffer(
    VADriverContextP ctx,
    VABufferID buf_id,    /* in */
    void **pbuf         /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    if (NULL == obj_buffer) {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        DEBUG_FAILURE;
        return vaStatus;
    }


    if (NULL == pbuf) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = psb__map_buffer(obj_buffer);
    if (VA_STATUS_SUCCESS != vaStatus) {
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (NULL != obj_buffer->buffer_data) {
        *pbuf = obj_buffer->buffer_data;

        /* specifically for Topaz encode
         * write validate coded data offset in CodedBuffer
         */
        if (obj_buffer->type == VAEncCodedBufferType)
            psb_codedbuf_map_mangle(ctx, obj_buffer, pbuf);
        /* *(IMG_UINT32 *)((void *)obj_buffer->buffer_data + 4) = 16; */
    } else {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    return vaStatus;
}

VAStatus psb_UnmapBuffer(
    VADriverContextP ctx,
    VABufferID buf_id    /* in */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    if (NULL == obj_buffer) {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = psb__unmap_buffer(obj_buffer);

    return vaStatus;
}


VAStatus psb_DestroyBuffer(
    VADriverContextP ctx,
    VABufferID buffer_id
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buffer_id);

    if (NULL == obj_buffer) {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    psb__suspend_buffer(driver_data, obj_buffer);
    return vaStatus;
}

static VAStatus psb__create_surface_rotation(VADriverContextP ctx, object_surface_p obj_surface, int protected)
{
    INIT_DRIVER_DATA
    int width, height;
    psb_surface_p psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    psb__information_message("Try to allocate surface for alternative rotate output\n");

    if (NULL == psb_surface) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
        return vaStatus;
    }

    width = obj_surface->width;
    height = obj_surface->height;

    if (driver_data->rotate == VA_ROTATION_180) {
        vaStatus = psb_surface_create(driver_data, width, height, VA_FOURCC_NV12,
                                      protected, psb_surface);
        obj_surface->width_r = width;
        obj_surface->height_r = height;
    } else {
        vaStatus = psb_surface_create(driver_data, obj_surface->height_origin, ((width + 0x1f) & ~0x1f), VA_FOURCC_NV12,
                                      protected, psb_surface
                                     );
        obj_surface->width_r = obj_surface->height_origin;
        obj_surface->height_r = ((width + 0x1f) & ~0x1f);
    }
    if (VA_STATUS_SUCCESS != vaStatus) {
        free(psb_surface);
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
        return vaStatus;
    }
    /* by default, surface fourcc is NV12 */
    memset(psb_surface->extra_info, 0, sizeof(psb_surface->extra_info));
    psb_surface->extra_info[4] = VA_FOURCC_NV12;
    psb_surface->extra_info[5] = driver_data->rotate;

    obj_surface->psb_surface_rotate = psb_surface;
    return vaStatus;
}

VAStatus psb_BeginPicture(
    VADriverContextP ctx,
    VAContextID context,
    VASurfaceID render_target
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;
    object_config_p obj_config;

    obj_context = CONTEXT(context);
    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONTEXT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    /* Must not be within BeginPicture / EndPicture already */
    ASSERT(obj_context->current_render_target == NULL);

    obj_surface = SURFACE(render_target);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    obj_context->current_render_surface_id = render_target;
    obj_context->current_render_target = obj_surface;
    obj_context->slice_count = 0;

    obj_config = CONFIG(obj_context->config_id);
    /* if the surface is decode render target, and in displaying */
    if (obj_config &&
        (obj_config->entrypoint != VAEntrypointEncSlice) &&
        (driver_data->cur_displaying_surface == render_target))
        psb__error_message("WARNING: rendering a displaying surface, may see tearing\n");

    if (VA_STATUS_SUCCESS == vaStatus) {
        vaStatus = obj_context->format_vtable->beginPicture(obj_context);
    }

    if (driver_data->xrandr_dirty & PSB_NEW_ROTATION) {
        int angle;
        angle = Rotation2Angle(driver_data->video_rotate) + Rotation2Angle(driver_data->mipi0_rotation);
        driver_data->local_rotation = Angle2Rotation(angle);
        angle = Rotation2Angle(driver_data->video_rotate) + Rotation2Angle(driver_data->hdmi_rotation);
        driver_data->extend_rotation = Angle2Rotation(angle);
#ifndef ANDROID
        if ((driver_data->mipi1_rotation != VA_ROTATION_NONE) ||
            ((driver_data->local_rotation != VA_ROTATION_NONE) &&
             (driver_data->extend_rotation != VA_ROTATION_NONE) &&
             (driver_data->local_rotation != driver_data->extend_rotation))) {
            driver_data->rotate = driver_data->video_rotate;
            /*fallback to texblit path*/
            driver_data->output_method = PSB_PUTSURFACE_CTEXTURE;
        } else {
            if (driver_data->output_method != PSB_PUTSURFACE_FORCE_CTEXTURE) {
                driver_data->output_method = PSB_PUTSURFACE_COVERLAY;
                driver_data->rotate = (driver_data->local_rotation == 0) ? driver_data->extend_rotation : driver_data->local_rotation;
            }
        }
#endif
        if (driver_data->rotate == VA_ROTATION_270)
            driver_data->rotate = 3; /* Match with hw definition */

        driver_data->xrandr_dirty &= ~PSB_NEW_ROTATION;
    }
    /* Create surface for rotation if needed */
    if (driver_data->rotate == VA_ROTATION_NONE && obj_surface->psb_surface_rotate) {
        psb_surface_destroy(obj_surface->psb_surface_rotate);
        obj_surface->psb_surface_rotate = NULL;
        obj_surface->width_r = obj_surface->width;
        obj_surface->height_r = obj_surface->height;
        obj_context->rotate = driver_data->rotate;
    } else if (driver_data->rotate != VA_ROTATION_NONE &&
               (!obj_surface->psb_surface_rotate ? 1 : (obj_surface->psb_surface_rotate->extra_info[5] != driver_data->rotate))) {
        if (!obj_surface->psb_surface_rotate) {
            psb__create_surface_rotation(ctx, obj_surface, obj_surface->psb_surface->buf.type == psb_bt_rar_surface);
        } else {
            psb_surface_destroy(obj_surface->psb_surface_rotate);
            free(obj_surface->psb_surface_rotate);
            psb__create_surface_rotation(ctx, obj_surface, obj_surface->psb_surface->buf.type == psb_bt_rar_surface);
        }
        obj_context->rotate = driver_data->rotate;
    }

    if (driver_data->is_oold &&  !obj_surface->psb_surface->in_loop_buf) {
        psb_surface_p psb_surface = obj_surface->psb_surface;

        psb_surface->in_loop_buf = calloc(1, sizeof(struct psb_buffer_s));
        if (NULL == psb_surface->in_loop_buf) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            DEBUG_FAILURE;
            return vaStatus;
        }

        /* FIXME: For RAR surface, need allocate RAR buffer  */
        vaStatus = psb_buffer_create(obj_context->driver_data,
                                     psb_surface->size,
                                     psb_bt_surface,
                                     psb_surface->in_loop_buf);
    } else if (!driver_data->is_oold && obj_surface->psb_surface->in_loop_buf) {
        psb_surface_p psb_surface = obj_surface->psb_surface;

        psb_buffer_destroy(psb_surface->in_loop_buf);
        free(psb_surface->in_loop_buf);
        psb_surface->in_loop_buf = NULL;
    }
    obj_context->is_oold = driver_data->is_oold;

    psb__information_message("---BeginPicture 0x%08x for frame %d --\n",
                             render_target, obj_context->frame_count);
#ifdef DEBUG_TRACE
    psb__trace_message("------Trace frame %d------\n", obj_context->frame_count);
#endif

    return vaStatus;
}

VAStatus psb_RenderPicture(
    VADriverContextP ctx,
    VAContextID context,
    VABufferID *buffers,
    int num_buffers
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_buffer_p *buffer_list;
    int i;

    obj_context = CONTEXT(context);
    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONTEXT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (num_buffers <= 0) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (NULL == buffers) {
        /* Don't crash on NULL pointers */
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        DEBUG_FAILURE;
        return vaStatus;
    }
    /* Must be within BeginPicture / EndPicture */
    ASSERT(obj_context->current_render_target != NULL);

    if (num_buffers > obj_context->num_buffers) {
        free(obj_context->buffer_list);

        obj_context->buffer_list = (object_buffer_p *) calloc(1, sizeof(object_buffer_p) * num_buffers);
        if (obj_context->buffer_list == NULL) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            obj_context->num_buffers = 0;
        }

        obj_context->num_buffers = num_buffers;
    }
    buffer_list = obj_context->buffer_list;

    if (VA_STATUS_SUCCESS == vaStatus) {
        /* Lookup buffer references */
        for (i = 0; i < num_buffers; i++) {
            object_buffer_p obj_buffer = BUFFER(buffers[i]);
            if (NULL == obj_buffer) {
                vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
                DEBUG_FAILURE;
                return vaStatus;
            }
            buffer_list[i] = obj_buffer;
        }
    }

    if (VA_STATUS_SUCCESS == vaStatus) {
        vaStatus = obj_context->format_vtable->renderPicture(obj_context, buffer_list, num_buffers);
    }

    if (buffer_list) {
        /* Release buffers */
        for (i = 0; i < num_buffers; i++) {
            if (buffer_list[i]) {
                psb__suspend_buffer(driver_data, buffer_list[i]);
            }
        }
    }

    return vaStatus;
}

VAStatus psb_EndPicture(
    VADriverContextP ctx,
    VAContextID context
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus;
    object_context_p obj_context;

    obj_context = CONTEXT(context);
    if (NULL == obj_context) {
        vaStatus = VA_STATUS_ERROR_INVALID_CONTEXT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = obj_context->format_vtable->endPicture(obj_context);

    psb__information_message("---EndPicture for frame %d --\n", obj_context->frame_count);

    obj_context->current_render_target = NULL;
    obj_context->frame_count++;
#ifdef DEBUG_TRACE
    psb__trace_message("FrameCount = %03d\n", obj_context->frame_count);
    psb__information_message("FrameCount = %03d\n", obj_context->frame_count);
    psb__trace_message(NULL);
#endif

    return vaStatus;
}


static void psb__surface_usage(
    psb_driver_data_p driver_data,
    object_surface_p obj_surface,
    int *decode, int *encode, int *rc_enable
)
{
    object_context_p obj_context;
    object_config_p obj_config;
    VAEntrypoint tmp;
    unsigned int eRCmode;
    int i;


    *decode = 0;
    *encode = 0;
    *rc_enable = 0;

    obj_context = CONTEXT(obj_surface->context_id);
    if (NULL == obj_context) /* not associate with a context */
        return;

    obj_config = CONFIG(obj_context->config_id);
    if (NULL == obj_config) /* not have a validate context */
        return;

    tmp = obj_config->entrypoint;

    *encode = (tmp == VAEntrypointEncSlice) || (tmp == VAEntrypointEncPicture);
    *decode = (VAEntrypointVLD <= tmp) && (tmp <= VAEntrypointDeblocking);

    if (*encode) {
        for (i = 0; i < obj_config->attrib_count; i++) {
            if (obj_config->attrib_list[i].type == VAConfigAttribRateControl)
                break;
        }

        if (i >= obj_config->attrib_count)
            eRCmode = VA_RC_NONE;
        else
            eRCmode = obj_config->attrib_list[i].value;

        if (eRCmode == VA_RC_NONE)
            *rc_enable = 0;
        else
            *rc_enable = 1;
    }
}

VAStatus psb_SyncSurface(
    VADriverContextP ctx,
    VASurfaceID render_target
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;
    int decode = 0, encode = 0, rc_enable = 0;

    obj_surface = SURFACE(render_target);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    /* The cur_displaying_surface indicates the surface being displayed by overlay.
     * The diaplay_timestamp records the time point of put surface, which would
     * be set to zero while using texture blit.*/

    /* don't use mutex here for performance concern... */
    //pthread_mutex_lock(&output->output_mutex);
    if (render_target == driver_data->cur_displaying_surface)
        vaStatus = VA_STATUS_ERROR_SURFACE_IN_DISPLAYING;
    else if ((VA_INVALID_SURFACE != driver_data->cur_displaying_surface)    /* use overlay */
             && (render_target == driver_data->last_displaying_surface)) {  /* It's the last displaying surface*/
        object_surface_p cur_obj_surface = SURFACE(driver_data->cur_displaying_surface);
        /*  The flip operation on current displaying surface could be delayed to
         *  next VBlank and hadn't been finished yet. Then, the last displaying
         *  surface shouldn't be freed, because the hardware may not
         *  complete loading data of it. Any change of the last surface could
         *  have a impect on the scrren.*/
        if (NULL != cur_obj_surface) {
            while ((GetTickCount() - cur_obj_surface->display_timestamp) < PSB_MAX_FLIP_DELAY)
                usleep(PSB_MAX_FLIP_DELAY * 1000);
        }
    }
    //pthread_mutex_unlock(&output->output_mutex);

    if (vaStatus != VA_STATUS_ERROR_SURFACE_IN_DISPLAYING)
        vaStatus = psb_surface_sync(obj_surface->psb_surface);

    /* report any error of decode for Android */
    psb__surface_usage(driver_data, obj_surface, &decode, &encode, &rc_enable);

    if (decode && IS_MRST(driver_data)) {
        struct drm_lnc_video_getparam_arg arg;
        uint32_t ret, handle, fw_status = 0;
        handle = wsbmKBufHandle(wsbmKBuf(obj_surface->psb_surface->buf.drm_buf));
        arg.key = IMG_VIDEO_DECODE_STATUS;
        arg.arg = (uint64_t)((unsigned long) & handle);
        arg.value = (uint64_t)((unsigned long) & fw_status);
        ret = drmCommandWriteRead(driver_data->drm_fd, driver_data->getParamIoctlOffset,
                                  &arg, sizeof(arg));
        if (ret == 0) {
            if (fw_status != 0)
                vaStatus = VA_STATUS_ERROR_DECODING_ERROR;
        } else {
            psb__information_message("IMG_VIDEO_DECODE_STATUS ioctl return failed.\n");
            vaStatus = VA_STATUS_ERROR_UNKNOWN;
        }
    }
    DEBUG_FAILURE;
    return vaStatus;
}


VAStatus psb_QuerySurfaceStatus(
    VADriverContextP ctx,
    VASurfaceID render_target,
    VASurfaceStatus *status    /* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;
    VASurfaceStatus surface_status;
    int frame_skip = 0, encode = 0, decode = 0, rc_enable = 0;

    obj_surface = SURFACE(render_target);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }
    if (NULL == status) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = psb_surface_query_status(obj_surface->psb_surface, &surface_status);

    /* The cur_displaying_surface indicates the surface being displayed by overlay.
     * The diaplay_timestamp records the time point of put surface, which would
     * be set to zero while using texture blit.*/
    pthread_mutex_lock(&driver_data->output_mutex);
    if (render_target == driver_data->cur_displaying_surface)
        surface_status = VASurfaceDisplaying;
    else if ((VA_INVALID_SURFACE != driver_data->cur_displaying_surface)    /* use overlay */
             && (render_target == driver_data->last_displaying_surface)) {  /* It's the last displaying surface*/
        object_surface_p cur_obj_surface = SURFACE(driver_data->cur_displaying_surface);
        /*The flip operation on current displaying surface could be delayed to
         *  next VBlank and hadn't been finished yet. Then, the last displaying
         *  surface shouldn't be freed, because the hardware may not
         *  complete loading data of it. Any change of the last surface could
         *  have a impect on the scrren.*/
        if ((NULL != cur_obj_surface)
            && ((GetTickCount() - cur_obj_surface->display_timestamp) < PSB_MAX_FLIP_DELAY)) {
            surface_status = VASurfaceDisplaying;
        }
    }
    pthread_mutex_unlock(&driver_data->output_mutex);

    /* try to get frameskip flag for encode */
    psb__surface_usage(driver_data, obj_surface, &decode, &encode, &rc_enable);
    if (encode && rc_enable) {
        if (IS_MRST(driver_data))
            lnc_surface_get_frameskip(driver_data, obj_surface->psb_surface, &frame_skip);
        else
            pnw_surface_get_frameskip(driver_data, obj_surface->psb_surface, &frame_skip);

        if (frame_skip == 1)
            surface_status = surface_status | VASurfaceSkipped;
    }

    *status = surface_status;

    return vaStatus;
}

VAStatus psb_QuerySurfaceError(
    VADriverContextP ctx,
    VASurfaceID render_target,
    VAStatus error_status,
    void **error_info /*out*/
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;
    uint32_t i;

    obj_surface = SURFACE(render_target);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (!IS_MRST(driver_data) || (driver_data->ec_enabled == 0)) {
        psb__information_message("error concealment is not supported for this profile.\n");
        error_info = NULL;
        return VA_STATUS_ERROR_UNKNOWN;
    }

    if (error_status == VA_STATUS_ERROR_DECODING_ERROR) {
        drm_psb_msvdx_decode_status_t *decode_status = driver_data->msvdx_decode_status;
        struct drm_lnc_video_getparam_arg arg;
        uint32_t ret, handle;
        handle = wsbmKBufHandle(wsbmKBuf(obj_surface->psb_surface->buf.drm_buf));

        arg.key = IMG_VIDEO_MB_ERROR;
        arg.arg = (uint64_t)((unsigned long) & handle);
        arg.value = (uint64_t)decode_status;
        ret = drmCommandWriteRead(driver_data->drm_fd, driver_data->getParamIoctlOffset,
                                  &arg, sizeof(arg));

        if (decode_status->num_error_slice > MAX_MB_ERRORS) {
            psb__information_message("too much mb errors are reported.\n");
            return VA_STATUS_ERROR_UNKNOWN;
        }
        i = 0;
        for (i = 0; i < decode_status->num_error_slice; ++i) {
            driver_data->surface_mb_error[i].status = 1;
            driver_data->surface_mb_error[i].start_mb = decode_status->start_error_mb_list[i];
            driver_data->surface_mb_error[i].end_mb = decode_status->end_error_mb_list[i];
            driver_data->surface_mb_error[i].decode_error_type = decode_status->slice_missing_or_error[i];
        }
        driver_data->surface_mb_error[i].status = -1;
        *error_info = driver_data->surface_mb_error;

        return vaStatus;
    } else {
        error_info = NULL;
        return VA_STATUS_ERROR_UNKNOWN;
    }
}

VAStatus psb_LockSurface(
    VADriverContextP ctx,
    VASurfaceID surface,
    unsigned int *fourcc, /* following are output argument */
    unsigned int *luma_stride,
    unsigned int *chroma_u_stride,
    unsigned int *chroma_v_stride,
    unsigned int *luma_offset,
    unsigned int *chroma_u_offset,
    unsigned int *chroma_v_offset,
    unsigned int *buffer_name,
    void **buffer
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    void *surface_data;
    int ret;

    object_surface_p obj_surface = SURFACE(surface);
    psb_surface_p psb_surface;
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    psb_surface = obj_surface->psb_surface;
    if (buffer_name) {
        /* todo */
    }

    if (buffer) { /* map the surface buffer */
        uint32_t srf_buf_ofs = 0;
        ret = psb_buffer_map(&psb_surface->buf, &surface_data);
        if (ret) {
            *buffer = NULL;
            vaStatus = VA_STATUS_ERROR_UNKNOWN;
            DEBUG_FAILURE;
            return vaStatus;
        }
        srf_buf_ofs = psb_surface->buf.buffer_ofs;
        *buffer = surface_data + srf_buf_ofs;
    }

    *fourcc = VA_FOURCC_NV12;
    *luma_stride = psb_surface->stride;
    *chroma_u_stride = psb_surface->stride;
    *chroma_v_stride = psb_surface->stride;
    *luma_offset = 0;
    *chroma_u_offset = obj_surface->height * psb_surface->stride;
    *chroma_v_offset = obj_surface->height * psb_surface->stride + 1;

    return vaStatus;
}


VAStatus psb_UnlockSurface(
    VADriverContextP ctx,
    VASurfaceID surface
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    object_surface_p obj_surface = SURFACE(surface);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    psb_surface_p psb_surface = obj_surface->psb_surface;

    psb_buffer_unmap(&psb_surface->buf);

    return VA_STATUS_SUCCESS;
}

VAStatus psb_GetEGLClientBufferFromSurface(
    VADriverContextP ctx,
    VASurfaceID surface,
    void **buffer
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    object_surface_p obj_surface = SURFACE(surface);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    psb_surface_p psb_surface = obj_surface->psb_surface;
    *buffer = (void *)psb_surface->bc_buffer;

    return vaStatus;
}

VAStatus psb_CreateSurfaceFromV4L2Buf(
    VADriverContextP ctx,
    int v4l2_fd,         /* file descriptor of V4L2 device */
    struct v4l2_format *v4l2_fmt,       /* format of V4L2 */
    struct v4l2_buffer *v4l2_buf,       /* V4L2 buffer */
    VASurfaceID *surface        /* out */
)
{
    INIT_DRIVER_DATA;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int surfaceID;
    object_surface_p obj_surface;
    psb_surface_p psb_surface;
    int width, height, buf_stride, buf_offset, size;
    unsigned long *user_ptr = NULL;

    if (IS_MRST(driver_data) == 0 && IS_MFLD(driver_data) == 0) {
        psb__error_message("CreateSurfaceFromV4L2Buf isn't supported on non-MRST platform\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }

    /* Todo:
     * sanity check if the v4l2 device on MRST is supported
     */
    if (V4L2_MEMORY_USERPTR == v4l2_buf->memory) {
        unsigned long tmp = (unsigned long)(v4l2_buf->m.userptr);

        if (tmp & 0xfff) {
            psb__error_message("The buffer address 0x%08x must be page aligned\n", tmp);
            return VA_STATUS_ERROR_UNKNOWN;
        }
    }

    surfaceID = object_heap_allocate(&driver_data->surface_heap);
    obj_surface = SURFACE(surfaceID);
    if (NULL == obj_surface) {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        DEBUG_FAILURE;
        return vaStatus;
    }
    MEMSET_OBJECT(obj_surface, struct object_surface_s);

    width = v4l2_fmt->fmt.pix.width;
    height = v4l2_fmt->fmt.pix.height;

    buf_stride = width; /* ? */
    buf_offset = v4l2_buf->m.offset;
    size = v4l2_buf->length;

    psb__information_message("Create Surface from V4L2 buffer: %dx%d, stride=%d, buffer offset=0x%08x, size=%d\n",
                             width, height, buf_stride, buf_offset, size);

    obj_surface->surface_id = surfaceID;
    *surface = surfaceID;
    obj_surface->context_id = -1;
    obj_surface->width = width;
    obj_surface->height = height;
    obj_surface->subpictures = NULL;
    obj_surface->subpic_count = 0;
    obj_surface->derived_imgcnt = 0;
    obj_surface->display_timestamp = 0;

    psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));
    if (NULL == psb_surface) {
        object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
        obj_surface->surface_id = VA_INVALID_SURFACE;

        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;

        DEBUG_FAILURE;

        return vaStatus;
    }

    /* current assume it is NV12 */
    if (IS_MRST(driver_data))
        vaStatus = psb_surface_create_camera(driver_data, width, height, buf_stride, size, psb_surface, 1, buf_offset);
    else {
        if (V4L2_MEMORY_USERPTR == v4l2_buf->memory)
            user_ptr = (unsigned long *)(v4l2_buf->m.userptr);
        else {
            user_ptr = mmap(NULL /* start anywhere */ ,
                            v4l2_buf->length,
                            PROT_READ ,
                            MAP_SHARED /* recommended */ ,
                            v4l2_fd, v4l2_buf->m.offset);
        }

        if (NULL != user_ptr && MAP_FAILED != user_ptr)
            vaStatus = psb_surface_create_camera_from_ub(driver_data, width, height,
                       buf_stride, size, psb_surface, 1, buf_offset, user_ptr);
        else {
            DEBUG_FAILURE;
            vaStatus = VA_STATUS_ERROR_UNKNOWN;
        }
    }

    if (VA_STATUS_SUCCESS != vaStatus) {
        free(psb_surface);
        object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
        obj_surface->surface_id = VA_INVALID_SURFACE;

        DEBUG_FAILURE;

        return vaStatus;
    }

    memset(psb_surface->extra_info, 0, sizeof(psb_surface->extra_info));
    psb_surface->extra_info[4] = VA_FOURCC_NV12; /* temp treat is as IYUV */

    obj_surface->psb_surface = psb_surface;

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        object_surface_p obj_surface = SURFACE(*surface);
        psb__destroy_surface(driver_data, obj_surface);
        *surface = VA_INVALID_SURFACE;
    }

    return vaStatus;
}


VAStatus psb_CreateSurfacesForUserPtr(
    VADriverContextP ctx,
    int width,
    int height,
    int format,
    int num_surfaces,
    VASurfaceID *surface_list,       /* out */
    unsigned size, /* total buffer size need to be allocated */
    unsigned int fourcc, /* expected fourcc */
    unsigned int luma_stride, /* luma stride, could be width aligned with a special value */
    unsigned int chroma_u_stride, /* chroma stride */
    unsigned int chroma_v_stride,
    unsigned int luma_offset, /* could be 0 */
    unsigned int chroma_u_offset, /* UV offset from the beginning of the memory */
    unsigned int chroma_v_offset
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int i, height_origin;
    unsigned long buffer_stride;

    psb__information_message("Create surface: width %d, height %d, format 0x%08x"
                             "\n\t\t\t\t\tnum_surface %d, buffer size %d, fourcc 0x%08x"
                             "\n\t\t\t\t\tluma_stride %d, chroma u stride %d, chroma v stride %d"
                             "\n\t\t\t\t\tluma_offset %d, chroma u offset %d, chroma v offset %d\n",
                             width, height, format,
                             num_surfaces, size, fourcc,
                             luma_stride, chroma_u_stride, chroma_v_stride,
                             luma_offset, chroma_u_offset, chroma_v_offset);

    if (num_surfaces <= 0) {
        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    if (NULL == surface_list) {
        vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
        DEBUG_FAILURE;
        return vaStatus;
    }

    /* We only support one format */
    if ((VA_RT_FORMAT_YUV420 != format)
        && (VA_RT_FORMAT_YUV422 != format)) {
        vaStatus = VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
        DEBUG_FAILURE;
        return vaStatus;
    }

    vaStatus = psb__checkSurfaceDimensions(driver_data, width, height);
    if (VA_STATUS_SUCCESS != vaStatus) {
        DEBUG_FAILURE;
        return vaStatus;
    }

    if ((size < width * height * 1.5) ||
        (luma_stride < width) ||
        (chroma_u_stride * 2 < width) ||
        (chroma_v_stride * 2 < width) ||
        (chroma_u_offset < luma_offset + width * height) ||
        (chroma_v_offset < luma_offset + width * height)) {

        vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
        DEBUG_FAILURE;
        return vaStatus;
    }

    height_origin = height;

    for (i = 0; i < num_surfaces; i++) {
        int surfaceID;
        object_surface_p obj_surface;
        psb_surface_p psb_surface;

        surfaceID = object_heap_allocate(&driver_data->surface_heap);
        obj_surface = SURFACE(surfaceID);
        if (NULL == obj_surface) {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            DEBUG_FAILURE;
            break;
        }
        MEMSET_OBJECT(obj_surface, struct object_surface_s);

        obj_surface->surface_id = surfaceID;
        surface_list[i] = surfaceID;
        obj_surface->context_id = -1;
        obj_surface->width = width;
        obj_surface->height = height;
        obj_surface->width_r = width;
        obj_surface->height_r = height;
        obj_surface->height_origin = height_origin;

        psb_surface = (psb_surface_p) calloc(1, sizeof(struct psb_surface_s));
        if (NULL == psb_surface) {
            object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
            obj_surface->surface_id = VA_INVALID_SURFACE;

            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;

            DEBUG_FAILURE;
            break;
        }

        switch (format) {
        case VA_RT_FORMAT_YUV422:
            fourcc = VA_FOURCC_YV16;
            break;
        case VA_RT_FORMAT_YUV420:
        default:
            fourcc = VA_FOURCC_NV12;
            break;
        }

        vaStatus = psb_surface_create_for_userptr(driver_data, width, height,
                   size,
                   fourcc,
                   luma_stride,
                   chroma_u_stride,
                   chroma_v_stride,
                   luma_offset,
                   chroma_u_offset,
                   chroma_v_offset,
                   psb_surface
                                                 );

        if (VA_STATUS_SUCCESS != vaStatus) {
            free(psb_surface);
            object_heap_free(&driver_data->surface_heap, (object_base_p) obj_surface);
            obj_surface->surface_id = VA_INVALID_SURFACE;

            DEBUG_FAILURE;
            break;
        }
        buffer_stride = psb_surface->stride;
        /* by default, surface fourcc is NV12 */
        memset(psb_surface->extra_info, 0, sizeof(psb_surface->extra_info));
        psb_surface->extra_info[4] = fourcc;

        obj_surface->psb_surface = psb_surface;
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus) {
        /* surface_list[i-1] was the last successful allocation */
        for (; i--;) {
            object_surface_p obj_surface = SURFACE(surface_list[i]);
            psb__destroy_surface(driver_data, obj_surface);
            surface_list[i] = VA_INVALID_SURFACE;
        }
    }


    return vaStatus;
}



int  LOCK_HARDWARE(psb_driver_data_p driver_data)
{
    char ret = 0;

    if (driver_data->dri2 || driver_data->dri_dummy)
        return 0;

    pthread_mutex_lock(&driver_data->drm_mutex);
    DRM_CAS(driver_data->drm_lock, driver_data->drm_context,
            (DRM_LOCK_HELD | driver_data->drm_context), ret);
    if (ret) {
        ret = drmGetLock(driver_data->drm_fd, driver_data->drm_context, 0);
        /* driver_data->contended_lock=1; */
    }

    return ret;
}

int UNLOCK_HARDWARE(psb_driver_data_p driver_data)
{
    /* driver_data->contended_lock=0; */
    if (driver_data->dri2 || driver_data->dri_dummy)
        return 0;

    DRM_UNLOCK(driver_data->drm_fd, driver_data->drm_lock, driver_data->drm_context);
    pthread_mutex_unlock(&driver_data->drm_mutex);

    return 0;
}


static void psb__deinitDRM(VADriverContextP ctx)
{
    INIT_DRIVER_DATA

    if (driver_data->main_pool) {
        driver_data->main_pool->takeDown(driver_data->main_pool);
        driver_data->main_pool = NULL;
    }
    if (driver_data->fence_mgr) {
        wsbmFenceMgrTTMTakedown(driver_data->fence_mgr);
        driver_data->fence_mgr = NULL;
    }

    if (wsbmIsInitialized())
        wsbmTakedown();

    close(driver_data->drm_fd);
    driver_data->drm_fd  = -1;
}


static VAStatus psb__initDRI(VADriverContextP ctx)
{
    INIT_DRIVER_DATA
    struct dri_state *dri_state = (struct dri_state *)ctx->dri_state;

    assert(dri_state);
    assert(dri_state->driConnectedFlag == VA_DRI2 ||
           dri_state->driConnectedFlag == VA_DUMMY);

    driver_data->drm_fd = dri_state->fd;
    driver_data->dri_dummy = (dri_state->driConnectedFlag == VA_DUMMY);
    driver_data->dri2 = (dri_state->driConnectedFlag == VA_DRI2);
    driver_data->ws_priv = NULL;
    driver_data->bus_id = NULL;

    return VA_STATUS_SUCCESS;
}


static VAStatus psb__initTTM(VADriverContextP ctx)
{
    INIT_DRIVER_DATA

    const char drm_ext[] = "psb_ttm_placement_alphadrop";
    union drm_psb_extension_arg arg;
    struct _WsbmBufferPool *pool;
    int ret;
    const char exec_ext[] = "psb_ttm_execbuf_alphadrop";
    union drm_psb_extension_arg exec_arg;
    const char lncvideo_getparam_ext[] = "lnc_video_getparam";
    union drm_psb_extension_arg lncvideo_getparam_arg;

    /* init wsbm */
    ret = wsbmInit(wsbmNullThreadFuncs(), psbVNodeFuncs());
    if (ret) {
        psb__error_message("failed initializing libwsbm.\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }

    strncpy(arg.extension, drm_ext, sizeof(arg.extension));
    /* FIXME: should check dri enabled?
     * it seems not init dri here at all
     */
    ret = drmCommandWriteRead(driver_data->drm_fd, DRM_PSB_EXTENSION,
                              &arg, sizeof(arg));
    if (ret != 0 || !arg.rep.exists) {
        psb__error_message("failed to detect DRM extension \"%s\".\n",
                           drm_ext);
        driver_data->main_pool = NULL;
        return VA_STATUS_ERROR_UNKNOWN;
    } else {
        pool = wsbmTTMPoolInit(driver_data->drm_fd,
                               arg.rep.driver_ioctl_offset);
        if (pool == NULL) {
            psb__error_message("failed to get ttm pool\n");
            return VA_STATUS_ERROR_UNKNOWN;
        }
        driver_data->main_pool = pool;
    }

    strncpy(exec_arg.extension, exec_ext, sizeof(exec_arg.extension));
    ret = drmCommandWriteRead(driver_data->drm_fd, DRM_PSB_EXTENSION, &exec_arg,
                              sizeof(exec_arg));
    if (ret != 0 || !exec_arg.rep.exists) {
        psb__error_message("failed to detect DRM extension \"%s\".\n",
                           exec_ext);
        return FALSE;
    }
    driver_data->execIoctlOffset = exec_arg.rep.driver_ioctl_offset;

    strncpy(lncvideo_getparam_arg.extension, lncvideo_getparam_ext, sizeof(lncvideo_getparam_arg.extension));
    ret = drmCommandWriteRead(driver_data->drm_fd, DRM_PSB_EXTENSION, &lncvideo_getparam_arg,
                              sizeof(lncvideo_getparam_arg));
    if (ret != 0 || !lncvideo_getparam_arg.rep.exists) {
        psb__error_message("failed to detect DRM extension \"%s\".\n",
                           lncvideo_getparam_ext);
        /* return FALSE; */ /* not reture FALSE, so it still can run */
    }
    driver_data->getParamIoctlOffset = lncvideo_getparam_arg.rep.driver_ioctl_offset;
    return VA_STATUS_SUCCESS;
}

static VAStatus psb__initDRM(VADriverContextP ctx)
{
    VAStatus vaStatus;

    vaStatus = psb__initDRI(ctx);

    if (vaStatus == VA_STATUS_SUCCESS)
        return psb__initTTM(ctx);
    else
        return vaStatus;
}

VAStatus psb_Terminate(VADriverContextP ctx)
{
    INIT_DRIVER_DATA
    object_subpic_p obj_subpic;
    object_image_p obj_image;
    object_buffer_p obj_buffer;
    object_surface_p obj_surface;
    object_context_p obj_context;
    object_config_p obj_config;
    object_heap_iterator iter;

    psb__information_message("vaTerminate: begin to tear down\n");

    if (driver_data->bcd_registered != 0)
        if (VA_STATUS_SUCCESS != psb_release_video_bcd(ctx))
            return VA_STATUS_ERROR_UNKNOWN;

    /* Clean up left over contexts */
    obj_context = (object_context_p) object_heap_first(&driver_data->context_heap, &iter);
    while (obj_context) {
        psb__information_message("vaTerminate: contextID %08x still allocated, destroying\n", obj_context->base.id);
        psb__destroy_context(driver_data, obj_context);
        obj_context = (object_context_p) object_heap_next(&driver_data->context_heap, &iter);
    }
    object_heap_destroy(&driver_data->context_heap);

    /* Clean up SubpicIDs */
    obj_subpic = (object_subpic_p) object_heap_first(&driver_data->subpic_heap, &iter);
    while (obj_subpic) {
        psb__information_message("vaTerminate: subpictureID %08x still allocated, destroying\n", obj_subpic->base.id);
        psb__destroy_subpicture(driver_data, obj_subpic);
        obj_subpic = (object_subpic_p) object_heap_next(&driver_data->subpic_heap, &iter);
    }
    object_heap_destroy(&driver_data->subpic_heap);

    /* Clean up ImageIDs */
    obj_image = (object_image_p) object_heap_first(&driver_data->image_heap, &iter);
    while (obj_image) {
        psb__information_message("vaTerminate: imageID %08x still allocated, destroying\n", obj_image->base.id);
        psb__destroy_image(driver_data, obj_image);
        obj_image = (object_image_p) object_heap_next(&driver_data->image_heap, &iter);
    }
    object_heap_destroy(&driver_data->image_heap);

    /* Clean up left over buffers */
    obj_buffer = (object_buffer_p) object_heap_first(&driver_data->buffer_heap, &iter);
    while (obj_buffer) {
        psb__information_message("vaTerminate: bufferID %08x still allocated, destroying\n", obj_buffer->base.id);
        psb__destroy_buffer(driver_data, obj_buffer);
        obj_buffer = (object_buffer_p) object_heap_next(&driver_data->buffer_heap, &iter);
    }
    object_heap_destroy(&driver_data->buffer_heap);

    /* Clean up left over surfaces */
    obj_surface = (object_surface_p) object_heap_first(&driver_data->surface_heap, &iter);
    while (obj_surface) {
        psb__information_message("vaTerminate: surfaceID %08x still allocated, destroying\n", obj_surface->base.id);
        psb__destroy_surface(driver_data, obj_surface);
        obj_surface = (object_surface_p) object_heap_next(&driver_data->surface_heap, &iter);
    }
    object_heap_destroy(&driver_data->surface_heap);

    /* Clean up configIDs */
    obj_config = (object_config_p) object_heap_first(&driver_data->config_heap, &iter);
    while (obj_config) {
        object_heap_free(&driver_data->config_heap, (object_base_p) obj_config);
        obj_config = (object_config_p) object_heap_next(&driver_data->config_heap, &iter);
    }
    object_heap_destroy(&driver_data->config_heap);


    if (driver_data->camera_bo) {
        psb__information_message("vaTerminate: clearup camera global BO\n");

        psb_buffer_destroy((psb_buffer_p)driver_data->camera_bo);
        free(driver_data->camera_bo);
        driver_data->camera_bo = NULL;
    }

    if (driver_data->rar_bo) {
        psb__information_message("vaTerminate: clearup RAR global BO\n");

        psb_buffer_destroy((psb_buffer_p)driver_data->rar_bo);
        free(driver_data->rar_bo);
        driver_data->rar_bo = NULL;
    }

    if (driver_data->rar_rd) {
        RAR_desc_t *rar_rd = driver_data->rar_rd;

        psb__information_message("vaTerminate: tear down RAR device\n");

        RAR_fini(rar_rd);
        free(driver_data->rar_rd);
        driver_data->rar_rd = NULL;
    }

    if (driver_data->ws_priv) {
        psb__information_message("vaTerminate: tear down output portion\n");

        psb_deinitOutput(ctx);
        driver_data->ws_priv = NULL;
    }

    psb__information_message("vaTerminate: de-initialized DRM\n");

    psb__deinitDRM(ctx);

    if (driver_data->msvdx_decode_status)
        free(driver_data->msvdx_decode_status);

    if (driver_data->surface_mb_error)
        free(driver_data->surface_mb_error);

    free(ctx->pDriverData);
    ctx->pDriverData = NULL;
    psb__information_message("vaTerminate: cleanup successfully, goodbye\n\n");

    psb__close_log();

    return VA_STATUS_SUCCESS;
}

EXPORT VAStatus __vaDriverInit_0_31(VADriverContextP ctx)
{
    psb_driver_data_p driver_data;
    struct VADriverVTableTPI *tpi;
    struct VADriverVTableEGL *va_egl;
    int result;

#ifdef DEBUG_TRACE
    /* make gdb always stop here */
    signal(SIGUSR1, SIG_IGN);
    kill(getpid(), SIGUSR1);
#endif

    psb__open_log();

    psb__information_message("vaInitilize: start the journey\n");

    ctx->version_major = 0;
    ctx->version_minor = 31;

    ctx->max_profiles = PSB_MAX_PROFILES;
    ctx->max_entrypoints = PSB_MAX_ENTRYPOINTS;
    ctx->max_attributes = PSB_MAX_CONFIG_ATTRIBUTES;
    ctx->max_image_formats = PSB_MAX_IMAGE_FORMATS;
    ctx->max_subpic_formats = PSB_MAX_SUBPIC_FORMATS;
    ctx->max_display_attributes = PSB_MAX_DISPLAY_ATTRIBUTES;

    ctx->vtable->vaTerminate = psb_Terminate;
    ctx->vtable->vaQueryConfigEntrypoints = psb_QueryConfigEntrypoints;
    ctx->vtable->vaTerminate = psb_Terminate;
    ctx->vtable->vaQueryConfigProfiles = psb_QueryConfigProfiles;
    ctx->vtable->vaQueryConfigEntrypoints = psb_QueryConfigEntrypoints;
    ctx->vtable->vaQueryConfigAttributes = psb_QueryConfigAttributes;
    ctx->vtable->vaCreateConfig = psb_CreateConfig;
    ctx->vtable->vaDestroyConfig = psb_DestroyConfig;
    ctx->vtable->vaGetConfigAttributes = psb_GetConfigAttributes;
    ctx->vtable->vaCreateSurfaces = psb_CreateSurfaces;
    ctx->vtable->vaDestroySurfaces = psb_DestroySurfaces;
    ctx->vtable->vaCreateContext = psb_CreateContext;
    ctx->vtable->vaDestroyContext = psb_DestroyContext;
    ctx->vtable->vaCreateBuffer = psb_CreateBuffer;
    ctx->vtable->vaBufferSetNumElements = psb_BufferSetNumElements;
    ctx->vtable->vaMapBuffer = psb_MapBuffer;
    ctx->vtable->vaUnmapBuffer = psb_UnmapBuffer;
    ctx->vtable->vaDestroyBuffer = psb_DestroyBuffer;
    ctx->vtable->vaBeginPicture = psb_BeginPicture;
    ctx->vtable->vaRenderPicture = psb_RenderPicture;
    ctx->vtable->vaEndPicture = psb_EndPicture;
    ctx->vtable->vaSyncSurface = psb_SyncSurface;
    ctx->vtable->vaQuerySurfaceStatus = psb_QuerySurfaceStatus;
    ctx->vtable->vaQuerySurfaceError = psb_QuerySurfaceError;
    ctx->vtable->vaPutSurface = psb_PutSurface;
    ctx->vtable->vaQueryImageFormats = psb_QueryImageFormats;
    ctx->vtable->vaCreateImage = psb_CreateImage;
    ctx->vtable->vaDeriveImage = psb_DeriveImage;
    ctx->vtable->vaDestroyImage = psb_DestroyImage;
    ctx->vtable->vaSetImagePalette = psb_SetImagePalette;
    ctx->vtable->vaGetImage = psb_GetImage;
    ctx->vtable->vaPutImage = psb_PutImage;
    ctx->vtable->vaQuerySubpictureFormats = psb_QuerySubpictureFormats;
    ctx->vtable->vaCreateSubpicture = psb_CreateSubpicture;
    ctx->vtable->vaDestroySubpicture = psb_DestroySubpicture;
    ctx->vtable->vaSetSubpictureImage = psb_SetSubpictureImage;
    ctx->vtable->vaSetSubpictureChromakey = psb_SetSubpictureChromakey;
    ctx->vtable->vaSetSubpictureGlobalAlpha = psb_SetSubpictureGlobalAlpha;
    ctx->vtable->vaAssociateSubpicture = psb_AssociateSubpicture;
    ctx->vtable->vaDeassociateSubpicture = psb_DeassociateSubpicture;
    ctx->vtable->vaQueryDisplayAttributes = psb_QueryDisplayAttributes;
    ctx->vtable->vaGetDisplayAttributes = psb_GetDisplayAttributes;
    ctx->vtable->vaSetDisplayAttributes = psb_SetDisplayAttributes;
    ctx->vtable->vaBufferInfo = psb_BufferInfo;
    ctx->vtable->vaLockSurface = psb_LockSurface;
    ctx->vtable->vaUnlockSurface = psb_UnlockSurface;
    ctx->vtable_tpi = calloc(1, sizeof(struct VADriverVTableTPI));
    if (NULL == ctx->vtable_tpi)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    tpi = (struct VADriverVTableTPI *)ctx->vtable_tpi;
    tpi->vaCreateSurfaceFromCIFrame = psb_CreateSurfaceFromCIFrame;
    tpi->vaCreateSurfaceFromV4L2Buf = psb_CreateSurfaceFromV4L2Buf;
    tpi->vaCreateSurfacesForUserPtr = psb_CreateSurfacesForUserPtr;

#ifdef ANDROID
    tpi->vaPutSurfaceBuf = psb_PutSurfaceBuf;
#endif

    ctx->vtable_egl = calloc(1, sizeof(struct VADriverVTableEGL));
    if (NULL == ctx->vtable_egl)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    va_egl = (struct VADriverVTableEGL *)ctx->vtable_egl;
    va_egl->vaGetEGLClientBufferFromSurface = psb_GetEGLClientBufferFromSurface;

    driver_data = (psb_driver_data_p) calloc(1, sizeof(*driver_data));
    ctx->pDriverData = (void *) driver_data;
    if (NULL == driver_data) {
        if (ctx->vtable_tpi)
            free(ctx->vtable_tpi);
        if (ctx->vtable_egl)
            free(ctx->vtable_egl);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    if (VA_STATUS_SUCCESS != psb__initDRM(ctx)) {
        free(ctx->pDriverData);
        ctx->pDriverData = NULL;
        return VA_STATUS_ERROR_UNKNOWN;
    }

    pthread_mutex_init(&driver_data->drm_mutex, NULL);

    /*
     * To read PBO.MSR.CCF Mode and Status Register C-Spec -p112
     */
#define PCI_PORT5_REG80_VIDEO_SD_DISABLE        0x0008
#define PCI_PORT5_REG80_VIDEO_HD_DISABLE        0x0010

#if 0
    struct drm_psb_hw_info hw_info;
    do {
        result = drmCommandRead(driver_data->drm_fd, DRM_PSB_HW_INFO, &hw_info, sizeof(hw_info));
    } while (result == EAGAIN);

    if (result != 0) {
        psb__deinitDRM(ctx);
        free(ctx->pDriverData);
        ctx->pDriverData = NULL;
        return VA_STATUS_ERROR_UNKNOWN;
    }

    driver_data->video_sd_disabled = !!(hw_info.caps & PCI_PORT5_REG80_VIDEO_SD_DISABLE);
    driver_data->video_hd_disabled = !!(hw_info.caps & PCI_PORT5_REG80_VIDEO_HD_DISABLE);
    psb__information_message("hw_info: rev_id = %08x capabilities = %08x\n", hw_info.rev_id, hw_info.caps);
    psb__information_message("hw_info: video_sd_disable=%d,video_hd_disable=%d\n",
                             driver_data->video_sd_disabled, driver_data->video_hd_disabled);
    if (driver_data->video_sd_disabled != 0) {
        psb__error_message("MRST: hw_info shows video_sd_disable is true,fix it manually\n");
        driver_data->video_sd_disabled = 0;
    }
    if (driver_data->video_hd_disabled != 0) {
        psb__error_message("MRST: hw_info shows video_hd_disable is true,fix it manually\n");
        driver_data->video_hd_disabled = 0;
    }
#endif

    if (0 != psb_get_device_info(ctx)) {
        psb__error_message("ERROR: failed to get video device info\n");
        driver_data->encode_supported = 1;
        driver_data->decode_supported = 1;
        driver_data->hd_encode_supported = 1;
        driver_data->hd_decode_supported = 1;
    }

    driver_data->use_xrandr_thread = 0;
    driver_data->xrandr_thread_id = 0;
    driver_data->rotate = VA_ROTATION_NONE;
    driver_data->video_rotate = VA_ROTATION_NONE;
    driver_data->xrandr_dirty = 0;
    driver_data->xrandr_update = 0;
    struct dri_state *dri_state = (struct dri_state *)ctx->dri_state;
    if (dri_state->driConnectedFlag == VA_DRI1 ||
        dri_state->driConnectedFlag == VA_DRI2 ||
        dri_state->driConnectedFlag == VA_DUMMY) {
        if (VA_STATUS_SUCCESS != psb_initOutput(ctx)) {
            psb__deinitDRM(ctx);
            free(ctx->pDriverData);
            ctx->pDriverData = NULL;
            return VA_STATUS_ERROR_UNKNOWN;
        }
    }

    driver_data->msvdx_context_base = (((unsigned int) getpid()) & 0xffff) << 16;

    //    driver_data->profile2Format[VAProfileMPEG2Simple] = &psb_MPEG2_vtable;
    driver_data->profile2Format[VAProfileMPEG2Main][VAEntrypointVLD] = &psb_MPEG2_vtable;
    driver_data->profile2Format[VAProfileMPEG2Main][VAEntrypointMoComp] = &psb_MPEG2MC_vtable;

    driver_data->profile2Format[VAProfileMPEG4Simple][VAEntrypointVLD] = &psb_MPEG4_vtable;
    driver_data->profile2Format[VAProfileMPEG4AdvancedSimple][VAEntrypointVLD] = &psb_MPEG4_vtable;
    //    driver_data->profile2Format[VAProfileMPEG4Main][VAEntrypointVLD] = &psb_MPEG4_vtable;

    driver_data->profile2Format[VAProfileH264Baseline][VAEntrypointVLD] = &psb_H264_vtable;
    driver_data->profile2Format[VAProfileH264Main][VAEntrypointVLD] = &psb_H264_vtable;
    driver_data->profile2Format[VAProfileH264High][VAEntrypointVLD] = &psb_H264_vtable;
    driver_data->profile2Format[VAProfileH264ConstrainedBaseline][VAEntrypointVLD] = &psb_H264_vtable;

    driver_data->profile2Format[VAProfileVC1Simple][VAEntrypointVLD] = &psb_VC1_vtable;
    driver_data->profile2Format[VAProfileVC1Main][VAEntrypointVLD] = &psb_VC1_vtable;
    driver_data->profile2Format[VAProfileVC1Advanced][VAEntrypointVLD] = &psb_VC1_vtable;
    driver_data->profile2Format[VAProfileH264ConstrainedBaseline][VAEntrypointVLD] = &psb_H264_vtable;

    if (IS_MFLD(driver_data)) {
        driver_data->profile2Format[VAProfileH263Baseline][VAEntrypointEncSlice] = &pnw_H263ES_vtable;
        driver_data->profile2Format[VAProfileH264Baseline][VAEntrypointEncSlice] = &pnw_H264ES_vtable;
        driver_data->profile2Format[VAProfileH264Main][VAEntrypointEncSlice] = &pnw_H264ES_vtable;
        driver_data->profile2Format[VAProfileMPEG4Simple][VAEntrypointEncSlice] = &pnw_MPEG4ES_vtable;
        driver_data->profile2Format[VAProfileMPEG4AdvancedSimple][VAEntrypointEncSlice] = &pnw_MPEG4ES_vtable;
        driver_data->profile2Format[VAProfileJPEGBaseline][VAEntrypointEncPicture] = &pnw_JPEG_vtable;

        driver_data->profile2Format[VAProfileMPEG2Main][VAEntrypointVLD] = &pnw_MPEG2_vtable;

        driver_data->profile2Format[VAProfileMPEG4Simple][VAEntrypointVLD] = &pnw_MPEG4_vtable;
        driver_data->profile2Format[VAProfileMPEG4AdvancedSimple][VAEntrypointVLD] = &pnw_MPEG4_vtable;

        driver_data->profile2Format[VAProfileH264Baseline][VAEntrypointVLD] = &pnw_H264_vtable;
        driver_data->profile2Format[VAProfileH264Main][VAEntrypointVLD] = &pnw_H264_vtable;
        driver_data->profile2Format[VAProfileH264High][VAEntrypointVLD] = &pnw_H264_vtable;

        driver_data->profile2Format[VAProfileVC1Simple][VAEntrypointVLD] = &pnw_VC1_vtable;
        driver_data->profile2Format[VAProfileVC1Main][VAEntrypointVLD] = &pnw_VC1_vtable;
        driver_data->profile2Format[VAProfileVC1Advanced][VAEntrypointVLD] = &pnw_VC1_vtable;
        driver_data->profile2Format[VAProfileH264ConstrainedBaseline][VAEntrypointVLD] = &pnw_H264_vtable;
    } else if (IS_MRST(driver_data) && driver_data->encode_supported) {
        driver_data->profile2Format[VAProfileH263Baseline][VAEntrypointEncSlice] = &lnc_H263ES_vtable;
        driver_data->profile2Format[VAProfileH264Baseline][VAEntrypointEncSlice] = &lnc_H264ES_vtable;
        driver_data->profile2Format[VAProfileH264Main][VAEntrypointEncSlice] = &lnc_H264ES_vtable;
        driver_data->profile2Format[VAProfileMPEG4Simple][VAEntrypointEncSlice] = &lnc_MPEG4ES_vtable;
        driver_data->profile2Format[VAProfileMPEG4AdvancedSimple][VAEntrypointEncSlice] = &lnc_MPEG4ES_vtable;
    }

    result = object_heap_init(&driver_data->config_heap, sizeof(struct object_config_s), CONFIG_ID_OFFSET);
    ASSERT(result == 0);

    result = object_heap_init(&driver_data->context_heap, sizeof(struct object_context_s), CONTEXT_ID_OFFSET);
    ASSERT(result == 0);

    result = object_heap_init(&driver_data->surface_heap, sizeof(struct object_surface_s), SURFACE_ID_OFFSET);
    ASSERT(result == 0);

    result = object_heap_init(&driver_data->buffer_heap, sizeof(struct object_buffer_s), BUFFER_ID_OFFSET);
    ASSERT(result == 0);

    result = object_heap_init(&driver_data->image_heap, sizeof(struct object_image_s), IMAGE_ID_OFFSET);
    ASSERT(result == 0);

    result = object_heap_init(&driver_data->subpic_heap, sizeof(struct object_subpic_s), SUBPIC_ID_OFFSET);
    ASSERT(result == 0);

    driver_data->cur_displaying_surface = VA_INVALID_SURFACE;
    driver_data->last_displaying_surface = VA_INVALID_SURFACE;

    driver_data->clear_color = 0;
    driver_data->blend_color = 0;
    driver_data->blend_mode = 0;
    driver_data->overlay_auto_paint_color_key = 0;

    if (IS_MFLD(driver_data))
        ctx->str_vendor = PSB_STR_VENDOR_MFLD;
    else
        ctx->str_vendor = PSB_STR_VENDOR_MRST;

    psb__information_message("vaInitilize: succeeded!\n\n");

    driver_data->msvdx_decode_status = calloc(1, sizeof(drm_psb_msvdx_decode_status_t));
    if (NULL == driver_data->msvdx_decode_status) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    driver_data->surface_mb_error = calloc(MAX_MB_ERRORS, sizeof(VASurfaceDecodeMBErrors));
    if (NULL == driver_data->surface_mb_error) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}


EXPORT VAStatus __vaDriverInit_0_32(VADriverContextP ctx)
{
    return __vaDriverInit_0_31(ctx);
}



static int psb_get_device_info(VADriverContextP ctx)
{
    INIT_DRIVER_DATA;
    struct drm_lnc_video_getparam_arg arg;
    unsigned long device_info;
    int ret = 0;
    unsigned long video_capability;
    unsigned long pci_device;

    driver_data->dev_id = 0x4100; /* by default MRST */

    arg.key = LNC_VIDEO_DEVICE_INFO;
    arg.value = (uint64_t)((unsigned long) & device_info);
    ret = drmCommandWriteRead(driver_data->drm_fd, driver_data->getParamIoctlOffset,
                              &arg, sizeof(arg));
    if (ret == 0) {
        pci_device = (device_info >> 16) & 0xffff;
        video_capability = device_info & 0xffff;

        driver_data->dev_id = pci_device;
        psb__information_message("Retrieve Device ID 0x%04x\n", driver_data->dev_id);

        if ((IS_MRST(driver_data) && (pci_device != 0x4101)) ||
            IS_MFLD(driver_data))
            driver_data->encode_supported = 1;
        else /* 0x4101 or other device hasn't encode support */
            driver_data->encode_supported = 0;

        driver_data->decode_supported = !(video_capability & 0x2);
        driver_data->hd_decode_supported = !(video_capability & 0x3);
        driver_data->hd_encode_supported = !(video_capability & 0x4);

        psb__information_message("video capability: decode %s, HD decode %s\n",
                                 driver_data->decode_supported ? "support" : "not support",
                                 driver_data->hd_decode_supported ? "support" : "not support");

        psb__information_message("video capability: encode %s, HD encode %s\n",
                                 driver_data->encode_supported ? "support" : "not support",
                                 driver_data->hd_encode_supported ? "support" : "not support");


        return ret;
    }

    psb__information_message("failed to get video device info\n");

    return ret;
}
