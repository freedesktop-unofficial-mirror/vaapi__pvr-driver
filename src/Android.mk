# Copyright (c) 2011 Intel Corporation. All Rights Reserved.
#
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sub license, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
# 
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
# IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

ifeq ($(ENABLE_IMG_GRAPHICS),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

PSBVIDEO_LOG_ENABLE := true

LOCAL_SRC_FILES :=		\
    lnc_H263ES.c            \
    lnc_H264ES.c            \
    lnc_MPEG4ES.c           \
    lnc_cmdbuf.c            \
    lnc_hostcode.c          \
    lnc_hostheader.c        \
    lnc_ospm.c		\
    object_heap.c           \
    psb_H264.c          	\
    psb_MPEG2.c         	\
    psb_MPEG2MC.c           \
    psb_MPEG4.c         	\
    psb_VC1.c           	\
    psb_buffer.c            \
    psb_buffer_dm.c         \
    psb_cmdbuf.c            \
    psb_deblock.c           \
    psb_drv_video.c         \
    psb_output.c		\
    psb_texstreaming.c            \
    psb_texture.c            \
    android/psb_output_android.c            \
    android/psb_android_glue.cpp            \
    psb_surface.c           \
    psb_overlay.c		\
    psb_ws_driver.c         \
    vc1_idx.c           	\
    vc1_vlc.c		\
    pnw_H263ES.c		\
    pnw_H264.c		\
    pnw_H264ES.c		\
    pnw_MPEG2.c		\
    pnw_MPEG4.c		\
    pnw_MPEG4ES.c		\
    pnw_VC1.c           \
    pnw_cmdbuf.c		\
    pnw_hostcode.c		\
    pnw_hostheader.c	\
    pnw_hostjpeg.c		\
    pnw_jpeg.c		\
    powervr_iep_lite/csc/csc2.c \
    powervr_iep_lite/csc/csc2_data.c \
    powervr_iep_lite/fixedpointmaths/fixedpointmaths.c \
    powervr_iep_lite/iep_lite/iep_lite_api.c \
    powervr_iep_lite/iep_lite/iep_lite_hardware.c \
    powervr_iep_lite/iep_lite/iep_lite_utils.c

LOCAL_CFLAGS := -DLINUX -DANDROID -g -Wall -Wno-unused

LOCAL_C_INCLUDES :=			\
    $(TOPDIR)hardware/intel/include         \
    $(TOPDIR)hardware/intel/include/eurasia/pvr2d              \
    $(TARGET_OUT_HEADERS)/libva	\
    $(TOPDIR)hardware/intel/include/drm     \
    $(TARGET_OUT_HEADERS)/libttm	\
    $(TARGET_OUT_HEADERS)/libmemrar	\
    $(TARGET_OUT_HEADERS)/libwsbm	\
    $(TARGET_OUT_HEADERS)/libpsb_drm\
    $(TARGET_OUT_HEADERS)/opengles  \
    $(LOCAL_PATH)/hwdefs		\
    $(LOCAL_PATH)/powervr_iep_lite/include		\
    $(LOCAL_PATH)/powervr_iep_lite/include/win32	\
    $(LOCAL_PATH)/powervr_iep_lite/csc		\
    $(LOCAL_PATH)/powervr_iep_lite/iep_lite		\
    $(LOCAL_PATH)/powervr_iep_lite/fixedpointmaths

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := pvr_drv_video

LOCAL_SHARED_LIBRARIES := libdl libdrm libwsbm libmemrar libpvr2d libcutils \
                libui libutils libbinder libsurfaceflinger_client

ifeq ($(strip $(PSBVIDEO_LOG_ENABLE)),true)
LOCAL_CFLAGS += -DPSBVIDEO_LOG_ENABLE
LOCAL_SHARED_LIBRARIES += liblog
endif

include $(BUILD_SHARED_LIBRARY)
endif

