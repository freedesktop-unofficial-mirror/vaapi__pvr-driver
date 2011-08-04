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


#ifndef _LNC_OSPM_EVENT_H_
#define _LNC_OSPM_EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "psb_drv_video.h"

#define OSPM_DBUS_MAX_OBJ_NAME_LEN  64

#define OSPM_EVENT_APP_LAUNCH         "launch"
#define OSPM_EVENT_APP_EXIT           "exit"
#define OSPM_EVENT_PPM_ACK            "ack"
#define OSPM_DBUS_EVENT_START         "start"
#define OSPM_DBUS_EVENT_STOP          "stop"
#define OSPM_DBUS_EVENT_PAUSE         "pause"
#define OSPM_DBUS_EVENT_ACTIVATED     "activate"
#define OSPM_DBUS_EVENT_DEACTIVATED   "deactivate"
#define OSPM_DBUS_EVENT_CHARGING      "charging"
#define OSPM_DBUS_EVENT_DISCHARGING   "discharging"
#define OSPM_DBUS_EVENT_NORMAL        "normal"
#define OSPM_DBUS_EVENT_ALERT         "alert"
#define OSPM_DBUS_EVENT_CRITICAL      "critical"

    int lnc_ospm_start(psb_driver_data_p driver_data, int encode);
    int lnc_ospm_stop(psb_driver_data_p driver_data, int encode);

#ifdef __cplusplus
}
#endif

#endif /* _LNC_OSPM_EVENT_H_ */
