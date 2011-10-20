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
 *    Elaine Wang <elaine.wang@intel.com>
 *    Zeng Li <zeng.li@intel.com>
 *
 */


#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "psb_def.h"
#include "psb_surface.h"
#include "psb_cmdbuf.h"
#include "pnw_hostcode.h"
#include "pnw_H264ES.h"
#include "pnw_hostheader.h"

#define TOPAZ_H264_MAX_BITRATE 50000000

#define INIT_CONTEXT_H264ES     context_ENC_p ctx = (context_ENC_p) obj_context->format_data
#define SURFACE(id)    ((object_surface_p) object_heap_lookup( &ctx->obj_context->driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &ctx->obj_context->driver_data->buffer_heap, id ))


static void pnw_H264ES_QueryConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs)
{
    int i;

    psb__information_message("pnw_H264ES_QueryConfigAttributes\n");

    /* RateControl attributes */
    for (i = 0; i < num_attribs; i++) {
        switch (attrib_list[i].type) {
        case VAConfigAttribRTFormat:
            break;

        case VAConfigAttribRateControl:
            attrib_list[i].value = VA_RC_NONE | VA_RC_CBR | VA_RC_VBR | VA_RC_VCM;
            break;

        default:
            attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
            break;
        }
    }
}


static VAStatus pnw_H264ES_ValidateConfig(
    object_config_p obj_config)
{
    int i;
    /* Check all attributes */
    for (i = 0; i < obj_config->attrib_count; i++) {
        switch (obj_config->attrib_list[i].type) {
        case VAConfigAttribRTFormat:
            /* Ignore */
            break;
        case VAConfigAttribRateControl:
            break;
        default:
            return VA_STATUS_ERROR_ATTR_NOT_SUPPORTED;
        }
    }

    return VA_STATUS_SUCCESS;
}


static VAStatus pnw_H264ES_CreateContext(
    object_context_p obj_context,
    object_config_p obj_config)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    context_ENC_p ctx;
    int i;
    unsigned int eRCmode;

    psb__information_message("pnw_H264ES_CreateContext\n");

    vaStatus = pnw_CreateContext(obj_context, obj_config, 0);

    if (VA_STATUS_SUCCESS != vaStatus)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    ctx = (context_ENC_p) obj_context->format_data;

    for (i = 0; i < obj_config->attrib_count; i++) {
        if (obj_config->attrib_list[i].type == VAConfigAttribRateControl)
            break;
    }

    if (i >= obj_config->attrib_count)
        eRCmode = VA_RC_NONE;
    else
        eRCmode = obj_config->attrib_list[i].value;


    if (eRCmode == VA_RC_VBR) {
        ctx->eCodec = IMG_CODEC_H264_VBR;
        ctx->sRCParams.RCEnable = IMG_TRUE;
    } else if (eRCmode == VA_RC_CBR) {
        ctx->eCodec = IMG_CODEC_H264_CBR;
        ctx->sRCParams.RCEnable = IMG_TRUE;
    } else if (eRCmode == VA_RC_NONE) {
        ctx->eCodec = IMG_CODEC_H264_NO_RC;
        ctx->sRCParams.RCEnable = IMG_FALSE;
    } else if (eRCmode == VA_RC_VCM) {
        ctx->eCodec = IMG_CODEC_H264_VCM;
        ctx->sRCParams.RCEnable = IMG_TRUE;
    } else
        return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

    psb__information_message("eCodec is %d\n", ctx->eCodec);
    ctx->eFormat = IMG_CODEC_PL12;      /* use default */

    ctx->Slices = 1;
    ctx->idr_pic_id = 1;

    if (getenv("PSB_VIDEO_SIG_CORE") == NULL) {
        ctx->Slices = 2;
        ctx->NumCores = 2;
    }

    ctx->ParallelCores = min(ctx->NumCores, ctx->Slices);

    ctx->IPEControl = pnw__get_ipe_control(ctx->eCodec);

    switch (obj_config->profile) {
    case VAProfileH264Baseline:
        ctx->profile_idc = 5;
        break;
    case VAProfileH264Main:
        ctx->profile_idc = 6;
        break;
    default:
        ctx->profile_idc = 6;
        break;
    }

    return vaStatus;
}

static void pnw_H264ES_DestroyContext(
    object_context_p obj_context)
{
    psb__information_message("pnw_H264ES_DestroyPicture\n");

    pnw_DestroyContext(obj_context);
}

static VAStatus pnw_H264ES_BeginPicture(
    object_context_p obj_context)
{
    INIT_CONTEXT_H264ES;
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    psb__information_message("pnw_H264ES_BeginPicture\n");

    vaStatus = pnw_BeginPicture(ctx);

    return vaStatus;
}

static VAStatus pnw__H264ES_process_sequence_param(context_ENC_p ctx, object_buffer_p obj_buffer)
{
    VAEncSequenceParameterBufferH264 *pSequenceParams;
    pnw_cmdbuf_p cmdbuf = ctx->obj_context->pnw_cmdbuf;
    H264_VUI_PARAMS VUI_Params;
    H264_CROP_PARAMS sCrop;
    int i;

    ASSERT(obj_buffer->type == VAEncSequenceParameterBufferType);
    ASSERT(obj_buffer->num_elements == 1);
    ASSERT(obj_buffer->size == sizeof(VAEncSequenceParameterBufferH264));

    if ((obj_buffer->num_elements != 1) ||
        (obj_buffer->size != sizeof(VAEncSequenceParameterBufferH264))) {
        return VA_STATUS_ERROR_UNKNOWN;
    }

    ctx->obj_context->frame_count = 0;

    pSequenceParams = (VAEncSequenceParameterBufferH264 *) obj_buffer->buffer_data;
    obj_buffer->buffer_data = NULL;
    obj_buffer->size = 0;

    if (!pSequenceParams->bits_per_second) {
        pSequenceParams->bits_per_second = ctx->Height * ctx->Width * 30 * 12;
        psb__information_message("bits_per_second is 0, set to %d\n",
                                 pSequenceParams->bits_per_second);
    }
    ctx->sRCParams.bBitrateChanged =
        (pSequenceParams->bits_per_second == ctx->sRCParams.BitsPerSecond ?
         IMG_FALSE : IMG_TRUE);

    if (pSequenceParams->bits_per_second > TOPAZ_H264_MAX_BITRATE) {
        ctx->sRCParams.BitsPerSecond = TOPAZ_H264_MAX_BITRATE;
        psb__information_message(" bits_per_second(%d) exceeds \
		the maximum bitrate, set it with %d\n",
                                 pSequenceParams->bits_per_second,
                                 TOPAZ_H264_MAX_BITRATE);
    } else
        ctx->sRCParams.BitsPerSecond = pSequenceParams->bits_per_second;

    ctx->sRCParams.FrameRate = pSequenceParams->frame_rate;
    ctx->sRCParams.InitialQp = pSequenceParams->initial_qp;
    ctx->sRCParams.MinQP = pSequenceParams->min_qp;
    ctx->sRCParams.BUSize = pSequenceParams->basic_unit_size;

    ctx->sRCParams.Slices = ctx->Slices;
    ctx->sRCParams.QCPOffset = 0;/* FIXME */
    ctx->sRCParams.IntraFreq = pSequenceParams->intra_period;

    ctx->sRCParams.IDRFreq = pSequenceParams->intra_idr_period;
    /*if (ctx->sRCParams.BitsPerSecond < 256000)
        ctx->sRCParams.BufferSize = (9 * ctx->sRCParams.BitsPerSecond) >> 1;
    else
        ctx->sRCParams.BufferSize = (5 * ctx->sRCParams.BitsPerSecond) >> 1;*/

    ctx->sRCParams.BufferSize = ctx->sRCParams.BitsPerSecond;
    ctx->sRCParams.InitialLevel = (3 * ctx->sRCParams.BufferSize) >> 4;
    ctx->sRCParams.InitialDelay = (13 * ctx->sRCParams.BufferSize) >> 4;

    if (ctx->obj_context->frame_count == 0) { /* Add Register IO behind begin Picture */
        pnw__UpdateRCBitsTransmitted(ctx);
        for (i = (ctx->ParallelCores - 1); i >= 0; i--) {
            pnw_set_bias(ctx, i);
        }
        /* This is needed in old firmware, now is no needed any more */
        //ctx->initial_qp_in_cmdbuf = cmdbuf->cmd_idx; /* remember the place */
        //cmdbuf->cmd_idx++;
    }

    VUI_Params.Time_Scale = ctx->sRCParams.FrameRate * 2;
    VUI_Params.bit_rate_value_minus1 = ctx->sRCParams.BitsPerSecond / 64 - 1;
    VUI_Params.cbp_size_value_minus1 = ctx->sRCParams.BitsPerSecond / 640 - 1;
    VUI_Params.CBR = ((IMG_CODEC_H264_CBR == ctx->eCodec) ? 1 : 0);
    VUI_Params.initial_cpb_removal_delay_length_minus1 = BPH_SEI_NAL_INITIAL_CPB_REMOVAL_DELAY_SIZE - 1;
    VUI_Params.cpb_removal_delay_length_minus1 = PTH_SEI_NAL_CPB_REMOVAL_DELAY_SIZE - 1;
    VUI_Params.dpb_output_delay_length_minus1 = PTH_SEI_NAL_DPB_OUTPUT_DELAY_SIZE - 1;
    VUI_Params.time_offset_length = 24;

    sCrop.bClip = IMG_FALSE;
    sCrop.LeftCropOffset = 0;
    sCrop.RightCropOffset = 0;
    sCrop.TopCropOffset = 0;
    sCrop.BottomCropOffset = 0;

    if (ctx->RawHeight & 0xf) {
        sCrop.bClip = IMG_TRUE;
        sCrop.BottomCropOffset = (((ctx->RawHeight + 0xf) & (~0xf)) - ctx->RawHeight) / 2;
    }
    if (ctx->RawWidth & 0xf) {
        sCrop.bClip = IMG_TRUE;
        sCrop.RightCropOffset = (((ctx->RawWidth + 0xf) & (~0xf)) - ctx->RawWidth) / 2;
    }
    /* sequence header is always inserted */

    /* Not know why set VUI_Params.CBR = 0; */
    /*
    if (ctx->eCodec== IMG_CODEC_H264_NO_RC)
        VUI_Params.CBR = 0;
    */
    memset(cmdbuf->header_mem_p + ctx->seq_header_ofs,
           0,
           HEADER_SIZE);

    if (ctx->eCodec == IMG_CODEC_H264_NO_RC)
        pnw__H264_prepare_sequence_header(cmdbuf->header_mem_p + ctx->seq_header_ofs,
                                          pSequenceParams->picture_width_in_mbs,
                                          pSequenceParams->picture_height_in_mbs,
                                          pSequenceParams->vui_flag,
                                          pSequenceParams->vui_flag ? (&VUI_Params) : NULL,
                                          &sCrop,
                                          pSequenceParams->level_idc, ctx->profile_idc);
    else
        pnw__H264_prepare_sequence_header(cmdbuf->header_mem_p + ctx->seq_header_ofs,
                                          pSequenceParams->picture_width_in_mbs,
                                          pSequenceParams->picture_height_in_mbs,
                                          pSequenceParams->vui_flag,
                                          pSequenceParams->vui_flag ? (&VUI_Params) : NULL,
                                          &sCrop,
                                          pSequenceParams->level_idc, ctx->profile_idc);

    /*Periodic IDR need SPS. We save the sequence header here*/
    if (ctx->sRCParams.IDRFreq != 0) {
        if (NULL == ctx->save_seq_header_p) {
            ctx->save_seq_header_p = malloc(HEADER_SIZE);
            if (NULL == ctx->save_seq_header_p) {
                psb__error_message("Ran out of memory!\n");
                free(pSequenceParams);
                return VA_STATUS_ERROR_ALLOCATION_FAILED;
            }
            memcpy((unsigned char *)ctx->save_seq_header_p,
                   (unsigned char *)(cmdbuf->header_mem_p + ctx->seq_header_ofs),
                   HEADER_SIZE);
        }
    }

    cmdbuf->cmd_idx_saved[PNW_CMDBUF_SEQ_HEADER_IDX] = cmdbuf->cmd_idx;
    /* Send to the last core as this will complete first */
    pnw_cmdbuf_insert_command_package(ctx->obj_context,
                                      ctx->ParallelCores - 1,
                                      MTX_CMDID_DO_HEADER,
                                      &cmdbuf->header_mem,
                                      ctx->seq_header_ofs);
    free(pSequenceParams);

    return VA_STATUS_SUCCESS;
}

static VAStatus pnw__H264ES_process_picture_param(context_ENC_p ctx, object_buffer_p obj_buffer)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int i;
    VAEncPictureParameterBufferH264 *pBuffer;
    int need_sps = 0;
    pnw_cmdbuf_p cmdbuf = ctx->obj_context->pnw_cmdbuf;

    ASSERT(obj_buffer->type == VAEncPictureParameterBufferType);

    if ((obj_buffer->num_elements != 1) ||
        (obj_buffer->size != sizeof(VAEncPictureParameterBufferH264))) {
        return VA_STATUS_ERROR_UNKNOWN;
    }

    /* Transfer ownership of VAEncPictureParameterBufferH264 data */
    pBuffer = (VAEncPictureParameterBufferH264 *) obj_buffer->buffer_data;
    obj_buffer->buffer_data = NULL;
    obj_buffer->size = 0;

    ctx->ref_surface = SURFACE(pBuffer->reference_picture);
    ctx->dest_surface = SURFACE(pBuffer->reconstructed_picture);
    ctx->coded_buf = BUFFER(pBuffer->coded_buf);

    ASSERT(ctx->Width == pBuffer->picture_width);
    ASSERT(ctx->Height == pBuffer->picture_height);

    if (NULL == ctx->coded_buf) {
        psb__error_message("%s L%d Invalid coded buffer handle\n", __FUNCTION__, __LINE__);
        free(pBuffer);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    if ((ctx->sRCParams.IntraFreq != 0) && (ctx->sRCParams.IDRFreq != 0)) { /* period IDR is desired */
        unsigned int is_intra = 0;
        unsigned int intra_cnt = 0;

        ctx->force_idr_h264 = 0;

        if ((ctx->obj_context->frame_count % ctx->sRCParams.IntraFreq) == 0) {
            is_intra = 1; /* suppose current frame is I frame */
            intra_cnt = ctx->obj_context->frame_count / ctx->sRCParams.IntraFreq;
        }

        /* current frame is I frame (suppose), and an IDR frame is desired*/
        if ((is_intra) && ((intra_cnt % ctx->sRCParams.IDRFreq) == 0)) {
            ctx->force_idr_h264 = 1;
            /*When two consecutive access units in decoding order are both IDR access
             * units, the value of idr_pic_id in the slices of the first such IDR
             * access unit shall differ from the idr_pic_id in the second such IDR
             * access unit. We set it with 1 or 0 alternately.*/
            ctx->idr_pic_id = 1 - ctx->idr_pic_id;

            /* it is periodic IDR in the middle of one sequence encoding, need SPS */
            if (ctx->obj_context->frame_count > 0)
                need_sps = 1;

            ctx->obj_context->frame_count = 0;
        }
    }
    /* For H264, PicHeader only needed in the first picture*/
    if (!(ctx->obj_context->frame_count)) {
        cmdbuf = ctx->obj_context->pnw_cmdbuf;
        cmdbuf->cmd_idx_saved[PNW_CMDBUF_PIC_HEADER_IDX] = cmdbuf->cmd_idx;

        if (need_sps) {
            psb__information_message("TOPAZ: insert a SPS before IDR frame\n");
            /* reuse the previous SPS */
            memcpy((unsigned char *)(cmdbuf->header_mem_p + ctx->seq_header_ofs),
                   (unsigned char *)ctx->save_seq_header_p,
                   HEADER_SIZE);

            cmdbuf->cmd_idx_saved[PNW_CMDBUF_SEQ_HEADER_IDX] = cmdbuf->cmd_idx;
            /* Send to the last core as this will complete first */
            pnw_cmdbuf_insert_command_package(ctx->obj_context,
                                              ctx->ParallelCores - 1,
                                              MTX_CMDID_DO_HEADER,
                                              &cmdbuf->header_mem,
                                              ctx->seq_header_ofs);
        }

        pnw__H264_prepare_picture_header(cmdbuf->header_mem_p + ctx->pic_header_ofs, IMG_FALSE, ctx->sRCParams.QCPOffset);

        /* Send to the last core as this will complete first */
        pnw_cmdbuf_insert_command_package(ctx->obj_context,
                                          ctx->ParallelCores - 1,
                                          MTX_CMDID_DO_HEADER,
                                          &cmdbuf->header_mem,
                                          ctx->pic_header_ofs);

    }

    if (ctx->ParallelCores == 1) {
        ctx->coded_buf_per_slice = 0;
        psb__information_message("TOPAZ: won't splite coded buffer(%d) since only one slice being encoded\n",
                                 ctx->coded_buf->size);
    } else {
        /*Make sure DMA start is 128bits alignment*/
        ctx->coded_buf_per_slice = (ctx->coded_buf->size / ctx->ParallelCores) & (~0xf) ;
        psb__information_message("TOPAZ: the size of coded_buf per slice %d( Total %d) \n", ctx->coded_buf_per_slice,
                                 ctx->coded_buf->size);
    }

    /* Prepare START_PICTURE params */
    /* FIXME is really need multiple picParams? Need multiple calculate for each? */
    for (i = (ctx->ParallelCores - 1); i >= 0; i--)
        vaStatus = pnw_RenderPictureParameter(ctx, i);

    free(pBuffer);
    return vaStatus;
}

static void pnw__H264ES_encode_one_slice(context_ENC_p ctx,
        VAEncSliceParameterBuffer *pBuffer)
{
    pnw_cmdbuf_p cmdbuf = ctx->obj_context->pnw_cmdbuf;
    unsigned int MBSkipRun, FirstMBAddress;
    unsigned char deblock_idc;
    unsigned char is_intra = 0;
    int slice_param_idx;
    PIC_PARAMS *psPicParams = (PIC_PARAMS *)(cmdbuf->pic_params_p);

    /*Slice encoding Order:
     *1.Insert Do header command
     *2.setup InRowParams
     *3.setup Slice params
     *4.Insert Do slice command
     * */

    MBSkipRun = (pBuffer->slice_height * ctx->Width) / 16;
    deblock_idc = pBuffer->slice_flags.bits.disable_deblocking_filter_idc;

    /*If the frame is skipped, it shouldn't be a I frame*/
    if (ctx->force_idr_h264 || (ctx->obj_context->frame_count == 0)) {
        is_intra = 1;
    } else
        is_intra = (ctx->sRCParams.RCEnable && ctx->sRCParams.FrameSkip) ? 0 : pBuffer->slice_flags.bits.is_intra;

    FirstMBAddress = (pBuffer->start_row_number * ctx->Width) / 16;

    memset(cmdbuf->header_mem_p + ctx->slice_header_ofs
           + ctx->obj_context->slice_count * HEADER_SIZE,
           0,
           HEADER_SIZE);

    /* Insert Do Header command, relocation is needed */
    pnw__H264_prepare_slice_header(cmdbuf->header_mem_p + ctx->slice_header_ofs
                                   + ctx->obj_context->slice_count * HEADER_SIZE,
                                   is_intra,
                                   pBuffer->slice_flags.bits.disable_deblocking_filter_idc,
                                   ctx->obj_context->frame_count,
                                   FirstMBAddress,
                                   MBSkipRun,
                                   0,
                                   ctx->force_idr_h264,
                                   IMG_FALSE,
                                   IMG_FALSE,
                                   ctx->idr_pic_id);

    /* ensure that this slice is consequtive to that last processed by the target core */
    /*
       ASSERT( -1 == ctx->LastSliceNum[ctx->SliceToCore]
       || ctx->obj_context->slice_count == 1 + ctx->LastSliceNum[ctx->SliceToCore] );
       */
    /* note the slice number the target core is now processing */
    ctx->LastSliceNum[ctx->SliceToCore] = ctx->obj_context->slice_count;

    pnw_cmdbuf_insert_command_package(ctx->obj_context,
                                      ctx->SliceToCore,
                                      MTX_CMDID_DO_HEADER,
                                      &cmdbuf->header_mem,
                                      ctx->slice_header_ofs + ctx->obj_context->slice_count * HEADER_SIZE);
    if (!(ctx->sRCParams.RCEnable && ctx->sRCParams.FrameSkip)) {
        /*Only reset on the first frame. It's more effective than DDK. Have confirmed with IMG*/
        if (ctx->obj_context->frame_count == 0)
            pnw_reset_encoder_params(ctx);
        if ((pBuffer->start_row_number == 0) && pBuffer->slice_flags.bits.is_intra) {
            ctx->BelowParamsBufIdx = (ctx->BelowParamsBufIdx + 1) & 0x1;
        }

        slice_param_idx = (pBuffer->slice_flags.bits.is_intra ? 0 : 1) * ctx->slice_param_num
                          + ctx->obj_context->slice_count;
        if (VAEncSliceParameter_Equal(&ctx->slice_param_cache[slice_param_idx], pBuffer) == 0) {
            /* cache current param parameters */
            memcpy(&ctx->slice_param_cache[slice_param_idx],
                   pBuffer, sizeof(VAEncSliceParameterBuffer));

            /* Setup InParams value*/
            pnw_setup_slice_params(ctx,
                                   pBuffer->start_row_number * 16,
                                   pBuffer->slice_height * 16,
                                   pBuffer->slice_flags.bits.is_intra,
                                   ctx->obj_context->frame_count > 0,
                                   psPicParams->sInParams.SeInitQP);
        }

        /* Insert do slice command and setup related buffer value */
        pnw__send_encode_slice_params(ctx,
                                      pBuffer->slice_flags.bits.is_intra,
                                      pBuffer->start_row_number * 16,
                                      deblock_idc,
                                      ctx->obj_context->frame_count,
                                      pBuffer->slice_height * 16,
                                      ctx->obj_context->slice_count);

        psb__information_message("Now frame_count/slice_count is %d/%d\n",
                                 ctx->obj_context->frame_count, ctx->obj_context->slice_count);
    }
    ctx->obj_context->slice_count++;

    return;
}

static VAStatus pnw__H264ES_process_slice_param(context_ENC_p ctx, object_buffer_p obj_buffer)
{
    /* Prepare InParams for macros of current slice, insert slice header, insert do slice command */
    VAEncSliceParameterBuffer *pBuf_per_core, *pBuffer;
    pnw_cmdbuf_p cmdbuf = ctx->obj_context->pnw_cmdbuf;
    PIC_PARAMS *psPicParams = (PIC_PARAMS *)(cmdbuf->pic_params_p);
    unsigned int i, j, slice_per_core;

    ASSERT(obj_buffer->type == VAEncSliceParameterBufferType);

    cmdbuf = ctx->obj_context->pnw_cmdbuf;
    psPicParams = (PIC_PARAMS *)cmdbuf->pic_params_p;

    /* Transfer ownership of VAEncPictureParameterBufferH264 data */
    pBuffer = (VAEncSliceParameterBuffer *) obj_buffer->buffer_data;
    obj_buffer->size = 0;

    /* H264: Calculate number of Mskip to skip IF this was a skip frame */

    /*In case the slice number changes*/
    if ((ctx->slice_param_cache != NULL) && (obj_buffer->num_elements != ctx->slice_param_num)) {
        psb__information_message("Slice number changes. Previous value is %d. Now it's %d\n",
                                 ctx->slice_param_num, obj_buffer->num_elements);
        free(ctx->slice_param_cache);
        ctx->slice_param_cache = NULL;
        ctx->slice_param_num = 0;
    }

    if (NULL == ctx->slice_param_cache) {
        ctx->slice_param_num = obj_buffer->num_elements;
        psb__information_message("Allocate %d VAEncSliceParameterBuffer cache buffers\n", 2 * ctx->slice_param_num);
        ctx->slice_param_cache = calloc(2 * ctx->slice_param_num, sizeof(VAEncSliceParameterBuffer));
        if (NULL == ctx->slice_param_cache) {
            psb__error_message("Run out of memory!\n");
            free(obj_buffer->buffer_data);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }
    }

    if (getenv("PSB_VIDEO_SIG_CORE") == NULL) {
        if ((ctx->ParallelCores == 2) && (obj_buffer->num_elements == 1)) {
            /*Need to replace unneccesary MTX_CMDID_STARTPICs with MTX_CMDID_PAD*/
            for (i = 0; i < (ctx->ParallelCores - 1); i++) {
                *(cmdbuf->cmd_idx_saved[PNW_CMDBUF_START_PIC_IDX] + i * 4) &= (~MTX_CMDWORD_ID_MASK);
                *(cmdbuf->cmd_idx_saved[PNW_CMDBUF_START_PIC_IDX] + i * 4) |= MTX_CMDID_PAD;
            }
            psb__information_message(" Remove unneccesary %d MTX_CMDID_STARTPIC commands from cmdbuf\n",
                                     ctx->ParallelCores - obj_buffer->num_elements);
            ctx->ParallelCores = obj_buffer->num_elements;
            *(cmdbuf->cmd_idx_saved[PNW_CMDBUF_SEQ_HEADER_IDX]) &=
                ~(MTX_CMDWORD_CORE_MASK << MTX_CMDWORD_CORE_SHIFT);
            *(cmdbuf->cmd_idx_saved[PNW_CMDBUF_PIC_HEADER_IDX]) &=
                ~(MTX_CMDWORD_CORE_MASK << MTX_CMDWORD_CORE_SHIFT);
            ctx->SliceToCore = ctx->ParallelCores - 1;
        }
    }

    slice_per_core = obj_buffer->num_elements / ctx->ParallelCores;
    pBuf_per_core = pBuffer;
    for (i = 0; i < slice_per_core; i++) {
        pBuffer = pBuf_per_core;
        for (j = 0; j < ctx->ParallelCores; j++) {
            pnw__H264ES_encode_one_slice(ctx, pBuffer);
            if (0 == ctx->SliceToCore) {
                ctx->SliceToCore = ctx->ParallelCores;
            }
            ctx->SliceToCore--;

            ASSERT(ctx->obj_context->slice_count < MAX_SLICES_PER_PICTURE);
            /*Move to the next buffer which will be sent to core j*/
            pBuffer += slice_per_core;
        }
        pBuf_per_core++; /* Move to the next buffer */
    }

    /*Cope with last slice when slice number is odd and parallelCores is even*/
    if (obj_buffer->num_elements > (slice_per_core * ctx->ParallelCores)) {
        ctx->SliceToCore = 0;
        pBuffer -= slice_per_core;
        pBuffer ++;
        pnw__H264ES_encode_one_slice(ctx, pBuffer);
    }

    free(obj_buffer->buffer_data);
    obj_buffer->buffer_data = NULL;

    return VA_STATUS_SUCCESS;
}

static VAStatus pnw__H264ES_process_misc_param(context_ENC_p ctx, object_buffer_p obj_buffer)
{
    /* Prepare InParams for macros of current slice, insert slice header, insert do slice command */
    VAEncMiscParameterBuffer *pBuffer;
    VAEncMiscParameterRateControl *rate_control_param;
    VAEncMiscParameterAIR *air_param;
    VAEncMiscParameterMaxSliceSize *max_slice_size_param;
    VAEncMiscParameterFrameRate *frame_rate_param;

    VAStatus vaStatus = VA_STATUS_SUCCESS;
    if (ctx->eCodec != IMG_CODEC_H264_VCM) {
        psb__information_message("Only VCM mode allow rate control setting.Ignore.\n");
        return VA_STATUS_SUCCESS;
    }
    ASSERT(obj_buffer->type == VAEncMiscParameterBufferType);

    /* Transfer ownership of VAEncMiscParameterBuffer data */
    pBuffer = (VAEncMiscParameterBuffer *) obj_buffer->buffer_data;
    obj_buffer->size = 0;

    switch (pBuffer->type) {
    case VAEncMiscParameterTypeFrameRate:
        frame_rate_param = (VAEncMiscParameterFrameRate *)pBuffer->data;

        if (frame_rate_param->framerate < 1 || frame_rate_param->framerate > 65535) {
            vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
            break;
        }

        if (ctx->sRCParams.FrameRate == frame_rate_param->framerate)
            break;

        psb__information_message("frame rate changed from %d to %d\n",
                                 ctx->sRCParams.FrameRate,
                                 frame_rate_param->framerate);
        ctx->sRCParams.FrameRate = frame_rate_param->framerate;
        ctx->sRCParams.bBitrateChanged = IMG_TRUE;
        break;

    case VAEncMiscParameterTypeRateControl:
        rate_control_param = (VAEncMiscParameterRateControl *)pBuffer->data;

        if (rate_control_param->initial_qp > 51 ||
            rate_control_param->min_qp > 51) {
            psb__error_message("Initial_qp(%d) and min_qpinitial_qp(%d) "
                               "are invalid.\nQP shouldn't be larger than 51 for H264\n",
                               rate_control_param->initial_qp, rate_control_param->min_qp);
            vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
            break;
        }

        if (rate_control_param->window_size > 65535) {
            psb__error_message("window_size is too much!\n");
            vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
            break;
        }

        psb__information_message("rate control changed from %d to %d\n",
                                 ctx->sRCParams.BitsPerSecond,
                                 rate_control_param->bits_per_second);

        if ((rate_control_param->bits_per_second == ctx->sRCParams.BitsPerSecond) &&
            (rate_control_param->window_size != 0) &&
            (ctx->sRCParams.BufferSize == ctx->sRCParams.BitsPerSecond / 1000 * rate_control_param->window_size) &&
            (ctx->sRCParams.MinQP == rate_control_param->min_qp) &&
            (ctx->sRCParams.InitialQp == rate_control_param->initial_qp))
            break;
        else
            ctx->sRCParams.bBitrateChanged = IMG_TRUE;

        if (rate_control_param->bits_per_second > TOPAZ_H264_MAX_BITRATE) {
            ctx->sRCParams.BitsPerSecond = TOPAZ_H264_MAX_BITRATE;
            psb__information_message(" bits_per_second(%d) exceeds \
			the maximum bitrate, set it with %d\n",
                                     rate_control_param->bits_per_second,
                                     TOPAZ_H264_MAX_BITRATE);
        } else if (rate_control_param->bits_per_second != 0)
            ctx->sRCParams.BitsPerSecond = rate_control_param->bits_per_second;

        if (rate_control_param->window_size != 0)
            ctx->sRCParams.BufferSize = ctx->sRCParams.BitsPerSecond * rate_control_param->window_size / 1000;
        if (rate_control_param->initial_qp != 0)
            ctx->sRCParams.InitialQp = rate_control_param->initial_qp;
        if (rate_control_param->min_qp != 0)
            ctx->sRCParams.MinQP = rate_control_param->min_qp;
        break;

    case VAEncMiscParameterTypeMaxSliceSize:
        max_slice_size_param = (VAEncMiscParameterMaxSliceSize *)pBuffer->data;

        /*The max slice size should not be bigger than 1920x1080x1.5x8 */
        if (max_slice_size_param->max_slice_size > 24883200) {
            psb__error_message("Invalid max_slice_size. It should be 1~ 3110400.\n");
            vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
            break;
        }

        if (ctx->max_slice_size == max_slice_size_param->max_slice_size)
            break;

        psb__information_message("max slice size changed to %d\n",
                                 max_slice_size_param->max_slice_size);

        ctx->max_slice_size = max_slice_size_param->max_slice_size;

        break;

    case VAEncMiscParameterTypeAIR:
        air_param = (VAEncMiscParameterAIR *)pBuffer->data;

        if (air_param->air_num_mbs > 65535 ||
            air_param->air_threshold > 65535) {
            vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
            break;
        }

        psb__information_message("air slice size changed to num_air_mbs %d "
                                 "air_threshold %d, air_auto %d\n",
                                 air_param->air_num_mbs, air_param->air_threshold,
                                 air_param->air_auto);

        if (((ctx->Height * ctx->Width) >> 8) < (int)air_param->air_num_mbs)
            air_param->air_num_mbs = ((ctx->Height * ctx->Width) >> 8);
        if (air_param->air_threshold == 0)
            psb__information_message("%s: air threshold is set to zero\n",
                                     __func__);
        ctx->num_air_mbs = air_param->air_num_mbs;
        ctx->air_threshold = air_param->air_threshold;
        //ctx->autotune_air_flag = air_param->air_auto;

        break;

    default:
        vaStatus = VA_STATUS_ERROR_UNKNOWN;
        DEBUG_FAILURE;
        break;
    }

    free(obj_buffer->buffer_data);
    obj_buffer->buffer_data = NULL;

    return vaStatus;
}



static VAStatus pnw_H264ES_RenderPicture(
    object_context_p obj_context,
    object_buffer_p *buffers,
    int num_buffers)
{
    INIT_CONTEXT_H264ES;
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int i;

    psb__information_message("pnw_H264ES_RenderPicture\n");

    for (i = 0; i < num_buffers; i++) {
        object_buffer_p obj_buffer = buffers[i];

        switch (obj_buffer->type) {
        case VAEncSequenceParameterBufferType:
            psb__information_message("pnw_H264_RenderPicture got VAEncSequenceParameterBufferType\n");
            vaStatus = pnw__H264ES_process_sequence_param(ctx, obj_buffer);
            DEBUG_FAILURE;
            break;

        case VAEncPictureParameterBufferType:
            psb__information_message("pnw_H264_RenderPicture got VAEncPictureParameterBuffer\n");
            vaStatus = pnw__H264ES_process_picture_param(ctx, obj_buffer);
            DEBUG_FAILURE;
            break;

        case VAEncSliceParameterBufferType:
            psb__information_message("pnw_H264_RenderPicture got VAEncSliceParameterBufferType\n");
            vaStatus = pnw__H264ES_process_slice_param(ctx, obj_buffer);
            DEBUG_FAILURE;
            break;

        case VAEncMiscParameterBufferType:
            psb__information_message("pnw_H264_RenderPicture got VAEncMiscParameterBufferType\n");
            vaStatus = pnw__H264ES_process_misc_param(ctx, obj_buffer);
            DEBUG_FAILURE;
            break;


        default:
            vaStatus = VA_STATUS_ERROR_UNKNOWN;
            DEBUG_FAILURE;
        }
        if (vaStatus != VA_STATUS_SUCCESS) {
            break;
        }
    }

    return vaStatus;
}

static VAStatus pnw_H264ES_EndPicture(
    object_context_p obj_context)
{
    INIT_CONTEXT_H264ES;
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    psb__information_message("pnw_H264ES_EndPicture\n");

    vaStatus = pnw_EndPicture(ctx);

    return vaStatus;
}


struct format_vtable_s pnw_H264ES_vtable = {
queryConfigAttributes:
    pnw_H264ES_QueryConfigAttributes,
validateConfig:
    pnw_H264ES_ValidateConfig,
createContext:
    pnw_H264ES_CreateContext,
destroyContext:
    pnw_H264ES_DestroyContext,
beginPicture:
    pnw_H264ES_BeginPicture,
renderPicture:
    pnw_H264ES_RenderPicture,
endPicture:
    pnw_H264ES_EndPicture
};

VAStatus pnw_set_frame_skip_flag(
    object_context_p obj_context)
{
    INIT_CONTEXT_H264ES;
    VAStatus vaStatus = VA_STATUS_SUCCESS;


    if (ctx && ctx->src_surface) {
        SET_SURFACE_INFO_skipped_flag(ctx->src_surface->psb_surface, 1);
        psb__information_message("Detected a skipped frame for surface 0x%08x.\n", ctx->src_surface->psb_surface);
    }

    return vaStatus;
}


