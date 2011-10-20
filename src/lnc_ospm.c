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
 *    Shengquan Yuan  <shengquan.yuan@intel.com>
 *    Forrest Zhang  <forrest.zhang@intel.com>
 *
 */


#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "lnc_ospm.h"

static int
lnc_ospm_event_send(const char * obj_path, const char * method_name)
{
    int ret = 0;

    if (obj_path == NULL || method_name == NULL) {
        psb__information_message("obj_path or method_name null pointer error\n");
        ret = 2;
        goto exit;
    }

    if (!strcmp(obj_path, "video_playback")) {
        if (!strcmp(method_name, OSPM_DBUS_EVENT_START)) {
            ret = system("dbus-send --system --print-reply --dest=com.intel.mid.ospm \
                          /com/intel/mid/ospm/video_playback com.intel.mid.ospm.MMF.IndicateStart");
            if (ret < 0) {
                psb__information_message("ospm_event_send error: event %s send error #%d on %s\n", method_name, ret, obj_path);
            }
        } else if (!strcmp(method_name, OSPM_DBUS_EVENT_STOP)) {
            ret = system("dbus-send --system --print-reply --dest=com.intel.mid.ospm \
                          /com/intel/mid/ospm/video_playback com.intel.mid.ospm.MMF.IndicateStop");
            if (ret < 0) {
                psb__information_message("ospm_event_send error: event %s send error #%d on %s\n", method_name, ret, obj_path);
            }
        } else {
            psb__information_message("ospm_event_send error: event %s isn't supported on %s\n", method_name, obj_path);
            ret = -1;
        }
    } else if (!strcmp(obj_path, "video_record")) {
        if (!strcmp(method_name, OSPM_DBUS_EVENT_START)) {
            ret = system("dbus-send --system --print-reply --dest=com.intel.mid.ospm \
                          /com/intel/mid/ospm/video_record com.intel.mid.ospm.MMF.IndicateStart");
            if (ret < 0) {
                psb__information_message("ospm_event_send error: event %s send error #%d on %s\n", method_name, ret, obj_path);
            }
        } else if (!strcmp(method_name, OSPM_DBUS_EVENT_STOP)) {
            ret = system("dbus-send --system --print-reply --dest=com.intel.mid.ospm \
                         /com/intel/mid/ospm/video_record com.intel.mid.ospm.MMF.IndicateStop");
            if (ret < 0) {
                psb__information_message("ospm_event_send error: event %s send error #%d on %s\n", method_name, ret, obj_path);
            }
        } else {
            psb__information_message("ospm_event_send error: event %s isn't supported on %s\n", method_name, obj_path);
            ret = 3;
        }
    } else {
        psb__information_message("ospm_event_send error: originator %s isn't supported\n", obj_path);
        ret = 4;
    }
exit:
    return ret;
}

static int lnc_handle_pm_qos(psb_driver_data_p driver_data)
{
    int ret = 0;
    unsigned int video_encode_pm_qos_default = 10000; /* 10 ms */

    driver_data->pm_qos_fd = open("/dev/cpu_dma_latency", O_RDWR);
    if (driver_data->pm_qos_fd == -1) {
        psb__information_message("PM_QoS for video Encoder initialization error\n");
        return -1;
    }

    psb__information_message("PM_QoS for video Encoder initialization ok\n");

    ret = write(driver_data->pm_qos_fd, &video_encode_pm_qos_default, sizeof(int));
    if (ret == -1)
        psb__information_message("PM_QoS for video Encoder config error\n");
    else
        psb__information_message("PM_QoS for video Encoder config ok\n");

    return ret;
}

static int lnc_toggle_gfxd0i3(psb_driver_data_p driver_data, int enable)
{
    int ret = -1;
    struct stat buf;

    if (enable) {
        /* Android */
        if (stat("/proc/dri/0/ospm", &buf) == 0)
            ret = system("echo 1 > /proc/dri/0/ospm");

        if (stat("/sys/module/pvrsrvkm/parameters/ospm", &buf) == 0) {
            ret = system("echo 1 > /sys/module/pvrsrvkm/parameters/ospm");
            return ret;
        }

        /* mrst MeeGo */
        if (stat("/sys/module/mrst_gfx/parameters/ospm", &buf) == 0) {
            ret = system("echo 1 > /sys/module/mrst_gfx/parameters/ospm");
            return ret;
        }

        /* mfld MeeGo */
        if (stat("/sys/module/medfield_gfx/parameters/ospm", &buf) == 0) {
            ret = system("echo 1 > /sys/module/medfield_gfx/parameters/ospm");
            return ret;
        }
    } else {
        if (stat("/proc/dri/0/ospm", &buf) == 0)
            ret = system("echo 0 > /proc/dri/0/ospm");

        /* Android */
        if (stat("/sys/module/pvrsrvkm/parameters/ospm", &buf) == 0) {
            ret = system("echo 0 > /sys/module/pvrsrvkm/parameters/ospm");
            return ret;
        }

        /* mrst MeeGo */
        if (stat("/sys/module/mrst_gfx/parameters/ospm", &buf) == 0) {
            ret = system("echo 0 > /sys/module/mrst_gfx/parameters/ospm");
            return ret;
        }

        /* mfld MeeGo */
        if (stat("/sys/module/medfield_gfx/parameters/ospm", &buf) == 0) {
            ret = system("echo 0 > /sys/module/medfield_gfx/parameters/ospm");
            return ret;
        }
    }

    return ret;
}

int lnc_ospm_start(psb_driver_data_p driver_data, int encode)
{
    int ret;

    if (psb_parse_config("PSB_VIDEO_NOPM", NULL) == 0)
        return 0;

    if (IS_MRST(driver_data)) {
        /*
        psb__information_message("OSPM:send DBUS message to ospm daemon\n");
        if (encode)
            ret = lnc_ospm_event_send("video_record", "start");
        else
            ret = lnc_ospm_event_send("video_playback", "start");

        if (ret != 0)
            psb__information_message("lnc_ospm_event_send start error: #%d\n", ret);
        else
            psb__information_message("lnc_ospm_event_send start ok\n");
        */
    } else if (IS_MFLD(driver_data)) {
        psb__information_message("OSPM:set PM_QoS parameters\n");
        return 0;
        /*
        if (encode)
            lnc_handle_pm_qos(driver_data);
        */
    }

    if (encode) {
        ret = lnc_toggle_gfxd0i3(driver_data, 0);

        if (ret == 0)
            psb__information_message("OSPM:disabled Gfx D0i3 for encode\n");
    }

    return 0;
}

int lnc_ospm_stop(psb_driver_data_p driver_data, int encode)
{
    int ret;

    if (getenv("PSB_VIDEO_NO_OSPM"))
        return 0;

    if (IS_MRST(driver_data)) {
        /*
        psb__information_message("OSPM:send DBUS message to ospm daemon\n");
        if (encode)
            ret = lnc_ospm_event_send("video_record", "stop");
        else
            ret = lnc_ospm_event_send("video_playback", "stop");

        if (ret != 0)
            psb__information_message("lnc_ospm_event_send start error: #%d\n", ret);
        else
            psb__information_message("lnc_ospm_event_send start ok\n");
        */
    } else if (IS_MFLD(driver_data)) {
        psb__information_message("OSPM:set PM_QoS parameters\n");
        /*
        if (encode)
            lnc_handle_pm_qos(driver_data);
        */
        return 0;
    }

    if (encode) {
        ret = lnc_toggle_gfxd0i3(driver_data, 1);
        if (ret == 0)
            psb__information_message("OSPM:re-enabled Gfx D0i3 for encode\n");
    }

    return 0;
}
