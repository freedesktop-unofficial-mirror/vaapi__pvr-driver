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
 *
 */


#ifdef __cplusplus
extern "C"
{
#endif
    unsigned char* psb_android_registerBuffers(void** surface, int pid, int width, int height);

    void psb_android_postBuffer(int offset);

    void psb_android_clearHeap();

    void psb_android_texture_streaming_display(int buffer_index);

    void psb_android_texture_streaming_set_texture_dim(unsigned short srcw,
            unsigned short srch);

    void psb_android_texture_streaming_set_rotate(int rotate);

    void psb_android_texture_streaming_set_crop(short srcx,
            short srcy,
            unsigned short srcw,
            unsigned short srch);

    void psb_android_texture_streaming_set_blend(short destx,
            short desty,
            unsigned short destw,
            unsigned short desth,
            unsigned int border_color,
            unsigned int blend_color,
            unsigned short blend_mode);

    void psb_android_texture_streaming_set_background_color(unsigned int background_color);
    void psb_android_texture_streaming_resetParams();
    void psb_android_texture_streaming_destroy();

    int psb_android_register_isurface(void** surface, int bcd_id, int srcw, int srch);
    int psb_android_surfaceflinger_status(void** surface, int *sf_compostion, int *rotation, int *widi);

    void psb_android_get_destbox(short* destx, short* desty, unsigned short* destw, unsigned short* desth);
    int psb_android_dynamic_source_init(void** android_isurface, int bcd_id, uint32_t srcw, uint32_t srch, uint32_t stride);
    void psb_android_dynamic_source_display(int buffer_index, int hdmi_mode);
    void psb_android_dynamic_source_destroy();

#ifdef __cplusplus
}
#endif
