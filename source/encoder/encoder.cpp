/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Steve Borho <steve@borho.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#include "common.h"
#include "primitives.h"
#include "threadpool.h"
#include "param.h"
#include "frame.h"

#include "TLibCommon/TComPicYuv.h"
#include "TLibCommon/TComRom.h"

#include "bitcost.h"
#include "cturow.h"
#include "encoder.h"
#include "slicetype.h"
#include "frameencoder.h"
#include "ratecontrol.h"
#include "dpb.h"
#include "nal.h"

#include "x265.h"

static const char *summaryCSVHeader =
    "Command, Date/Time, Elapsed Time, FPS, Bitrate, "
    "Y PSNR, U PSNR, V PSNR, Global PSNR, SSIM, SSIM (dB), "
    "I count, I ave-QP, I kpbs, I-PSNR Y, I-PSNR U, I-PSNR V, I-SSIM (dB), "
    "P count, P ave-QP, P kpbs, P-PSNR Y, P-PSNR U, P-PSNR V, P-SSIM (dB), "
    "B count, B ave-QP, B kpbs, B-PSNR Y, B-PSNR U, B-PSNR V, B-SSIM (dB), "
    "Version\n";

using namespace x265;

Encoder::Encoder()
{
    m_aborted = false;
    m_encodedFrameNum = 0;
    m_pocLast = -1;
    m_maxRefPicNum = 0;
    m_curEncoder = 0;
    m_numLumaWPFrames = 0;
    m_numChromaWPFrames = 0;
    m_numLumaWPBiFrames = 0;
    m_numChromaWPBiFrames = 0;
    m_lookahead = NULL;
    m_frameEncoder = NULL;
    m_rateControl = NULL;
    m_dpb = NULL;
    m_exportedPic = NULL;
    m_numDelayedPic = 0;
    m_outputCount = 0;
    m_csvfpt = NULL;
    m_param = NULL;
}

Encoder::~Encoder()
{
}

void Encoder::create()
{
    if (!primitives.sad[0])
    {
        // this should be an impossible condition when using our public API, and indicates a serious bug.
        x265_log(m_param, X265_LOG_ERROR, "Primitives must be initialized before encoder is created\n");
        abort();
    }

    m_frameEncoder = new FrameEncoder[m_param->frameNumThreads];
    if (m_frameEncoder)
        for (int i = 0; i < m_param->frameNumThreads; i++)
            m_frameEncoder[i].setThreadPool(m_threadPool);

    /* Allocate thread local data shared by all frame encoders */
    ThreadPool *pool = ThreadPool::getThreadPool();
    const int poolThreadCount = pool ? pool->getThreadCount() : 1;
    m_threadLocalData = new ThreadLocalData[poolThreadCount];
    if (m_threadLocalData)
    {
        for (int i = 0; i < poolThreadCount; i++)
            m_threadLocalData[i].init(*this);
    }
    else
        m_aborted = true;

    m_lookahead = new Lookahead(m_param, m_threadPool);
    m_dpb = new DPB(m_param);
    m_rateControl = new RateControl(m_param);

    /* Try to open CSV file handle */
    if (m_param->csvfn)
    {
        m_csvfpt = fopen(m_param->csvfn, "r");
        if (m_csvfpt)
        {
            // file already exists, re-open for append
            fclose(m_csvfpt);
            m_csvfpt = fopen(m_param->csvfn, "ab");
        }
        else
        {
            // new CSV file, write header
            m_csvfpt = fopen(m_param->csvfn, "wb");
            if (m_csvfpt)
            {
                if (m_param->logLevel >= X265_LOG_DEBUG)
                {
                    fprintf(m_csvfpt, "Encode Order, Type, POC, QP, Bits, ");
                    if (m_param->rc.rateControlMode == X265_RC_CRF)
                        fprintf(m_csvfpt, "RateFactor, ");
                    fprintf(m_csvfpt, "Y PSNR, U PSNR, V PSNR, YUV PSNR, SSIM, SSIM (dB), "
                                      "Encoding time, Elapsed time, List 0, List 1\n");
                }
                else
                    fputs(summaryCSVHeader, m_csvfpt);
            }
        }
    }

    m_aborted |= parseLambdaFile(m_param);
}

void Encoder::destroy()
{
    if (m_exportedPic)
    {
        ATOMIC_DEC(&m_exportedPic->m_countRefEncoders);
        m_exportedPic = NULL;
    }

    if (m_frameEncoder)
    {
        for (int i = 0; i < m_totalFrameThreads; i++)
        {
            // Ensure frame encoder is idle before destroying it
            m_frameEncoder[i].getEncodedPicture(m_nalList);
            m_frameEncoder[i].destroy();
        }

        delete [] m_frameEncoder;
    }

    if (m_threadLocalData)
        delete [] m_threadLocalData;

    if (m_lookahead)
    {
        m_lookahead->destroy();
        delete m_lookahead;
    }

    delete m_dpb;
    if (m_rateControl)
    {
        m_rateControl->destroy();
        delete m_rateControl;
    }
    // thread pool release should always happen last
    if (m_threadPool)
        m_threadPool->release();

    free(m_param->rc.statFileName); // alloc'd by strdup
    X265_FREE(m_param);
    if (m_csvfpt)
        fclose(m_csvfpt);
}

void Encoder::init()
{
    if (m_frameEncoder)
    {
        int numRows = (m_param->sourceHeight + g_maxCUSize - 1) / g_maxCUSize;
        int numCols = (m_param->sourceWidth  + g_maxCUSize - 1) / g_maxCUSize;
        for (int i = 0; i < m_param->frameNumThreads; i++)
        {
            if (!m_frameEncoder[i].init(this, numRows, numCols))
            {
                x265_log(m_param, X265_LOG_ERROR, "Unable to initialize frame encoder, aborting\n");
                m_aborted = true;
            }
        }
    }
    if (!m_rateControl->init(&m_frameEncoder[0].m_sps))
        m_aborted = true;
    m_lookahead->init();
    m_encodeStartTime = x265_mdate();
}

void Encoder::getStreamHeaders()
{
    Bitstream bs;
    m_frameEncoder->getStreamHeaders(m_nalList, bs);
}

void Encoder::updateVbvPlan(RateControl* rc)
{
    int encIdx, curIdx;

    curIdx = (m_curEncoder + m_param->frameNumThreads - 1) % m_param->frameNumThreads;
    encIdx = (curIdx + 1) % m_param->frameNumThreads;
    while (encIdx != curIdx)
    {
        FrameEncoder *encoder = &m_frameEncoder[encIdx];
        if (encoder->m_rce.isActive)
        {
            rc->m_bufferFill -= encoder->m_rce.frameSizeEstimated;
            rc->m_bufferFill = X265_MAX(rc->m_bufferFill, 0);
            rc->m_bufferFill += encoder->m_rce.bufferRate;
            rc->m_bufferFill = X265_MIN(rc->m_bufferFill, rc->m_bufferSize);
        }
        encIdx = (encIdx + 1) % m_param->frameNumThreads;
    }
}

/** set default quantization matrix to array
*/
void Encoder::setDefaultScalingList()
{
    for (uint32_t sizeId = 0; sizeId < SCALING_LIST_SIZE_NUM; sizeId++)
    {
        for (uint32_t listId = 0; listId < g_scalingListNum[sizeId]; listId++)
        {
            getScalingList()->processDefaultMarix(sizeId, listId);
        }
    }
}

/**
 \param   pic_in              input original YUV picture or NULL
 \param   pic_out             pointer to reconstructed picture struct
 \retval                      1 - success, negative on error. m_nalList contains access unit
 */
int Encoder::encode(const x265_picture* pic_in, x265_picture *pic_out)
{
    if (m_aborted)
        return -1;

    if (m_exportedPic)
    {
        ATOMIC_DEC(&m_exportedPic->m_countRefEncoders);
        m_exportedPic = NULL;
        m_dpb->recycleUnreferenced();
    }

    if (pic_in)
    {
        if (pic_in->colorSpace != m_param->internalCsp)
        {
            x265_log(m_param, X265_LOG_ERROR, "Unsupported color space (%d) on input\n",
                     pic_in->colorSpace);
            return -1;
        }
        if (pic_in->bitDepth < 8 || pic_in->bitDepth > 16)
        {
            x265_log(m_param, X265_LOG_ERROR, "Input bit depth (%d) must be between 8 and 16\n",
                     pic_in->bitDepth);
            return -1;
        }

        Frame *pic;
        if (m_dpb->m_freeList.empty())
        {
            pic = new Frame;
            if (!pic || !pic->create(m_param, m_defaultDisplayWindow, m_conformanceWindow))
            {
                m_aborted = true;
                x265_log(m_param, X265_LOG_ERROR, "memory allocation failure, aborting encode\n");
                if (pic)
                {
                    pic->destroy();
                    delete pic;
                }
                return -1;
            }
        }
        else
            pic = m_dpb->m_freeList.popBack();

        /* Copy input picture into a TComPic, send to lookahead */
        pic->m_POC = ++m_pocLast;
        pic->reinit(m_param);
        pic->getPicYuvOrg()->copyFromPicture(*pic_in, m_pad);
        pic->m_userData = pic_in->userData;
        pic->m_pts = pic_in->pts;
        pic->m_forceqp = pic_in->forceqp;

        if (m_pocLast == 0)
            m_firstPts = pic->m_pts;
        if (m_bframeDelay && m_pocLast == m_bframeDelay)
            m_bframeDelayTime = pic->m_pts - m_firstPts;

        // Encoder holds a reference count until collecting stats
        ATOMIC_INC(&pic->m_countRefEncoders);
        bool bEnableWP = m_param->bEnableWeightedPred || m_param->bEnableWeightedBiPred;
        if (m_param->rc.aqMode || bEnableWP)
            m_rateControl->calcAdaptiveQuantFrame(pic);
        m_lookahead->addPicture(pic, pic_in->sliceType);
        m_numDelayedPic++;
    }
    else
        m_lookahead->flush();

    if (m_param->rc.rateControlMode == X265_RC_ABR)
    {
        // delay frame parallelism for non-VBV ABR
        if (m_pocLast == 0 && !m_param->rc.vbvBufferSize && !m_param->rc.vbvMaxBitrate)
            m_param->frameNumThreads = 1;
        else if (m_param->frameNumThreads != m_totalFrameThreads)
        {
            // re-enable frame parallelism after the first few P frames are encoded
            uint32_t frameCnt = (uint32_t)((0.5 * m_param->fpsNum / m_param->fpsDenom) / (m_param->bframes + 1));
            if (m_analyzeP.m_numPics > frameCnt)
                m_param->frameNumThreads = m_totalFrameThreads;
        }
    }

    FrameEncoder *curEncoder = &m_frameEncoder[m_curEncoder];
    m_curEncoder = (m_curEncoder + 1) % m_param->frameNumThreads;
    int ret = 0;

    // getEncodedPicture() should block until the FrameEncoder has completed
    // encoding the frame.  This is how back-pressure through the API is
    // accomplished when the encoder is full.
    Frame *out = curEncoder->getEncodedPicture(m_nalList);

    if (out)
    {
        if (pic_out)
        {
            TComPicYuv *recpic = out->getPicYuvRec();
            pic_out->poc = out->getSlice()->getPOC();
            pic_out->bitDepth = X265_DEPTH;
            pic_out->userData = out->m_userData;
            pic_out->colorSpace = m_param->internalCsp;

            pic_out->pts = out->m_pts;
            pic_out->dts = out->m_dts;

            switch (out->getSlice()->getSliceType())
            {
            case I_SLICE:
                pic_out->sliceType = out->m_lowres.bKeyframe ? X265_TYPE_IDR : X265_TYPE_I;
                break;
            case P_SLICE:
                pic_out->sliceType = X265_TYPE_P;
                break;
            case B_SLICE:
                pic_out->sliceType = X265_TYPE_B;
                break;
            }

            pic_out->planes[0] = recpic->getLumaAddr();
            pic_out->stride[0] = recpic->getStride() * sizeof(pixel);
            pic_out->planes[1] = recpic->getCbAddr();
            pic_out->stride[1] = recpic->getCStride() * sizeof(pixel);
            pic_out->planes[2] = recpic->getCrAddr();
            pic_out->stride[2] = recpic->getCStride() * sizeof(pixel);
        }

        if (out->getSlice()->getSliceType() == P_SLICE)
        {
            if (out->getSlice()->m_weightPredTable[0][0][0].bPresentFlag)
                m_numLumaWPFrames++;
            if (out->getSlice()->m_weightPredTable[0][0][1].bPresentFlag ||
                out->getSlice()->m_weightPredTable[0][0][2].bPresentFlag)
                m_numChromaWPFrames++;
        }
        else if (out->getSlice()->getSliceType() == B_SLICE)
        {
            bool bLuma = false, bChroma = false;
            for (int l = 0; l < 2; l++)
            {
                if (out->getSlice()->m_weightPredTable[l][0][0].bPresentFlag)
                    bLuma = true;
                if (out->getSlice()->m_weightPredTable[l][0][1].bPresentFlag ||
                    out->getSlice()->m_weightPredTable[l][0][2].bPresentFlag)
                    bChroma = true;
            }

            if (bLuma)
                m_numLumaWPBiFrames++;
            if (bChroma)
                m_numChromaWPBiFrames++;
        }

        uint64_t bytes = 0;
        for (uint32_t i = 0; i < m_nalList.m_numNal; i++)
        {
            int type = m_nalList.m_nal[i].type;

            // exclude SEI
            if (type != NAL_UNIT_PREFIX_SEI && type != NAL_UNIT_SUFFIX_SEI)
            {
                bytes += m_nalList.m_nal[i].sizeBytes;
                // and exclude start code prefix
                bytes -= (!i || type == NAL_UNIT_SPS || type == NAL_UNIT_PPS) ? 4 : 3;
            }
        }
        if (m_rateControl->rateControlEnd(out, bytes << 3, &curEncoder->m_rce, &curEncoder->m_frameStats) < 0)
        {
            m_aborted = true;
            return -1;
        }
        finishFrameStats(out, curEncoder, bytes << 3);
        // Allow this frame to be recycled if no frame encoders are using it for reference
        if (!pic_out)
        {
            ATOMIC_DEC(&out->m_countRefEncoders);
            m_dpb->recycleUnreferenced();
        }
        else
            m_exportedPic = out;

        m_numDelayedPic--;

        ret = 1;
    }

    // pop a single frame from decided list, then provide to frame encoder
    // curEncoder is guaranteed to be idle at this point
    Frame* fenc = m_lookahead->getDecidedPicture();
    if (fenc)
    {
        // give this picture a TComPicSym instance before encoding
        if (m_dpb->m_picSymFreeList)
        {
            fenc->m_picSym = m_dpb->m_picSymFreeList;
            m_dpb->m_picSymFreeList = m_dpb->m_picSymFreeList->m_freeListNext;
            fenc->m_reconPicYuv = fenc->m_picSym->m_reconPicYuv;
        }
        else
        {
            fenc->allocPicSym(m_param);
            // NOTE: the SAO pointer from m_frameEncoder for read m_maxSplitLevel, etc, we can remove it later
            if (m_param->bEnableSAO)
                fenc->getPicSym()->allocSaoParam(m_frameEncoder->getSAO());
        }
        fenc->getSlice()->setPOC(fenc->m_POC);
        curEncoder->m_rce.encodeOrder = m_encodedFrameNum++;
        if (m_bframeDelay)
        {
            int64_t *prevReorderedPts = m_prevReorderedPts;
            fenc->m_dts = m_encodedFrameNum > m_bframeDelay
                ? prevReorderedPts[(m_encodedFrameNum - m_bframeDelay) % m_bframeDelay]
                : fenc->m_reorderedPts - m_bframeDelayTime;
            prevReorderedPts[m_encodedFrameNum % m_bframeDelay] = fenc->m_reorderedPts;
        }
        else
            fenc->m_dts = fenc->m_reorderedPts;

        // Initialize slice for encoding with this FrameEncoder
        curEncoder->initSlice(fenc);

        // determine references, setup RPS, etc
        m_dpb->prepareEncode(fenc);

        // set slice QP
        m_rateControl->rateControlStart(fenc, m_lookahead, &curEncoder->m_rce, this);

        // Allow FrameEncoder::compressFrame() to start in a worker thread
        curEncoder->m_enable.trigger();
    }

    return ret;
}

void EncStats::addPsnr(double psnrY, double psnrU, double psnrV)
{
    m_psnrSumY += psnrY;
    m_psnrSumU += psnrU;
    m_psnrSumV += psnrV;
}

void EncStats::addBits(uint64_t bits)
{
    m_accBits += bits;
    m_numPics++;
}

void EncStats::addSsim(double ssim)
{
    m_globalSsim += ssim;
}

void EncStats::addQP(double aveQp)
{
    m_totalQp += aveQp;
}

char* Encoder::statsCSVString(EncStats& stat, char* buffer)
{
    if (!stat.m_numPics)
    {
        sprintf(buffer, "-, -, -, -, -, -, -, ");
        return buffer;
    }

    double fps = (double)m_param->fpsNum / m_param->fpsDenom;
    double scale = fps / 1000 / (double)stat.m_numPics;

    int len = sprintf(buffer, "%-6d, ", stat.m_numPics);

    len += sprintf(buffer + len, "%2.2lf, ", stat.m_totalQp / (double)stat.m_numPics);
    len += sprintf(buffer + len, "%-8.2lf, ", stat.m_accBits * scale);
    if (m_param->bEnablePsnr)
    {
        len += sprintf(buffer + len, "%.3lf, %.3lf, %.3lf, ",
                       stat.m_psnrSumY / (double)stat.m_numPics,
                       stat.m_psnrSumU / (double)stat.m_numPics,
                       stat.m_psnrSumV / (double)stat.m_numPics);
    }
    else
        len += sprintf(buffer + len, "-, -, -, ");

    if (m_param->bEnableSsim)
        sprintf(buffer + len, "%.3lf, ", x265_ssim2dB(stat.m_globalSsim / (double)stat.m_numPics));
    else
        sprintf(buffer + len, "-, ");
    return buffer;
}

char* Encoder::statsString(EncStats& stat, char* buffer)
{
    double fps = (double)m_param->fpsNum / m_param->fpsDenom;
    double scale = fps / 1000 / (double)stat.m_numPics;

    int len = sprintf(buffer, "%6d, ", stat.m_numPics);

    len += sprintf(buffer + len, "Avg QP:%2.2lf", stat.m_totalQp / (double)stat.m_numPics);
    len += sprintf(buffer + len, "  kb/s: %-8.2lf", stat.m_accBits * scale);
    if (m_param->bEnablePsnr)
    {
        len += sprintf(buffer + len, "  PSNR Mean: Y:%.3lf U:%.3lf V:%.3lf",
                       stat.m_psnrSumY / (double)stat.m_numPics,
                       stat.m_psnrSumU / (double)stat.m_numPics,
                       stat.m_psnrSumV / (double)stat.m_numPics);
    }
    if (m_param->bEnableSsim)
    {
        sprintf(buffer + len, "  SSIM Mean: %.6lf (%.3lfdB)",
                stat.m_globalSsim / (double)stat.m_numPics,
                x265_ssim2dB(stat.m_globalSsim / (double)stat.m_numPics));
    }
    return buffer;
}

void Encoder::printSummary()
{
    if (m_param->logLevel < X265_LOG_INFO)
        return;

    char buffer[200];
    if (m_analyzeI.m_numPics)
        x265_log(m_param, X265_LOG_INFO, "frame I: %s\n", statsString(m_analyzeI, buffer));
    if (m_analyzeP.m_numPics)
        x265_log(m_param, X265_LOG_INFO, "frame P: %s\n", statsString(m_analyzeP, buffer));
    if (m_analyzeB.m_numPics)
        x265_log(m_param, X265_LOG_INFO, "frame B: %s\n", statsString(m_analyzeB, buffer));
    if (m_analyzeAll.m_numPics)
        x265_log(m_param, X265_LOG_INFO, "global : %s\n", statsString(m_analyzeAll, buffer));
    if (m_param->bEnableWeightedPred && m_analyzeP.m_numPics)
    {
        x265_log(m_param, X265_LOG_INFO, "Weighted P-Frames: Y:%.1f%% UV:%.1f%%\n",
            (float)100.0 * m_numLumaWPFrames / m_analyzeP.m_numPics,
            (float)100.0 * m_numChromaWPFrames / m_analyzeP.m_numPics);
    }
    if (m_param->bEnableWeightedBiPred && m_analyzeB.m_numPics)
    {
        x265_log(m_param, X265_LOG_INFO, "Weighted B-Frames: Y:%.1f%% UV:%.1f%%\n",
            (float)100.0 * m_numLumaWPBiFrames / m_analyzeB.m_numPics,
            (float)100.0 * m_numChromaWPBiFrames / m_analyzeB.m_numPics);
    }
    int pWithB = 0;
    for (int i = 0; i <= m_param->bframes; i++)
    {
        pWithB += m_lookahead->m_histogram[i];
    }

    if (pWithB)
    {
        int p = 0;
        for (int i = 0; i <= m_param->bframes; i++)
        {
            p += sprintf(buffer + p, "%.1f%% ", 100. * m_lookahead->m_histogram[i] / pWithB);
        }

        x265_log(m_param, X265_LOG_INFO, "consecutive B-frames: %s\n", buffer);
    }
    if (m_param->bLossless)
    {
        float frameSize = (float)(m_param->sourceWidth - m_pad[0]) * (m_param->sourceHeight - m_pad[1]);
        float uncompressed = frameSize * X265_DEPTH * m_analyzeAll.m_numPics;

        x265_log(m_param, X265_LOG_INFO, "lossless compression ratio %.2f::1\n", uncompressed / m_analyzeAll.m_accBits);
    }

    if (!m_param->bLogCuStats)
        return;

    const int poolThreadCount = m_threadPool ? m_threadPool->getThreadCount() : 1;

    for (int sliceType = 2; sliceType >= 0; sliceType--)
    {
        if (sliceType == P_SLICE && !m_analyzeP.m_numPics)
            continue;
        if (sliceType == B_SLICE && !m_analyzeB.m_numPics)
            continue;

        StatisticLog finalLog;
        for (int depth = 0; depth < (int)g_maxCUDepth; depth++)
        {
            for (int i = 0; i < poolThreadCount; i++)
            {
                StatisticLog& enclog = m_threadLocalData[i].m_cuCoder.m_sliceTypeLog[sliceType];
                if (depth == 0)
                    finalLog.totalCu += enclog.totalCu;
                finalLog.cntIntra[depth] += enclog.cntIntra[depth];
                for (int m = 0; m < INTER_MODES; m++)
                {
                    if (m < INTRA_MODES)
                        finalLog.cuIntraDistribution[depth][m] += enclog.cuIntraDistribution[depth][m];
                    finalLog.cuInterDistribution[depth][m] += enclog.cuInterDistribution[depth][m];
                }

                if (depth == (int)g_maxCUDepth - 1)
                    finalLog.cntIntraNxN += enclog.cntIntraNxN;
                if (sliceType != I_SLICE)
                {
                    finalLog.cntTotalCu[depth] += enclog.cntTotalCu[depth];
                    finalLog.cntInter[depth] += enclog.cntInter[depth];
                    finalLog.cntSkipCu[depth] += enclog.cntSkipCu[depth];
                }
            }

            uint64_t cntInter, cntSkipCu, cntIntra = 0, cntIntraNxN = 0, encCu = 0;
            uint64_t cuInterDistribution[INTER_MODES], cuIntraDistribution[INTRA_MODES];

            // check for 0/0, if true assign 0 else calculate percentage
            for (int n = 0; n < INTER_MODES; n++)
            {
                if (finalLog.cntInter[depth] == 0)
                    cuInterDistribution[n] = 0;
                else
                    cuInterDistribution[n] = (finalLog.cuInterDistribution[depth][n] * 100) / finalLog.cntInter[depth];
                if (n < INTRA_MODES)
                {
                    if (finalLog.cntIntra[depth] == 0)
                    {
                        cntIntraNxN = 0;
                        cuIntraDistribution[n] = 0;
                    }
                    else
                    {
                        cntIntraNxN = (finalLog.cntIntraNxN * 100) / finalLog.cntIntra[depth];
                        cuIntraDistribution[n] = (finalLog.cuIntraDistribution[depth][n] * 100) / finalLog.cntIntra[depth];
                    }
                }
            }

            if (finalLog.totalCu == 0)
            {
                encCu = 0;
            }
            else
            {
                if (sliceType == I_SLICE)
                {
                    cntIntra = (finalLog.cntIntra[depth] * 100) / finalLog.totalCu;
                    cntIntraNxN = (finalLog.cntIntraNxN * 100) / finalLog.totalCu;
                }
                else
                {
                    encCu = ((finalLog.cntIntra[depth] + finalLog.cntInter[depth]) * 100) / finalLog.totalCu;
                }
            }
            if (sliceType == I_SLICE)
            {
                cntInter = 0;
                cntSkipCu = 0;
            }
            else
            {
                if (finalLog.cntTotalCu[depth] == 0)
                {
                    cntInter = 0;
                    cntIntra = 0;
                    cntSkipCu = 0;
                }
                else
                {
                    cntInter = (finalLog.cntInter[depth] * 100) / finalLog.cntTotalCu[depth];
                    cntIntra = (finalLog.cntIntra[depth] * 100) / finalLog.cntTotalCu[depth];
                    cntSkipCu = (finalLog.cntSkipCu[depth] * 100) / finalLog.cntTotalCu[depth];
                }
            }
            // print statistics
            int cuSize = g_maxCUSize >> depth;
            char stats[256] = { 0 };
            int len = 0;
            if (sliceType != I_SLICE)
            {
                len += sprintf(stats + len, " EncCU "X265_LL "%% Merge "X265_LL "%%", encCu, cntSkipCu);
            }
            if (cntInter)
            {
                len += sprintf(stats + len, " Inter "X265_LL "%%", cntInter);
                if (m_param->bEnableAMP)
                    len += sprintf(stats + len, "(%dx%d "X265_LL "%% %dx%d "X265_LL "%% %dx%d "X265_LL "%% AMP "X265_LL "%%)",
                                   cuSize, cuSize, cuInterDistribution[0],
                                   cuSize / 2, cuSize, cuInterDistribution[2],
                                   cuSize, cuSize / 2, cuInterDistribution[1],
                                   cuInterDistribution[3]);
                else if (m_param->bEnableRectInter)
                    len += sprintf(stats + len, "(%dx%d "X265_LL "%% %dx%d "X265_LL "%% %dx%d "X265_LL "%%)",
                                   cuSize, cuSize, cuInterDistribution[0],
                                   cuSize / 2, cuSize, cuInterDistribution[2],
                                   cuSize, cuSize / 2, cuInterDistribution[1]);
            }
            if (cntIntra)
            {
                len += sprintf(stats + len, " Intra "X265_LL "%%(DC "X265_LL "%% P "X265_LL "%% Ang "X265_LL "%%",
                               cntIntra, cuIntraDistribution[0],
                               cuIntraDistribution[1], cuIntraDistribution[2]);
                if (sliceType != I_SLICE)
                {
                    if (depth == (int)g_maxCUDepth - 1)
                        len += sprintf(stats + len, " %dx%d "X265_LL "%%", cuSize / 2, cuSize / 2, cntIntraNxN);
                }

                len += sprintf(stats + len, ")");
                if (sliceType == I_SLICE)
                {
                    if (depth == (int)g_maxCUDepth - 1)
                        len += sprintf(stats + len, " %dx%d: "X265_LL "%%", cuSize / 2, cuSize / 2, cntIntraNxN);
                }
            }
            const char slicechars[] = "BPI";
            if (stats[0])
                x265_log(m_param, X265_LOG_INFO, "%c%-2d:%s\n", slicechars[sliceType], cuSize, stats);
        }
    }
}

void Encoder::fetchStats(x265_stats *stats, size_t statsSizeBytes)
{
    if (statsSizeBytes >= sizeof(stats))
    {
        stats->globalPsnrY = m_analyzeAll.m_psnrSumY;
        stats->globalPsnrU = m_analyzeAll.m_psnrSumU;
        stats->globalPsnrV = m_analyzeAll.m_psnrSumV;
        stats->encodedPictureCount = m_analyzeAll.m_numPics;
        stats->totalWPFrames = m_numLumaWPFrames;
        stats->accBits = m_analyzeAll.m_accBits;
        stats->elapsedEncodeTime = (double)(x265_mdate() - m_encodeStartTime) / 1000000;
        if (stats->encodedPictureCount > 0)
        {
            stats->globalSsim = m_analyzeAll.m_globalSsim / stats->encodedPictureCount;
            stats->globalPsnr = (stats->globalPsnrY * 6 + stats->globalPsnrU + stats->globalPsnrV) / (8 * stats->encodedPictureCount);
            stats->elapsedVideoTime = (double)stats->encodedPictureCount * m_param->fpsDenom / m_param->fpsNum;
            stats->bitrate = (0.001f * stats->accBits) / stats->elapsedVideoTime;
        }
        else
        {
            stats->globalSsim = 0;
            stats->globalPsnr = 0;
            stats->bitrate = 0;
            stats->elapsedVideoTime = 0;
        }
    }

    /* If new statistics are added to x265_stats, we must check here whether the
     * structure provided by the user is the new structure or an older one (for
     * future safety) */
}

void Encoder::writeLog(int argc, char **argv)
{
    if (m_csvfpt)
    {
        if (m_param->logLevel >= X265_LOG_DEBUG)
        {
            // adding summary to a per-frame csv log file needs a summary header
            fprintf(m_csvfpt, "\nSummary\n");
            fputs(summaryCSVHeader, m_csvfpt);
        }
        // CLI arguments or other
        for (int i = 1; i < argc; i++)
        {
            if (i) fputc(' ', m_csvfpt);
            fputs(argv[i], m_csvfpt);
        }

        // current date and time
        time_t now;
        struct tm* timeinfo;
        time(&now);
        timeinfo = localtime(&now);
        char buffer[200];
        strftime(buffer, 128, "%c", timeinfo);
        fprintf(m_csvfpt, ", %s, ", buffer);

        x265_stats stats;
        fetchStats(&stats, sizeof(stats));

        // elapsed time, fps, bitrate
        fprintf(m_csvfpt, "%.2f, %.2f, %.2f,",
                stats.elapsedEncodeTime, stats.encodedPictureCount / stats.elapsedEncodeTime, stats.bitrate);

        if (m_param->bEnablePsnr)
            fprintf(m_csvfpt, " %.3lf, %.3lf, %.3lf, %.3lf,",
                    stats.globalPsnrY / stats.encodedPictureCount, stats.globalPsnrU / stats.encodedPictureCount,
                    stats.globalPsnrV / stats.encodedPictureCount, stats.globalPsnr);
        else
            fprintf(m_csvfpt, " -, -, -, -,");
        if (m_param->bEnableSsim)
            fprintf(m_csvfpt, " %.6f, %6.3f,", stats.globalSsim, x265_ssim2dB(stats.globalSsim));
        else
            fprintf(m_csvfpt, " -, -,");

        fputs(statsCSVString(m_analyzeI, buffer), m_csvfpt);
        fputs(statsCSVString(m_analyzeP, buffer), m_csvfpt);
        fputs(statsCSVString(m_analyzeB, buffer), m_csvfpt);
        fprintf(m_csvfpt, " %s\n", x265_version_str);
    }
}

/**
 * Produce an ascii(hex) representation of picture digest.
 *
 * Returns: a statically allocated null-terminated string.  DO NOT FREE.
 */
static const char*digestToString(const unsigned char digest[3][16], int numChar)
{
    const char* hex = "0123456789abcdef";
    static char string[99];
    int cnt = 0;

    for (int yuvIdx = 0; yuvIdx < 3; yuvIdx++)
    {
        for (int i = 0; i < numChar; i++)
        {
            string[cnt++] = hex[digest[yuvIdx][i] >> 4];
            string[cnt++] = hex[digest[yuvIdx][i] & 0xf];
        }

        string[cnt++] = ',';
    }

    string[cnt - 1] = '\0';
    return string;
}

void Encoder::finishFrameStats(Frame* pic, FrameEncoder *curEncoder, uint64_t bits)
{
    TComPicYuv* recon = pic->getPicYuvRec();

    //===== calculate PSNR =====
    int width  = recon->getWidth() - m_pad[0];
    int height = recon->getHeight() - m_pad[1];
    int size = width * height;

    int maxvalY = 255 << (X265_DEPTH - 8);
    int maxvalC = 255 << (X265_DEPTH - 8);
    double refValueY = (double)maxvalY * maxvalY * size;
    double refValueC = (double)maxvalC * maxvalC * size / 4.0;
    uint64_t ssdY, ssdU, ssdV;

    ssdY = curEncoder->m_SSDY;
    ssdU = curEncoder->m_SSDU;
    ssdV = curEncoder->m_SSDV;
    double psnrY = (ssdY ? 10.0 * log10(refValueY / (double)ssdY) : 99.99);
    double psnrU = (ssdU ? 10.0 * log10(refValueC / (double)ssdU) : 99.99);
    double psnrV = (ssdV ? 10.0 * log10(refValueC / (double)ssdV) : 99.99);

    TComSlice*  slice = pic->getSlice();

    //===== add bits, psnr and ssim =====
    m_analyzeAll.addBits(bits);
    m_analyzeAll.addQP(pic->m_avgQpAq);

    if (m_param->bEnablePsnr)
    {
        m_analyzeAll.addPsnr(psnrY, psnrU, psnrV);
    }

    double ssim = 0.0;
    if (m_param->bEnableSsim && curEncoder->m_ssimCnt)
    {
        ssim = curEncoder->m_ssim / curEncoder->m_ssimCnt;
        m_analyzeAll.addSsim(ssim);
    }
    if (slice->isIntra())
    {
        m_analyzeI.addBits(bits);
        m_analyzeI.addQP(pic->m_avgQpAq);
        if (m_param->bEnablePsnr)
            m_analyzeI.addPsnr(psnrY, psnrU, psnrV);
        if (m_param->bEnableSsim)
            m_analyzeI.addSsim(ssim);
    }
    else if (slice->isInterP())
    {
        m_analyzeP.addBits(bits);
        m_analyzeP.addQP(pic->m_avgQpAq);
        if (m_param->bEnablePsnr)
            m_analyzeP.addPsnr(psnrY, psnrU, psnrV);
        if (m_param->bEnableSsim)
            m_analyzeP.addSsim(ssim);
    }
    else if (slice->isInterB())
    {
        m_analyzeB.addBits(bits);
        m_analyzeB.addQP(pic->m_avgQpAq);
        if (m_param->bEnablePsnr)
            m_analyzeB.addPsnr(psnrY, psnrU, psnrV);
        if (m_param->bEnableSsim)
            m_analyzeB.addSsim(ssim);
    }

    // if debug log level is enabled, per frame logging is performed
    if (m_param->logLevel >= X265_LOG_DEBUG)
    {
        char c = (slice->isIntra() ? 'I' : slice->isInterP() ? 'P' : 'B');
        int poc = slice->getPOC();
        if (!slice->isReferenced())
            c += 32; // lower case if unreferenced

        char buf[1024];
        int p;
        p = sprintf(buf, "POC:%d %c QP %2.2lf(%d) %10d bits", poc, c, pic->m_avgQpAq, slice->getSliceQp(), (int)bits);
        if (m_param->rc.rateControlMode == X265_RC_CRF)
            p += sprintf(buf + p, " RF:%.3lf", pic->m_rateFactor);
        if (m_param->bEnablePsnr)
            p += sprintf(buf + p, " [Y:%6.2lf U:%6.2lf V:%6.2lf]", psnrY, psnrU, psnrV);
        if (m_param->bEnableSsim)
            p += sprintf(buf + p, " [SSIM: %.3lfdB]", x265_ssim2dB(ssim));

        if (!slice->isIntra())
        {
            int numLists = slice->isInterP() ? 1 : 2;
            for (int list = 0; list < numLists; list++)
            {
                p += sprintf(buf + p, " [L%d ", list);
                for (int ref = 0; ref < slice->getNumRefIdx(list); ref++)
                {
                    int k = slice->getRefPOC(list, ref) - slice->getLastIDR();
                    p += sprintf(buf + p, "%d ", k);
                }

                p += sprintf(buf + p, "]");
            }
        }

        // per frame CSV logging if the file handle is valid
        if (m_csvfpt)
        {
            fprintf(m_csvfpt, "%d, %c-SLICE, %4d, %2.2lf, %10d,", m_outputCount++, c, poc, pic->m_avgQpAq, (int)bits);
            if (m_param->rc.rateControlMode == X265_RC_CRF)
                fprintf(m_csvfpt, "%.3lf,", pic->m_rateFactor);
            double psnr = (psnrY * 6 + psnrU + psnrV) / 8;
            if (m_param->bEnablePsnr)
                fprintf(m_csvfpt, "%.3lf, %.3lf, %.3lf, %.3lf,", psnrY, psnrU, psnrV, psnr);
            else
                fprintf(m_csvfpt, " -, -, -, -,");
            if (m_param->bEnableSsim)
                fprintf(m_csvfpt, " %.6f, %6.3f,", ssim, x265_ssim2dB(ssim));
            else
                fprintf(m_csvfpt, " -, -,");
            fprintf(m_csvfpt, " %.3lf, %.3lf", curEncoder->m_frameTime, curEncoder->m_elapsedCompressTime);
            if (!slice->isIntra())
            {
                int numLists = slice->isInterP() ? 1 : 2;
                for (int list = 0; list < numLists; list++)
                {
                    fprintf(m_csvfpt, ", ");
                    for (int ref = 0; ref < slice->getNumRefIdx(list); ref++)
                    {
                        int k = slice->getRefPOC(list, ref) - slice->getLastIDR();
                        fprintf(m_csvfpt, " %d", k);
                    }
                }

                if (numLists == 1)
                    fprintf(m_csvfpt, ", -");
            }
            else
                fprintf(m_csvfpt, ", -, -");
            fprintf(m_csvfpt, "\n");
        }

        if (m_param->decodedPictureHashSEI && m_param->logLevel >= X265_LOG_FULL)
        {
            const char* digestStr = NULL;
            if (m_param->decodedPictureHashSEI == 1)
            {
                digestStr = digestToString(curEncoder->m_seiReconPictureDigest.m_digest, 16);
                p += sprintf(buf + p, " [MD5:%s]", digestStr);
            }
            else if (m_param->decodedPictureHashSEI == 2)
            {
                digestStr = digestToString(curEncoder->m_seiReconPictureDigest.m_digest, 2);
                p += sprintf(buf + p, " [CRC:%s]", digestStr);
            }
            else if (m_param->decodedPictureHashSEI == 3)
            {
                digestStr = digestToString(curEncoder->m_seiReconPictureDigest.m_digest, 4);
                p += sprintf(buf + p, " [Checksum:%s]", digestStr);
            }
        }
        x265_log(m_param, X265_LOG_DEBUG, "%s\n", buf);
        fflush(stderr);
    }
}

#if defined(_MSC_VER)
#pragma warning(disable: 4800) // forcing int to bool
#pragma warning(disable: 4127) // conditional expression is constant
#endif

void Encoder::initSPS(TComSPS *sps)
{
    m_ptl.levelIdc = m_level;
    m_ptl.tierFlag = m_levelTier ? true : false;
    m_ptl.profileIdc = m_profile;
    m_ptl.profileCompatibilityFlag[m_profile] = true;
    m_ptl.progressiveSourceFlag = !m_param->interlaceMode;
    m_ptl.interlacedSourceFlag = !!m_param->interlaceMode;
    m_ptl.nonPackedConstraintFlag = false;
    m_ptl.frameOnlyConstraintFlag = false;

    if (m_profile == Profile::MAIN10 && X265_DEPTH == 8)
        /* The above constraint is equal to Profile::MAIN */
        m_ptl.profileCompatibilityFlag[Profile::MAIN] = true;
    if (m_profile == Profile::MAIN)
        /* A Profile::MAIN10 decoder can always decode Profile::MAIN */
        m_ptl.profileCompatibilityFlag[Profile::MAIN10] = true;

    /* TODO: Range extension profiles */
    /* TODO: check final spec for compatibility rules here */

    sps->setPicWidthInLumaSamples(m_param->sourceWidth);
    sps->setPicHeightInLumaSamples(m_param->sourceHeight);
    sps->setConformanceWindow(m_conformanceWindow);
    sps->setChromaFormatIdc(m_param->internalCsp);
    sps->setMaxCUSize(g_maxCUSize);
    sps->setMaxCUDepth(g_maxCUDepth);

    int log2MinCUSize = g_maxLog2CUSize - (g_maxCUDepth - g_addCUDepth);

    sps->setLog2MinCodingBlockSize(log2MinCUSize);
    sps->setLog2DiffMaxMinCodingBlockSize(g_maxCUDepth - g_addCUDepth);

    sps->setQuadtreeTULog2MaxSize(m_quadtreeTULog2MaxSize);
    sps->setQuadtreeTULog2MinSize(m_quadtreeTULog2MinSize);
    sps->setQuadtreeTUMaxDepthInter(m_param->tuQTMaxInterDepth);
    sps->setQuadtreeTUMaxDepthIntra(m_param->tuQTMaxIntraDepth);

    sps->setTMVPFlagsPresent(false);
    sps->setUseSAO(m_param->bEnableSAO);
    sps->setUseAMP(m_param->bEnableAMP);
    sps->setAMPAcc(g_maxCUDepth - g_addCUDepth);

    sps->setBitDepthY(X265_DEPTH);
    sps->setBitDepthC(X265_DEPTH);

    sps->setQpBDOffsetY(QP_BD_OFFSET);
    sps->setQpBDOffsetC(QP_BD_OFFSET);

    sps->setMaxDecPicBuffering(m_vps.m_maxDecPicBuffering);
    sps->setNumReorderPics(m_vps.m_numReorderPics);

    sps->setScalingListFlag((m_useScalingListId == 0) ? 0 : 1);
    sps->setUseStrongIntraSmoothing(m_param->bEnableStrongIntraSmoothing);

    TComVUI* vui = sps->getVuiParameters();
    vui->aspectRatioInfoPresentFlag = !!m_param->vui.aspectRatioIdc;
    vui->aspectRatioIdc = m_param->vui.aspectRatioIdc;
    vui->sarWidth = m_param->vui.sarWidth;
    vui->sarHeight = m_param->vui.sarHeight;

    vui->overscanInfoPresentFlag = m_param->vui.bEnableOverscanInfoPresentFlag;
    vui->overscanAppropriateFlag = m_param->vui.bEnableOverscanAppropriateFlag;

    vui->videoSignalTypePresentFlag = m_param->vui.bEnableVideoSignalTypePresentFlag;
    vui->videoFormat = m_param->vui.videoFormat;
    vui->videoFullRangeFlag = m_param->vui.bEnableVideoFullRangeFlag;

    vui->colourDescriptionPresentFlag = m_param->vui.bEnableColorDescriptionPresentFlag;
    vui->colourPrimaries = m_param->vui.colorPrimaries;
    vui->transferCharacteristics = m_param->vui.transferCharacteristics;
    vui->matrixCoefficients = m_param->vui.matrixCoeffs;

    vui->chromaLocInfoPresentFlag = m_param->vui.bEnableChromaLocInfoPresentFlag;
    vui->chromaSampleLocTypeTopField = m_param->vui.chromaSampleLocTypeTopField;
    vui->chromaSampleLocTypeBottomField = m_param->vui.chromaSampleLocTypeBottomField;

    vui->defaultDisplayWindow = m_defaultDisplayWindow;

    vui->frameFieldInfoPresentFlag = !!m_param->interlaceMode;
    vui->fieldSeqFlag = !!m_param->interlaceMode;

    vui->hrdParametersPresentFlag = m_param->bEmitHRDSEI;

    vui->timingInfo.timingInfoPresentFlag = true;
    vui->timingInfo.numUnitsInTick = m_param->fpsDenom;
    vui->timingInfo.timeScale = m_param->fpsNum;
}

void Encoder::initPPS(TComPPS *pps)
{
    pps->setConstrainedIntraPred(m_param->bEnableConstrainedIntra);
    bool isVbv = m_param->rc.vbvBufferSize > 0 && m_param->rc.vbvMaxBitrate > 0;

    /* TODO: This variable m_maxCuDQPDepth needs to be a CLI option to allow us to choose AQ granularity */
    bool bUseDQP = (m_maxCuDQPDepth > 0 || m_param->rc.aqMode || isVbv) ? true : false;

    int lowestQP = -QP_BD_OFFSET;

    if ((m_maxCuDQPDepth == 0) && (m_param->rc.qp == lowestQP))
        bUseDQP = false;

    if (bUseDQP)
    {
        pps->setUseDQP(true);
        pps->setMaxCuDQPDepth(m_maxCuDQPDepth);
        pps->setMinCuDQPSize(pps->getSPS()->getMaxCUSize() >> (pps->getMaxCuDQPDepth()));
    }
    else
    {
        pps->setUseDQP(false);
        pps->setMaxCuDQPDepth(0);
        pps->setMinCuDQPSize(pps->getSPS()->getMaxCUSize() >> (pps->getMaxCuDQPDepth()));
    }

    pps->setChromaCbQpOffset(m_param->cbQpOffset);
    pps->setChromaCrQpOffset(m_param->crQpOffset);

    pps->setEntropyCodingSyncEnabledFlag(m_param->bEnableWavefront);
    pps->setUseWP(m_param->bEnableWeightedPred);
    pps->setWPBiPred(m_param->bEnableWeightedBiPred);
    pps->setSignHideFlag(m_param->bEnableSignHiding);
    pps->setDeblockingFilterControlPresentFlag(!m_param->bEnableLoopFilter);
    pps->setDeblockingFilterOverrideEnabledFlag(!m_loopFilterOffsetInPPS);
    pps->setPicDisableDeblockingFilterFlag(!m_param->bEnableLoopFilter);
    pps->setLog2ParallelMergeLevelMinus2(m_log2ParallelMergeLevelMinus2);
    pps->setCabacInitPresentFlag(m_param->frameNumThreads == 1);

    pps->setNumRefIdxL0DefaultActive(1);
    pps->setNumRefIdxL1DefaultActive(1);

    pps->setTransquantBypassEnableFlag(m_param->bCULossless || m_param->bLossless);
    pps->setUseTransformSkip(m_param->bEnableTransformSkip);
}

void Encoder::configure(x265_param *p)
{
    this->m_param = p;

    // Trim the thread pool if WPP is disabled
    if (!p->bEnableWavefront)
        p->poolNumThreads = 1;

    setThreadPool(ThreadPool::allocThreadPool(p->poolNumThreads));
    int poolThreadCount = ThreadPool::getThreadPool()->getThreadCount();
    uint32_t maxLog2CUSize = g_convertToBit[p->maxCUSize] + 2;
    int rows = (p->sourceHeight + p->maxCUSize - 1) >> maxLog2CUSize;

    if (p->frameNumThreads == 0)
    {
        // auto-detect frame threads
        int cpuCount = getCpuCount();
        if (poolThreadCount <= 1)
            p->frameNumThreads = X265_MIN(cpuCount, rows / 2);
        else if (cpuCount > 32)
            p->frameNumThreads = 6; // dual-socket 10-core IvyBridge or higher
        else if (cpuCount >= 16)
            p->frameNumThreads = 5; // 8 HT cores, or dual socket
        else if (cpuCount >= 8)
            p->frameNumThreads = 3; // 4 HT cores
        else if (cpuCount >= 4)
            p->frameNumThreads = 2; // Dual or Quad core
        else
            p->frameNumThreads = 1;
    }
    if (poolThreadCount > 1)
    {
        x265_log(p, X265_LOG_INFO, "WPP streams / pool / frames         : %d / %d / %d\n", rows, poolThreadCount, p->frameNumThreads);
    }
    else if (p->frameNumThreads > 1)
    {
        x265_log(p, X265_LOG_INFO, "Concurrently encoded frames         : %d\n", p->frameNumThreads);
        p->bEnableWavefront = 0;
    }
    else
    {
        x265_log(p, X265_LOG_INFO, "Parallelism disabled, single thread mode\n");
        p->bEnableWavefront = 0;
    }
    if (!p->saoLcuBasedOptimization && p->frameNumThreads > 1)
    {
        x265_log(p, X265_LOG_INFO, "Warning: picture-based SAO used with frame parallelism\n");
    }
    m_totalFrameThreads = m_param->frameNumThreads;

    if (p->keyframeMax < 0)
    {
        /* A negative max GOP size indicates the user wants only one I frame at
         * the start of the stream. Set an infinite GOP distance and disable
         * adaptive I frame placement */
        p->keyframeMax = INT_MAX;
        p->scenecutThreshold = 0;
    }
    else if (p->keyframeMax <= 1)
    {
        // disable lookahead for all-intra encodes
        p->bFrameAdaptive = 0;
        p->bframes = 0;
    }
    if (!p->keyframeMin)
    {
        double fps = (double)p->fpsNum / p->fpsDenom;
        p->keyframeMin = X265_MIN((int)fps, p->keyframeMax / 10);
    }
    p->keyframeMin = X265_MAX(1, X265_MIN(p->keyframeMin, p->keyframeMax / 2 + 1));
    if (!p->bEnableRectInter)
    {
        p->bEnableAMP = false;
    }
    if (p->bBPyramid && !p->bframes)
    {
        p->bBPyramid = 0;
    }

    // psy-rd is not supported in RD levels below 2
    if (p->rdLevel < 2)
        p->psyRd = 0.0;

    /* In 444, chroma gets twice as much resolution, so halve quality when psy-rd is enabled */
    if (p->internalCsp == X265_CSP_I444 && p->psyRd)
    {
        p->cbQpOffset += 6;
        p->crQpOffset += 6;
    }

    // disable RDOQ if psy-rd is enabled; until we make it psy-aware
    m_bEnableRDOQ = p->psyRd == 0.0 && p->rdLevel >= 4;

    if (p->bLossless)
    {
        p->rc.rateControlMode = X265_RC_CQP;
        p->rc.qp = 4; // An oddity, QP=4 is more lossless than QP=0 and gives better lambdas
        p->bEnableSsim = 0;
        p->bEnablePsnr = 0;
    }

    if (p->rc.rateControlMode == X265_RC_CQP)
    {
        p->rc.aqMode = X265_AQ_NONE;
        p->rc.bitrate = 0;
        p->rc.cuTree = 0;
        p->rc.aqStrength = 0;
    }

    if (p->rc.aqMode == 0 && p->rc.cuTree)
    {
        p->rc.aqMode = X265_AQ_VARIANCE;
        p->rc.aqStrength = 0.0;
    }

    if (p->lookaheadDepth == 0 && p->rc.cuTree && !p->rc.bStatRead)
    {
        x265_log(p, X265_LOG_WARNING, "cuTree disabled, requires lookahead to be enabled\n");
        p->rc.cuTree = 0;
    }

    if (p->rc.aqStrength == 0 && p->rc.cuTree == 0)
    {
        p->rc.aqMode = X265_AQ_NONE;
    }

    if (p->rc.aqMode == X265_AQ_NONE && p->rc.cuTree == 0)
    {
        p->rc.aqStrength = 0;
    }

    if (p->internalCsp != X265_CSP_I420)
    {
        x265_log(p, X265_LOG_WARNING, "!! HEVC Range Extension specifications are not finalized !!\n");
        x265_log(p, X265_LOG_WARNING, "!! This output bitstream may not be compliant with the final spec !!\n");
    }

    if (p->interlaceMode)
    {
        x265_log(p, X265_LOG_WARNING, "Support for interlaced video is experimental\n");
    }

    if (p->rc.rfConstantMin > p->rc.rfConstant)
    {
        x265_log(m_param, X265_LOG_WARNING, "CRF min must be less than CRF\n");
        p->rc.rfConstantMin = 0;
    }

    m_bframeDelay = p->bframes ? (p->bBPyramid ? 2 : 1) : 0;

    p->bFrameBias = X265_MIN(X265_MAX(-90, p->bFrameBias), 100);

    //====== Coding Tools ========

    uint32_t tuQTMaxLog2Size = maxLog2CUSize - 1;
    m_quadtreeTULog2MaxSize = tuQTMaxLog2Size;
    uint32_t tuQTMinLog2Size = 2; //log2(4)
    m_quadtreeTULog2MinSize = tuQTMinLog2Size;

    //========= set default display window ==================================
    m_defaultDisplayWindow.bEnabled = p->vui.bEnableDefaultDisplayWindowFlag;
    m_defaultDisplayWindow.rightOffset = p->vui.defDispWinRightOffset;
    m_defaultDisplayWindow.topOffset = p->vui.defDispWinTopOffset;
    m_defaultDisplayWindow.bottomOffset = p->vui.defDispWinBottomOffset;
    m_defaultDisplayWindow.leftOffset = p->vui.defDispWinLeftOffset;
    m_pad[0] = m_pad[1] = 0;

    //======== set pad size if width is not multiple of the minimum CU size =========
    uint32_t maxCUDepth = (uint32_t)g_convertToBit[p->maxCUSize];
    uint32_t minCUDepth = (p->maxCUSize >> (maxCUDepth - 1));
    if ((p->sourceWidth % minCUDepth) != 0)
    {
        uint32_t padsize = 0;
        uint32_t rem = p->sourceWidth % minCUDepth;
        padsize = minCUDepth - rem;
        p->sourceWidth += padsize;
        m_pad[0] = padsize; //pad width

        /* set the confirmation window offsets  */
        m_conformanceWindow.bEnabled = true;
        m_conformanceWindow.rightOffset += m_pad[0];
    }

    //======== set pad size if height is not multiple of the minimum CU size =========
    if ((p->sourceHeight % minCUDepth) != 0)
    {
        uint32_t padsize = 0;
        uint32_t rem = p->sourceHeight % minCUDepth;
        padsize = minCUDepth - rem;
        p->sourceHeight += padsize;
        m_pad[1] = padsize; //pad height

        /* set the confirmation window offsets  */
        m_conformanceWindow.bEnabled = true;
        m_conformanceWindow.bottomOffset += m_pad[1];
    }

    //====== HM Settings not exposed for configuration ======
    m_loopFilterOffsetInPPS = 0;
    m_loopFilterBetaOffsetDiv2 = 0;
    m_loopFilterTcOffsetDiv2 = 0;

    /* Increase the DPB size and reorder picture if bpyramid is enabled */
    m_vps.m_numReorderPics = (p->bBPyramid && p->bframes > 1) ? 2 : 1;
    m_vps.m_maxDecPicBuffering = X265_MIN(MAX_NUM_REF, X265_MAX(m_vps.m_numReorderPics + 1, (uint32_t)p->maxNumReferences) + m_vps.m_numReorderPics);

    m_maxCuDQPDepth = 0;
    m_maxNumOffsetsPerPic = 2048;
    m_log2ParallelMergeLevelMinus2 = 0;

    m_useScalingListId = 0;
    if (m_useScalingListId == SCALING_LIST_DEFAULT)
        // set default quantization matrix to array
        setDefaultScalingList();
}
