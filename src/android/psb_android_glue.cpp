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
 *    Zhaohan Ren  <zhaohan.ren@intel.com>
 *    Jiang Fei  <jiang.fei@intel.com>
 *    Binglin Chen <binglin.chen@intel.com>
 *
 */


#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <surfaceflinger/Surface.h>
#include <surfaceflinger/ISurface.h>
#include <surfaceflinger/SurfaceComposerClient.h>
#include <surfaceflinger/ISurfaceComposer.h>
#include <binder/MemoryHeapBase.h>
#include "psb_android_glue.h"
#include "psb_texstreaming.h"
#include <cutils/log.h>
#include <ui/Rect.h>

using namespace android;

#ifdef  LOG_TAG
#undef  LOG_TAG
#endif

#define LOG_TAG "pvr_drv_video"

sp<ISurface> isurface;
ISurface::BufferHeap mBufferHeap;
unsigned int update_forced;

unsigned char* psb_android_registerBuffers(void** android_isurface, int pid, int width, int height)
{
    sp<MemoryHeapBase> heap;
    int framesize = width * height * 2;

    heap = new MemoryHeapBase(framesize);
    if (heap->heapID() < 0) {
        printf("Error creating frame buffer heap");
        return 0;
    }

    mBufferHeap = ISurface::BufferHeap(width, height, width, height, PIXEL_FORMAT_RGB_565, heap);

    isurface = static_cast<ISurface*>(*android_isurface);

    isurface->registerBuffers(mBufferHeap);

    return static_cast<uint8_t*>(mBufferHeap.heap->base());
}

void psb_android_postBuffer(int offset)
{
    if (isurface.get())
        isurface->postBuffer(offset);
}

//called in DestroySurfaces
void psb_android_clearHeap()
{
    if (isurface.get()) {
        isurface->unregisterBuffers();
        mBufferHeap.heap.clear();
    }
}

int psb_android_register_isurface(void** android_isurface, int bcd_id, int srcw, int srch)
{
    if (isurface != *android_isurface) {
        isurface = static_cast<ISurface*>(*android_isurface);
        if (isurface.get()) {
            isurface->createTextureStreamSource();
            LOGD("In psb_android_register_isurface: buffer_device_id is %d, srcw is %d, srch is %d.\n", bcd_id, srcw, srch);
            isurface->setTextureStreamID(bcd_id);
            isurface->setTextureStreamDim(srcw, srch);
            isurface->resetTextureStreamParams();
            update_forced = 1;
            return 0;
        } else {
            return -1;
        }
    }
    update_forced = 0;
    return 0;
}

void psb_android_texture_streaming_set_texture_dim(unsigned short srcw,
        unsigned short srch)
{
    static short saved_srcw, saved_srch;
    if (isurface.get() &&
        (update_forced || (saved_srcw != srcw) || (saved_srch != srch))) {
#if 0
        /*
        Resolve issue - Green line in the bottom of display while video is played
        This issue is caused by the buffer size larger than video size and texture linear filtering.
        The pixels of last line will computed from the pixels out of video picture
        */
        if ((srch  & 0x1f) == 0)
            isurface->setTextureStreamDim(srcw, srch);
        else
            isurface->setTextureStreamDim(srcw, srch - 1);
#else
        LOGD("In psb_android_texture_streaming_set_texture_dim: srcw is %d, srch is %d.\n", srcw, srch);
        /*surface flinger will do the upper height correction*/
        isurface->setTextureStreamDim(srcw, srch);
#endif
        saved_srcw = srcw;
        saved_srch = srch;
    }
}

void psb_android_texture_streaming_set_crop(short srcx,
        short srcy,
        unsigned short srcw,
        unsigned short srch)
{
    static short saved_srcx, saved_srcy, saved_srcw, saved_srch;
    if (isurface.get() &&
        (update_forced || (saved_srcx != srcx) || (saved_srcy != srcy) || (saved_srcw != srcw) || (saved_srch != srch))) {
        /*assume crop will only be called from app layer*/
        isurface->setTextureStreamClipRect(srcx, srcy, srcw, srch);
        saved_srcx = srcx;
        saved_srcy = srcy;
        saved_srcw = srcw;
        saved_srch = srch;
    }
}


void psb_android_texture_streaming_set_rotate(int rotate)
{
    if (isurface.get()) {
        isurface->setTextureStreamRotate(rotate);
    }
}

void psb_android_texture_streaming_set_blend(short destx,
        short desty,
        unsigned short destw,
        unsigned short desth,
        unsigned int background_color,
        unsigned int blend_color,
        unsigned short blend_mode)
{
    static unsigned short saved_destx, saved_desty, saved_destw, saved_desth;
    static unsigned int saved_background_color, saved_blend_color;
    static int saved_blend_mode = -1;
    unsigned short bg_red = 0, bg_green = 0, bg_blue = 0, bg_alpha = 0;
    unsigned short blend_red = 0, blend_green = 0, blend_blue = 0, blend_alpha = 0;

    if (saved_background_color != background_color) {
        bg_alpha = (background_color & 0xff000000) >> 24;
        bg_red = (background_color & 0xff0000) >> 16;
        bg_green = (background_color & 0xff00) >> 8;
        bg_blue = background_color & 0xff;
        saved_background_color = background_color;
    }

    if (saved_blend_color != blend_color) {
        blend_alpha = (blend_color & 0xff000000) >> 24;
        blend_red = (blend_color & 0xff0000) >> 16;
        blend_green = (blend_color & 0xff00) >> 8;
        blend_blue = blend_color & 0xff;
        saved_blend_color = blend_color;
    }

    if (isurface.get()) {
        if (update_forced || (saved_destx != destx) || (saved_desty != desty) || (saved_destw != destw) || (saved_desth != desth)) {
            isurface->setTextureStreamPosRect(destx, desty, destw, desth);
            saved_destx = destx;
            saved_desty = desty;
            saved_destw = destw;
            saved_desth = desth;
        }
        if (update_forced || (saved_background_color != background_color))
            isurface->setTextureStreamBorderColor(bg_red, bg_green, bg_blue, bg_alpha);
        if (update_forced || (saved_blend_color != blend_color))
            isurface->setTextureStreamVideoColor(blend_red, blend_green, blend_blue, blend_alpha);
        if (update_forced || (saved_blend_mode != blend_mode)) {
            isurface->setTextureStreamBlendMode(blend_mode);
            saved_blend_mode = blend_mode;
        }
    }
}

void psb_android_texture_streaming_set_background_color(unsigned int background_color)
{
    unsigned short bg_red, bg_green, bg_blue, bg_alpha;

    bg_alpha = (background_color & 0xff000000) >> 24;
    bg_red = (background_color & 0xff0000) >> 16;
    bg_green = (background_color & 0xff00) >> 8;
    bg_blue = background_color & 0xff;

    if (isurface.get()) {
        isurface->setTextureStreamPosRect(0, 0, 0, 0);
        isurface->setTextureStreamBorderColor(bg_red, bg_green, bg_blue, bg_alpha);
    }
}

void psb_android_texture_streaming_display(int buffer_index)
{
    if (isurface.get())
        isurface->displayTextureStreamBuffer(buffer_index);
}

void psb_android_texture_streaming_resetParams()
{
    if (isurface.get()) {
        isurface->resetTextureStreamParams();
    }
}

void psb_android_texture_streaming_destroy()
{
    if (isurface.get())
        isurface->destroyTextureStreamSource();
    isurface = NULL;
}

int psb_android_surfaceflinger_status(void** android_isurface, int *sf_compositioin, int *rotation, int *widi)
{
    sp<ISurface> tmp_isurface;

    tmp_isurface = static_cast<ISurface*>(*android_isurface);
    if (tmp_isurface.get()) {
        uint32_t pm = tmp_isurface->getVideoPostMethod();

        if ((pm & 0xff) == ISurfaceComposer::eVideoPostOverlay)
            *sf_compositioin = 0; /* no composition */
        else
            *sf_compositioin = 1; /* with composition */
        *rotation = (pm >> 8) & 0xff;
        *widi = (pm >> 16) & 0xff;
    }

    return 0;
}

void psb_android_get_destbox(short* destx, short* desty, unsigned short* destw, unsigned short* desth)
{
    if (isurface.get()) {
        Rect pm = isurface->getVisibleRect();
        *destx = pm.left;
        *desty = pm.top;
        *destw = pm.width();
        *desth = pm.height();
    }
}


