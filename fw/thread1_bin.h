
/******************************************************************************

 @File         thread0_bin.h

 @Title

 @Copyright    Copyright (C)  Imagination Technologies Limited. All Rights Reserved.

 @Platform

 @Description

******************************************************************************/
#ifdef DE_ENV
#include "global.h"
#endif
//#if SLICE_SWITCHING_VARIANT
// This file was automatically generated from ../release/thread0.dnl using dnl2c.

/*
extern unsigned long aui32MTXDXVAFWText[];
extern unsigned long ui32MTXDXVAFWTextSize;

extern unsigned long aui32MTXDXVAFWData[];
extern unsigned long ui32MTXDXVAFWDataSize;

extern unsigned long ui32MTXDXVAFWDataLocation;
*/
typedef struct {
    const char* psVersion;
    const char* psBuildDate;
    unsigned int uiTextSize;
    unsigned int uiDataSize;
    unsigned int DataOffset;
    const unsigned char* pui8Text;
    const unsigned char* pui8Data;
} FIRMWARE;

//extern const FIRMWARE sSliceSwitchingFirmware;
extern const FIRMWARE sFrameSwitchingFirmware;
extern const FIRMWARE sSliceSwitchingFirmware;
extern const FIRMWARE sFirmware1100_SS;
extern const FIRMWARE sFirmware1133_SS;
extern const FIRMWARE sFirmware1133_FS;
extern const FIRMWARE sFirmware1163_SS;
extern const FIRMWARE sFirmware1163_FS;
extern const FIRMWARE sFirmware1300_SS;
extern const FIRMWARE sFirmware1311_SS;
extern const FIRMWARE sFirmware1313_SS;
#define FIRMWARE_VERSION_DEFINED
#define FIRMWARE_BUILDDATE_DEFINED
//#endif /* SLICE_SWITCHING_VARIANT */
