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
 */


/*
 * Authors:
 *    Waldo Bastian <waldo.bastian@intel.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thread0_ss_bin.h" /* old sliceswitching firmware */
#include "thread0_bin.h" /* new frameswitching firmware for error concealment */
#include "thread1_bin.h"

#define MDFLD_SLICESWITCH_FIRMWARE 1

struct msvdx_fw {
    unsigned int ver;
    unsigned int  text_size;
    unsigned int data_size;
    unsigned int data_location;
};

int main()
{
    unsigned long i = 0;
    unsigned long lseek;
    FILE *ptr = NULL;

    /* Create msvdx firmware for mrst */

    struct msvdx_fw fw;
    fw.ver = 944;
    fw.text_size = ui32MTXDXVAFWTextSize_ss;
    fw.data_size = ui32MTXDXVAFWDataSize_ss;
    fw.data_location = ui32MTXDXVAFWDataLocation_ss;

    ptr = fopen("msvdx_fw.bin", "w");
    if (ptr == NULL) {
        fprintf(stderr, "Create msvdx_fw_mrst.bin failed\n");
        exit(-1);
    }
    fwrite(&fw, sizeof(fw), 1, ptr);

    for (i = 0; i < fw.text_size; i++) {
        fwrite(&aui32MTXDXVAFWText_ss[i], 4, 1, ptr);
    }
    for (i = 0; i < fw.data_size; i++) {
        fwrite(&aui32MTXDXVAFWData_ss[i], 4, 1, ptr);
    }

    lseek = ((sizeof(fw) + (fw.text_size + fw.data_size) * 4 + 0xfff) & ~0xfff);
    fseek(ptr, lseek, SEEK_SET);

    /* Create msvdx firmware for mrst error concealment*/
    /*  fw.ver = 0xec;
            fw.text_size = ui32sMiniSSLegacyTextSize;
            fw.data_size = ui32sMiniSSLegacyDataSize;
            fw.data_location = ui32sMiniSSLegacyDataLocation;

        fwrite( &fw, sizeof(fw), 1, ptr);

        for (i = 0; i < fw.text_size; i++)
        {
                fwrite( &aui32sMiniSSLegacyText[i],4, 1, ptr);
        }
        for (i = 0; i < fw.data_size; i++)
        {
                fwrite( &aui32sMiniSSLegacyData[i],4, 1, ptr);
        }
        fclose (ptr);
    */
    fw.ver = 0x4cd;
    fw.text_size = ui32MTXDXVAFWTextSize;
    fw.data_size = ui32MTXDXVAFWDataSize;
    fw.data_location = ui32MTXDXVAFWDataLocation;

    fwrite(&fw, sizeof(fw), 1, ptr);

    for (i = 0; i < fw.text_size; i++) {
        fwrite(&aui32MTXDXVAFWText[i], 4, 1, ptr);
    }
    for (i = 0; i < fw.data_size; i++) {
        fwrite(&aui32MTXDXVAFWData[i], 4, 1, ptr);
    }
    fclose(ptr);

#if MDFLD_SLICESWITCH_FIRMWARE
    /* Create msvdx firmware for mfld */
    fw.ver = 0x2;
    fw.text_size = sSliceSwitchingFirmware.uiTextSize / 4;
    fw.data_size = sSliceSwitchingFirmware.uiDataSize / 4;;
    fw.data_location = sSliceSwitchingFirmware.DataOffset + 0x82880000;

    ptr = fopen("msvdx_fw_mfld.bin", "w");
    if (ptr == NULL) {
        fprintf(stderr, "Create msvdx_fw_mfld.bin failed\n");
        exit(-1);
    }
    fwrite(&fw, sizeof(fw), 1, ptr);

    for (i = 0; i < fw.text_size; i++) {
        fwrite(&sSliceSwitchingFirmware.pui8Text[i*4], 4, 1, ptr);
    }
    for (i = 0; i < fw.data_size; i++) {
        fwrite(&sSliceSwitchingFirmware.pui8Data[i*4], 4, 1, ptr);
    }
    fclose(ptr);

#else
    fw.ver = 0x2;
    fw.text_size = sFrameSwitchingFirmware.uiTextSize / 4;
    fw.data_size = sFrameSwitchingFirmware.uiDataSize / 4;;
    fw.data_location = sFrameSwitchingFirmware.DataOffset + 0x82880000;

    ptr = fopen("msvdx_fw_mfld.bin", "w");
    if (ptr == NULL) {
        fprintf(stderr, "Create msvdx_fw_mfld.bin failed\n");
        exit(-1);
    }
    fwrite(&fw, sizeof(fw), 1, ptr);

    for (i = 0; i < fw.text_size; i++) {
        fwrite(&sFrameSwitchingFirmware.pui8Text[i*4], 4, 1, ptr);
    }
    for (i = 0; i < fw.data_size; i++) {
        fwrite(&sFrameSwitchingFirmware.pui8Data[i*4], 4, 1, ptr);
    }
    fclose(ptr);
#endif

    /* Create msvdx firmware for mfld DE2.0 */

    FIRMWARE fw_DE2;

    /* fw_DE2 = sFirmware1133_SS; */
    fw_DE2 = sFirmware1163_SS;
    /* fw_DE2 = sFirmware1163_FS; */
    /* fw_DE2 = sFirmware1133_FS; */

    fw.ver = 0x0496;
    fw.text_size = fw_DE2.uiTextSize / 4;
    fw.data_size = fw_DE2.uiDataSize / 4;;
    fw.data_location = fw_DE2.DataOffset + 0x82880000;

    ptr = fopen("msvdx_fw_mfld_DE2.0.bin", "w");
    if (ptr == NULL) {
        fprintf(stderr, "Create msvdx_fw_mfld_DE2.0.bin failed\n");
        exit(-1);
    }
    fwrite(&fw, sizeof(fw), 1, ptr);

    for (i = 0; i < fw.text_size; i++) {
        fwrite(&fw_DE2.pui8Text[i*4], 4, 1, ptr);
    }
    for (i = 0; i < fw.data_size; i++) {
        fwrite(&fw_DE2.pui8Data[i*4], 4, 1, ptr);
    }
    fclose(ptr);

    return 0;
}

