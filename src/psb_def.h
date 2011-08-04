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

#ifndef _PSB_DEF_H_
#define _PSB_DEF_H_

#include <assert.h>
#include <string.h>

/* #define VA_EMULATOR 1 */

/* #define DEBUG_TRACE */
/* #define DEBUG_TRACE_VERBOSE */


#ifdef DEBUG_TRACE

#ifndef ASSERT
#define ASSERT  assert
#endif

#ifndef IMG_ASSERT
#define IMG_ASSERT  assert
#endif

#else /* DEBUG_TRACE */

#undef ASSERT
#undef IMG_ASSERT

#define ASSERT(x)
#define IMG_ASSERT(x)

#endif /* DEBUG_TRACE */

#ifndef FALSE
#define FALSE   0
#endif

#ifndef TRUE
#define TRUE    1
#endif


void psb__error_message(const char *msg, ...);
void psb__information_message(const char *msg, ...);
void psb__trace_message(const char *msg, ...);


#define DEBUG_FAILURE           while(vaStatus) {psb__information_message("%s fails with '%d' at %s:%d\n", __FUNCTION__, vaStatus, __FILE__, __LINE__);break;}
#define DEBUG_FAILURE_RET       while(ret)              {psb__information_message("%s fails with '%s' at %s:%d\n", __FUNCTION__, strerror(ret < 0 ? -ret : ret), __FILE__, __LINE__);break;}


#ifndef VA_FOURCC_YV16
#define VA_FOURCC_YV16 0x36315659
#endif
#endif /* _PSB_DEF_H_ */
