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
 *    Jason Hu <jason.hu@intel.com>
 */

#include "psb_HDMIExtMode.h"
#include "psb_output_android.h"
#include "pvr2d.h"
#include "psb_drv_video.h"

#define INIT_DRIVER_DATA    psb_driver_data_p driver_data = (psb_driver_data_p) ctx->pDriverData

VAStatus psb_HDMIExt_get_prop(psb_android_output_p output,
                              unsigned short *xres, unsigned short *yres)
{
    psb_HDMIExt_info_p psb_HDMIExt_info = (psb_HDMIExt_info_p)output->psb_HDMIExt_info;

    if (!psb_HDMIExt_info || !psb_HDMIExt_info->hdmi_extvideo_prop ||
        (psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode == OFF)) {
        psb__error_message("%s : Failed to get HDMI prop\n", __FUNCTION__);
        return VA_STATUS_ERROR_UNKNOWN;
    }
    *xres = psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode_XRes;
    *yres = psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode_YRes;

    return VA_STATUS_SUCCESS;
}

psb_hdmi_mode psb_HDMIExt_get_mode(psb_android_output_p output)
{
    psb_HDMIExt_info_p psb_HDMIExt_info = (psb_HDMIExt_info_p)output->psb_HDMIExt_info;

    if (!psb_HDMIExt_info) {
        psb__error_message("%s : Failed to get HDMI mode\n", __FUNCTION__);
        return VA_STATUS_ERROR_UNKNOWN;
    }
    return psb_HDMIExt_info->hdmi_mode;
}

VAStatus psb_HDMIExt_update(VADriverContextP ctx, psb_HDMIExt_info_p psb_HDMIExt_info)
{
    INIT_DRIVER_DATA;
    drmModeCrtc *hdmi_crtc = NULL;
    drmModeConnector *hdmi_connector = NULL;
    drmModeEncoder *hdmi_encoder = NULL;
    int width = 0, height = 0;
    char *strHeight = NULL;
    struct drm_lnc_video_getparam_arg arg;
    int hdmi_state = 0;
    static int hdmi_connected_frame = 0;

    arg.key = IMG_VIDEO_GET_HDMI_STATE;
    arg.value = (uint64_t)((unsigned int)&hdmi_state);
    drmCommandWriteRead(driver_data->drm_fd, driver_data->getParamIoctlOffset,
                        &arg, sizeof(arg));

    psb__information_message("%s : hdmi_state = %d\n", __FUNCTION__, hdmi_state);
    if (hdmi_state == HDMI_MODE_EXT_VIDEO) {
        psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode = EXTENDED_VIDEO;
        psb_HDMIExt_info->hdmi_mode = EXTENDED_VIDEO;

        if ((psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode_XRes == 0 ||
             psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode_YRes == 0)) {
            psb_extvideo_prop_p hdmi_extvideo_prop = psb_HDMIExt_info->hdmi_extvideo_prop;

            hdmi_connector = drmModeGetConnector(driver_data->drm_fd, psb_HDMIExt_info->hdmi_connector_id);
            if (!hdmi_connector) {
                psb__error_message("%s : Failed to get hdmi connector\n", __FUNCTION__);
                return VA_STATUS_ERROR_UNKNOWN;
            }

            hdmi_encoder = drmModeGetEncoder(driver_data->drm_fd, hdmi_connector->encoder_id);
            if (!hdmi_encoder) {
                psb__error_message("%s : Failed to get hdmi encoder\n", __FUNCTION__);
                return VA_STATUS_ERROR_UNKNOWN;
            }

            hdmi_crtc = drmModeGetCrtc(driver_data->drm_fd, hdmi_encoder->crtc_id);
            if (!hdmi_crtc) {
                /* No CRTC attached to HDMI. */
                psb__error_message("%s : Failed to get hdmi crtc\n", __FUNCTION__);
                return VA_STATUS_ERROR_UNKNOWN;
            }

            strHeight = strstr(hdmi_crtc->mode.name, "x");
            hdmi_extvideo_prop->ExtVideoMode_XRes = (unsigned short)atoi(hdmi_crtc->mode.name);
            hdmi_extvideo_prop->ExtVideoMode_YRes = (unsigned short)atoi(strHeight + 1);
            psb__information_message("%s : size = %d x %d\n", __FUNCTION__,
                                     hdmi_extvideo_prop->ExtVideoMode_XRes, hdmi_extvideo_prop->ExtVideoMode_YRes);
            drmModeFreeCrtc(hdmi_crtc);
            drmModeFreeEncoder(hdmi_encoder);
            drmModeFreeConnector(hdmi_connector);
        }
    } else if (hdmi_state == HDMI_MODE_OFF) {
        psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode = OFF;
        psb_HDMIExt_info->hdmi_mode = OFF;
        hdmi_connected_frame = 0;
    } else {
        psb_HDMIExt_info->hdmi_extvideo_prop->ExtVideoMode = UNDEFINED;
        psb_HDMIExt_info->hdmi_mode = UNDEFINED;
    }

    return VA_STATUS_SUCCESS;
}

psb_HDMIExt_info_p psb_HDMIExt_init(VADriverContextP ctx, psb_android_output_p output)
{
    INIT_DRIVER_DATA;
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *mipi_encoder = NULL;
    drmModeCrtc *mipi_crtc = NULL;
    int mipi_connector_id = 0, mipi_encoder_id = 0, mipi_crtc_id = 0, i;
    psb_HDMIExt_info_p psb_HDMIExt_info = NULL;

    psb_HDMIExt_info = (psb_HDMIExt_info_p)calloc(1, sizeof(psb_HDMIExt_info_s));
    if (!psb_HDMIExt_info) {
        psb__error_message("%s : Failed to create psb_HDMIExt_info.\n", __FUNCTION__);
        return NULL;
    }
    memset(psb_HDMIExt_info, 0, sizeof(psb_HDMIExt_info_s));

    psb_HDMIExt_info->hdmi_extvideo_prop = (psb_extvideo_prop_p)calloc(1, sizeof(psb_extvideo_prop_s));
    if (!psb_HDMIExt_info->hdmi_extvideo_prop) {
        psb__error_message("%s : Failed to create hdmi_extvideo_prop.\n", __FUNCTION__);
        return NULL;
    }
    memset(psb_HDMIExt_info->hdmi_extvideo_prop, 0, sizeof(psb_extvideo_prop_s));

    /*Get Resources.*/
    resources = drmModeGetResources(driver_data->drm_fd);
    if (!resources) {
        psb__error_message("%s : drmModeGetResources failed.\n", __FUNCTION__);
        goto exit;
    }

    /*Get MIPI and HDMI connector id.*/
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(driver_data->drm_fd, resources->connectors[i]);

        if (!connector) {
            psb__error_message("%s : Failed to get connector %i\n", __FUNCTION__,
                               resources->connectors[i]);
            continue;
        }

        if (connector->connector_type == DRM_MODE_CONNECTOR_DVID)
            psb_HDMIExt_info->hdmi_connector_id = connector->connector_id;

        if ((connector->connector_type == /*DRM_MODE_CONNECTOR_MIPI*/15) &&
            (!mipi_connector_id)) {
            mipi_connector_id = connector->connector_id;
            mipi_encoder_id = connector->encoder_id;
        }

        drmModeFreeConnector(connector);
    }

    if (!mipi_connector_id ||
        !psb_HDMIExt_info->hdmi_connector_id ||
        !mipi_encoder_id) {
        psb__error_message("%s : Failed to get connector id or mipi encoder id. mipi_connector_id=%d, hdmi_connector_id=%d, mipi_encoder_id=%d\n", __FUNCTION__,
                           mipi_connector_id, psb_HDMIExt_info->hdmi_connector_id, mipi_encoder_id);
        goto exit;
    }

    mipi_encoder = drmModeGetEncoder(driver_data->drm_fd, mipi_encoder_id);
    if (!mipi_encoder) {
        psb__error_message("%s : Failed to get mipi encoder %i\n", __FUNCTION__);
        goto exit;
    }

    psb_HDMIExt_info->mipi_crtc_id = mipi_encoder->crtc_id;
    psb__information_message("%s : mipi_crtc_id = %d\n", __FUNCTION__,
                             mipi_crtc_id);

    drmModeFreeEncoder(mipi_encoder);

    if (psb_HDMIExt_update(ctx, psb_HDMIExt_info))
        goto exit;

    if (resources)
        drmModeFreeResources(resources);

    return psb_HDMIExt_info;

exit:
    if (resources)
        drmModeFreeResources(resources);

    if (connector)
        drmModeFreeConnector(connector);

    free(psb_HDMIExt_info);

    return NULL;
}

VAStatus psb_HDMIExt_deinit(psb_android_output_p output)
{
    psb_HDMIExt_info_p psb_HDMIExt_info = (psb_HDMIExt_info_p)output->psb_HDMIExt_info;

    if (psb_HDMIExt_info->hdmi_extvideo_prop)
        free(psb_HDMIExt_info->hdmi_extvideo_prop);

    if (psb_HDMIExt_info)
        free(psb_HDMIExt_info);

    return VA_STATUS_SUCCESS;
}

