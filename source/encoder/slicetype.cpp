/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Gopu Govindaswamy <gopu@multicorewareinc.com>
 *          Steve Borho <steve@borho.org>
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
#include "frame.h"
#include "framedata.h"
#include "picyuv.h"
#include "primitives.h"
#include "lowres.h"
#include "mv.h"

#include "slicetype.h"
#include "motion.h"
#include "ratecontrol.h"

using namespace x265;

namespace {

inline int16_t median(int16_t a, int16_t b, int16_t c)
{
    int16_t t = (a - b) & ((a - b) >> 31);

    a -= t;
    b += t;
    b -= (b - c) & ((b - c) >> 31);
    b += (a - b) & ((a - b) >> 31);
    return b;
}

inline void median_mv(MV &dst, MV a, MV b, MV c)
{
    dst.x = median(a.x, b.x, c.x);
    dst.y = median(a.y, b.y, c.y);
}

/* Compute variance to derive AC energy of each block */
inline uint32_t acEnergyVar(Frame *curFrame, uint64_t sum_ssd, int shift, int plane)
{
    uint32_t sum = (uint32_t)sum_ssd;
    uint32_t ssd = (uint32_t)(sum_ssd >> 32);

    curFrame->m_lowres.wp_sum[plane] += sum;
    curFrame->m_lowres.wp_ssd[plane] += ssd;
    return ssd - ((uint64_t)sum * sum >> shift);
}

/* Find the energy of each block in Y/Cb/Cr plane */
inline uint32_t acEnergyPlane(Frame *curFrame, pixel* src, intptr_t srcStride, int plane, int colorFormat)
{
    if ((colorFormat != X265_CSP_I444) && plane)
    {
        ALIGN_VAR_8(pixel, pix[8 * 8]);
        primitives.cu[BLOCK_8x8].copy_pp(pix, 8, src, srcStride);
        return acEnergyVar(curFrame, primitives.cu[BLOCK_8x8].var(pix, 8), 6, plane);
    }
    else
        return acEnergyVar(curFrame, primitives.cu[BLOCK_16x16].var(src, srcStride), 8, plane);
}

} // end anonymous namespace

/* Find the total AC energy of each block in all planes */
uint32_t LookaheadTLD::acEnergyCu(Frame* curFrame, uint32_t blockX, uint32_t blockY, int csp)
{
    intptr_t stride = curFrame->m_fencPic->m_stride;
    intptr_t cStride = curFrame->m_fencPic->m_strideC;
    intptr_t blockOffsetLuma = blockX + (blockY * stride);
    int hShift = CHROMA_H_SHIFT(csp);
    int vShift = CHROMA_V_SHIFT(csp);
    intptr_t blockOffsetChroma = (blockX >> hShift) + ((blockY >> vShift) * cStride);

    uint32_t var;

    var  = acEnergyPlane(curFrame, curFrame->m_fencPic->m_picOrg[0] + blockOffsetLuma, stride, 0, csp);
    var += acEnergyPlane(curFrame, curFrame->m_fencPic->m_picOrg[1] + blockOffsetChroma, cStride, 1, csp);
    var += acEnergyPlane(curFrame, curFrame->m_fencPic->m_picOrg[2] + blockOffsetChroma, cStride, 2, csp);
    x265_emms();
    return var;
}

void LookaheadTLD::calcAdaptiveQuantFrame(Frame *curFrame, x265_param* param)
{
    /* Actual adaptive quantization */
    int maxCol = curFrame->m_fencPic->m_picWidth;
    int maxRow = curFrame->m_fencPic->m_picHeight;

    for (int y = 0; y < 3; y++)
    {
        curFrame->m_lowres.wp_ssd[y] = 0;
        curFrame->m_lowres.wp_sum[y] = 0;
    }

    /* Calculate Qp offset for each 16x16 block in the frame */
    int blockXY = 0;
    int blockX = 0, blockY = 0;
    double strength = 0.f;
    if (param->rc.aqMode == X265_AQ_NONE || param->rc.aqStrength == 0)
    {
        /* Need to init it anyways for CU tree */
        int cuCount = widthInCU * heightInCU;

        if (param->rc.aqMode && param->rc.aqStrength == 0)
        {
            memset(curFrame->m_lowres.qpCuTreeOffset, 0, cuCount * sizeof(double));
            memset(curFrame->m_lowres.qpAqOffset, 0, cuCount * sizeof(double));
            for (int cuxy = 0; cuxy < cuCount; cuxy++)
                curFrame->m_lowres.invQscaleFactor[cuxy] = 256;
        }

        /* Need variance data for weighted prediction */
        if (param->bEnableWeightedPred || param->bEnableWeightedBiPred)
        {
            for (blockY = 0; blockY < maxRow; blockY += 16)
                for (blockX = 0; blockX < maxCol; blockX += 16)
                    acEnergyCu(curFrame, blockX, blockY, param->internalCsp);
        }
    }
    else
    {
        blockXY = 0;
        double avg_adj_pow2 = 0, avg_adj = 0, qp_adj = 0;
        if (param->rc.aqMode == X265_AQ_AUTO_VARIANCE)
        {
            double bit_depth_correction = pow(1 << (X265_DEPTH - 8), 0.5);
            for (blockY = 0; blockY < maxRow; blockY += 16)
            {
                for (blockX = 0; blockX < maxCol; blockX += 16)
                {
                    uint32_t energy = acEnergyCu(curFrame, blockX, blockY, param->internalCsp);
                    qp_adj = pow(energy + 1, 0.1);
                    curFrame->m_lowres.qpCuTreeOffset[blockXY] = qp_adj;
                    avg_adj += qp_adj;
                    avg_adj_pow2 += qp_adj * qp_adj;
                    blockXY++;
                }
            }

            avg_adj /= ncu;
            avg_adj_pow2 /= ncu;
            strength = param->rc.aqStrength * avg_adj / bit_depth_correction;
            avg_adj = avg_adj - 0.5f * (avg_adj_pow2 - (11.f * bit_depth_correction)) / avg_adj;
        }
        else
            strength = param->rc.aqStrength * 1.0397f;

        blockXY = 0;
        for (blockY = 0; blockY < maxRow; blockY += 16)
        {
            for (blockX = 0; blockX < maxCol; blockX += 16)
            {
                if (param->rc.aqMode == X265_AQ_AUTO_VARIANCE)
                {
                    qp_adj = curFrame->m_lowres.qpCuTreeOffset[blockXY];
                    qp_adj = strength * (qp_adj - avg_adj);
                }
                else
                {
                    uint32_t energy = acEnergyCu(curFrame, blockX, blockY, param->internalCsp);
                    qp_adj = strength * (X265_LOG2(X265_MAX(energy, 1)) - (14.427f + 2 * (X265_DEPTH - 8)));
                }
                curFrame->m_lowres.qpAqOffset[blockXY] = qp_adj;
                curFrame->m_lowres.qpCuTreeOffset[blockXY] = qp_adj;
                curFrame->m_lowres.invQscaleFactor[blockXY] = x265_exp2fix8(qp_adj);
                blockXY++;
            }
        }
    }

    if (param->bEnableWeightedPred || param->bEnableWeightedBiPred)
    {
        int hShift = CHROMA_H_SHIFT(param->internalCsp);
        int vShift = CHROMA_V_SHIFT(param->internalCsp);
        maxCol = ((maxCol + 8) >> 4) << 4;
        maxRow = ((maxRow + 8) >> 4) << 4;
        int width[3]  = { maxCol, maxCol >> hShift, maxCol >> hShift };
        int height[3] = { maxRow, maxRow >> vShift, maxRow >> vShift };

        for (int i = 0; i < 3; i++)
        {
            uint64_t sum, ssd;
            sum = curFrame->m_lowres.wp_sum[i];
            ssd = curFrame->m_lowres.wp_ssd[i];
            curFrame->m_lowres.wp_ssd[i] = ssd - (sum * sum + (width[i] * height[i]) / 2) / (width[i] * height[i]);
        }
    }
}

void LookaheadTLD::lowresIntraEstimate(Lowres& fenc)
{
    ALIGN_VAR_32(pixel, prediction[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
    ALIGN_VAR_32(pixel, fencIntra[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
    pixel neighbours[2][X265_LOWRES_CU_SIZE * 4 + 1];

    const int lookAheadLambda = (int)x265_lambda_tab[X265_LOOKAHEAD_QP];
    const int intraPenalty = 5 * lookAheadLambda;
    const int lowresPenalty = 4; /* fixed CU cost overhead */

    const int cuSize  = X265_LOWRES_CU_SIZE;
    const int cuSize2 = cuSize << 1;
    const int sizeIdx = X265_LOWRES_CU_BITS - 2;

    pixel *planar = (cuSize >= 8) ? neighbours[1] : neighbours[0];
    pixelcmp_t satd = primitives.pu[sizeIdx].satd;

    for (int cuY = 0; cuY < heightInCU; cuY++)
    {
        fenc.rowSatds[0][0][cuY] = 0;

        for (int cuX = 0; cuX < widthInCU; cuX++)
        {
            const int cuXY = cuX + cuY * widthInCU;
            const intptr_t pelOffset = cuSize * cuX + cuSize * cuY * fenc.lumaStride;

            /* Prep reference pixels */
            pixel *pixCur = fenc.lowresPlane[0] + pelOffset;
            primitives.cu[sizeIdx].copy_pp(fencIntra, cuSize, pixCur, fenc.lumaStride);

            memcpy(neighbours[0], pixCur - 1 - fenc.lumaStride, (cuSize + 1) * sizeof(pixel));
            for (int i = 1; i < cuSize + 1; i++)
                neighbours[0][i + cuSize2] = pixCur[-1 - fenc.lumaStride + i * fenc.lumaStride]; /* todo: fixme */

            for (int i = 0; i < cuSize; i++)
            {
                neighbours[0][i + cuSize + 1] = neighbours[0][cuSize];                     // Copy above-last pixel
                neighbours[0][i + cuSize2 + cuSize + 1] = neighbours[0][cuSize2 + cuSize]; // Copy left-last pixel
            }

            neighbours[1][0]  = neighbours[0][0];                      // Copy top-left pixel 
            neighbours[1][cuSize2] = neighbours[0][cuSize2];           // Copy top-right pixel
            neighbours[1][cuSize2 << 1] = neighbours[0][cuSize2 << 1]; // Bottom-left pixel

            // Filter neighbour pixels with [1-2-1]
            neighbours[1][1]           = (neighbours[0][0] + (neighbours[0][1] << 1)           + neighbours[0][2] + 2)               >> 2;
            neighbours[1][cuSize2 + 1] = (neighbours[0][0] + (neighbours[0][cuSize2 + 1] << 1) + neighbours[0][cuSize2 + 1 + 1] + 2) >> 2;
            for (int i = 2; i < cuSize2; i++)
            {
                neighbours[1][i]           = (neighbours[0][i - 1]           + (neighbours[0][i] << 1)           + neighbours[0][i + 1]      + 2) >> 2;
                neighbours[1][cuSize2 + i] = (neighbours[0][cuSize2 + i - 1] + (neighbours[0][cuSize2 + i] << 1) + neighbours[0][cuSize2 + i + 1] + 2) >> 2;
            }

            int cost, icost = me.COST_MAX;
            uint32_t ilowmode = 0;

            /* DC and planar */
            primitives.cu[sizeIdx].intra_pred[DC_IDX](prediction, cuSize, neighbours[0], 0, (cuSize <= 16));
            cost = satd(fencIntra, cuSize, prediction, cuSize);
            COPY2_IF_LT(icost, cost, ilowmode, DC_IDX);

            primitives.cu[sizeIdx].intra_pred[PLANAR_IDX](prediction, cuSize, planar, 0, 0);
            cost = satd(fencIntra, cuSize, prediction, cuSize);
            COPY2_IF_LT(icost, cost, ilowmode, PLANAR_IDX);

            /* scan angular predictions */
            int filter, acost = me.COST_MAX;
            uint32_t mode, alowmode = 4;
            for (mode = 5; mode < 35; mode += 5)
            {
                filter = !!(g_intraFilterFlags[mode] & cuSize);
                primitives.cu[sizeIdx].intra_pred[mode](prediction, cuSize, neighbours[filter], mode, cuSize <= 16);
                cost = satd(fencIntra, cuSize, prediction, cuSize);
                COPY2_IF_LT(acost, cost, alowmode, mode);
            }
            for (uint32_t dist = 2; dist >= 1; dist--)
            {
                int minusmode = alowmode - dist;
                int plusmode = alowmode + dist;

                mode = minusmode;
                filter = !!(g_intraFilterFlags[mode] & cuSize);
                primitives.cu[sizeIdx].intra_pred[mode](prediction, cuSize, neighbours[filter], mode, cuSize <= 16);
                cost = satd(fencIntra, cuSize, prediction, cuSize);
                COPY2_IF_LT(acost, cost, alowmode, mode);

                mode = plusmode;
                filter = !!(g_intraFilterFlags[mode] & cuSize);
                primitives.cu[sizeIdx].intra_pred[mode](prediction, cuSize, neighbours[filter], mode, cuSize <= 16);
                cost = satd(fencIntra, cuSize, prediction, cuSize);
                COPY2_IF_LT(acost, cost, alowmode, mode);
            }
            COPY2_IF_LT(icost, acost, ilowmode, alowmode);

            icost += intraPenalty + lowresPenalty; /* estimate intra signal cost */

            fenc.lowresCosts[0][0][cuXY] = (uint16_t)(X265_MIN(icost, LOWRES_COST_MASK) | (0 << LOWRES_COST_SHIFT));
            fenc.intraCost[cuXY] = icost;
            fenc.intraMode[cuXY] = (uint8_t)ilowmode;
            fenc.rowSatds[0][0][cuY] += icost;
            fenc.costEst[0][0] += icost;
        }
    }
}

uint32_t LookaheadTLD::weightCostLuma(Lowres& fenc, Lowres& ref, WeightParam& wp)
{
    pixel *src = ref.fpelPlane[0];
    intptr_t stride = fenc.lumaStride;

    if (wp.bPresentFlag)
    {
        int offset = wp.inputOffset << (X265_DEPTH - 8);
        int scale = wp.inputWeight;
        int denom = wp.log2WeightDenom;
        int round = denom ? 1 << (denom - 1) : 0;
        int correction = IF_INTERNAL_PREC - X265_DEPTH; // intermediate interpolation depth
        int widthHeight = (int)stride;

        primitives.weight_pp(ref.buffer[0], wbuffer[0], stride, widthHeight, paddedLines,
            scale, round << correction, denom + correction, offset);
        src = weightedRef.fpelPlane[0];
    }

    uint32_t cost = 0;
    intptr_t pixoff = 0;
    int mb = 0;

    for (int y = 0; y < fenc.lines; y += 8, pixoff = y * stride)
    {
        for (int x = 0; x < fenc.width; x += 8, mb++, pixoff += 8)
        {
            int satd = primitives.pu[LUMA_8x8].satd(src + pixoff, stride, fenc.fpelPlane[0] + pixoff, stride);
            cost += X265_MIN(satd, fenc.intraCost[mb]);
        }
    }

    return cost;
}

bool LookaheadTLD::allocWeightedRef(Lowres& fenc)
{
    intptr_t planesize = fenc.buffer[1] - fenc.buffer[0];
    intptr_t padoffset = fenc.lowresPlane[0] - fenc.buffer[0];
    paddedLines = (int)(planesize / fenc.lumaStride);

    wbuffer[0] = X265_MALLOC(pixel, 4 * planesize);
    if (wbuffer[0])
    {
        wbuffer[1] = wbuffer[0] + planesize;
        wbuffer[2] = wbuffer[1] + planesize;
        wbuffer[3] = wbuffer[2] + planesize;
    }
    else
        return false;

    for (int i = 0; i < 4; i++)
        weightedRef.lowresPlane[i] = wbuffer[i] + padoffset;

    weightedRef.fpelPlane[0] = weightedRef.lowresPlane[0];
    weightedRef.lumaStride = fenc.lumaStride;
    weightedRef.isLowres = true;
    weightedRef.isWeighted = false;

    return true;
}

void LookaheadTLD::weightsAnalyse(Lowres& fenc, Lowres& ref)
{
    static const float epsilon = 1.f / 128.f;
    int deltaIndex = fenc.frameNum - ref.frameNum;

    WeightParam wp;
    wp.bPresentFlag = false;

    if (!wbuffer[0])
    {
        if (!allocWeightedRef(fenc))
            return;
    }

    /* epsilon is chosen to require at least a numerator of 127 (with denominator = 128) */
    float guessScale, fencMean, refMean;
    x265_emms();
    if (fenc.wp_ssd[0] && ref.wp_ssd[0])
        guessScale = sqrtf((float)fenc.wp_ssd[0] / ref.wp_ssd[0]);
    else
        guessScale = 1.0f;
    fencMean = (float)fenc.wp_sum[0] / (fenc.lines * fenc.width) / (1 << (X265_DEPTH - 8));
    refMean = (float)ref.wp_sum[0] / (fenc.lines * fenc.width) / (1 << (X265_DEPTH - 8));

    /* Early termination */
    if (fabsf(refMean - fencMean) < 0.5f && fabsf(1.f - guessScale) < epsilon)
        return;

    int minoff = 0, minscale, mindenom;
    unsigned int minscore = 0, origscore = 1;
    int found = 0;

    wp.setFromWeightAndOffset((int)(guessScale * 128 + 0.5f), 0, 7, true);
    mindenom = wp.log2WeightDenom;
    minscale = wp.inputWeight;

    origscore = minscore = weightCostLuma(fenc, ref, wp);

    if (!minscore)
        return;

    unsigned int s = 0;
    int curScale = minscale;
    int curOffset = (int)(fencMean - refMean * curScale / (1 << mindenom) + 0.5f);
    if (curOffset < -128 || curOffset > 127)
    {
        /* Rescale considering the constraints on curOffset. We do it in this order
        * because scale has a much wider range than offset (because of denom), so
        * it should almost never need to be clamped. */
        curOffset = x265_clip3(-128, 127, curOffset);
        curScale = (int)((1 << mindenom) * (fencMean - curOffset) / refMean + 0.5f);
        curScale = x265_clip3(0, 127, curScale);
    }
    SET_WEIGHT(wp, true, curScale, mindenom, curOffset);
    s = weightCostLuma(fenc, ref, wp);
    COPY4_IF_LT(minscore, s, minscale, curScale, minoff, curOffset, found, 1);

    /* Use a smaller denominator if possible */
    while (mindenom > 0 && !(minscale & 1))
    {
        mindenom--;
        minscale >>= 1;
    }

    if (!found || (minscale == 1 << mindenom && minoff == 0) || (float)minscore / origscore > 0.998f)
        return;
    else
    {
        SET_WEIGHT(wp, true, minscale, mindenom, minoff);

        // set weighted delta cost
        fenc.weightedCostDelta[deltaIndex] = minscore / origscore;

        int offset = wp.inputOffset << (X265_DEPTH - 8);
        int scale = wp.inputWeight;
        int denom = wp.log2WeightDenom;
        int round = denom ? 1 << (denom - 1) : 0;
        int correction = IF_INTERNAL_PREC - X265_DEPTH; // intermediate interpolation depth
        intptr_t stride = ref.lumaStride;
        int widthHeight = (int)stride;

        for (int i = 0; i < 4; i++)
            primitives.weight_pp(ref.buffer[i], wbuffer[i], stride, widthHeight, paddedLines,
            scale, round << correction, denom + correction, offset);

        weightedRef.isWeighted = true;
    }
}

Lookahead::Lookahead(x265_param *param, ThreadPool* pool)
{
    m_param = param;
    m_pool  = pool;

    m_lastNonB = NULL;
    m_scratch  = NULL;
    m_tld      = NULL;
    m_filled   = false;
    m_outputSignalRequired = false;
    m_isActive = true;

    m_heightInCU = ((m_param->sourceHeight / 2) + X265_LOWRES_CU_SIZE - 1) >> X265_LOWRES_CU_BITS;
    m_widthInCU = ((m_param->sourceWidth / 2) + X265_LOWRES_CU_SIZE - 1) >> X265_LOWRES_CU_BITS;
    m_ncu = m_widthInCU > 2 && m_heightInCU > 2 ? (m_widthInCU - 2) * (m_heightInCU - 2) : m_widthInCU * m_heightInCU;

    m_lastKeyframe = -m_param->keyframeMax;
    memset(m_preframes, 0, sizeof(m_preframes));
    m_preTotal = m_preAcquired = m_preCompleted = 0;
    m_sliceTypeBusy = false;
    m_fullQueueSize = m_param->lookaheadDepth;
    m_bAdaptiveQuant = m_param->rc.aqMode || m_param->bEnableWeightedPred || m_param->bEnableWeightedBiPred;

    /* If we have a thread pool and are using --b-adapt 2, it is generally
     * preferrable to perform all motion searches for each lowres frame in large
     * batched; this will create one job per --bframe per lowres frame, and
     * these jobs are performed by workers bonded to the thread running
     * slicetypeDecide() */
    m_bBatchMotionSearch = m_pool && m_param->bFrameAdaptive == X265_B_ADAPT_TRELLIS;

    /* It is also beneficial to pre-calculate all possible frame cost estimates
     * using worker threads bonded to the worker thread running
     * slicetypeDecide(). This creates bframes * bframes jobs which take less
     * time than the motion search batches but there are many of them. This may
     * do much unnecessary work, some frame cost estimates are not needed, so if
     * the thread pool is small we disable this feature after the initial burst
     * of work */
    m_bBatchFrameCosts = m_bBatchMotionSearch;

    if (m_bBatchMotionSearch && m_pool->m_numWorkers > 12)
    {
        m_numRowsPerSlice = m_heightInCU / (m_pool->m_numWorkers - 1);   // default to numWorkers - 1 slices
        m_numRowsPerSlice = X265_MAX(m_numRowsPerSlice, 10);             // at least 10 rows per slice
        m_numRowsPerSlice = X265_MIN(m_numRowsPerSlice, m_heightInCU);   // but no more than the full picture
        m_numCoopSlices = m_heightInCU / m_numRowsPerSlice;
    }
    else
    {
        m_numRowsPerSlice = m_heightInCU;
        m_numCoopSlices = 1;
    }

#if DETAILED_CU_STATS
    m_slicetypeDecideElapsedTime = 0;
    m_preLookaheadElapsedTime = 0;
    m_countSlicetypeDecide = 0;
    m_countPreLookahead = 0;
#endif

    memset(m_histogram, 0, sizeof(m_histogram));
}

#if DETAILED_CU_STATS
void Lookahead::getWorkerStats(int64_t& batchElapsedTime, uint64_t& batchCount, int64_t& coopSliceElapsedTime, uint64_t& coopSliceCount)
{
    batchElapsedTime = coopSliceElapsedTime = 0;
    coopSliceCount = batchCount = 0;
    int tldCount = m_pool ? m_pool->m_numWorkers : 1;
    for (int i = 0; i < tldCount; i++)
    {
        batchElapsedTime += m_tld[i].batchElapsedTime;
        coopSliceElapsedTime += m_tld[i].coopSliceElapsedTime;
        batchCount += m_tld[i].countBatches;
        coopSliceCount += m_tld[i].countCoopSlices;
    }
}
#endif

bool Lookahead::create()
{
    int numTLD = 1 + (m_pool ? m_pool->m_numWorkers : 0);
    m_tld = new LookaheadTLD[numTLD];
    for (int i = 0; i < numTLD; i++)
        m_tld[i].init(m_widthInCU, m_heightInCU, m_ncu);
    m_scratch = X265_MALLOC(int, m_tld[0].widthInCU);

    return m_tld && m_scratch;
}

void Lookahead::stop()
{
    if (m_pool && !m_inputQueue.empty())
    {
        m_preLookaheadLock.acquire();
        m_isActive = false;
        bool wait = m_outputSignalRequired = m_sliceTypeBusy;
        m_preLookaheadLock.release();

        if (wait)
            m_outputSignal.wait();
    }
}

void Lookahead::destroy()
{
    // these two queues will be empty unless the encode was aborted
    while (!m_inputQueue.empty())
    {
        Frame* curFrame = m_inputQueue.popFront();
        curFrame->destroy();
        delete curFrame;
    }

    while (!m_outputQueue.empty())
    {
        Frame* curFrame = m_outputQueue.popFront();
        curFrame->destroy();
        delete curFrame;
    }

    X265_FREE(m_scratch);

    delete [] m_tld;
}

/* The synchronization of slicetypeDecide is managed here.  The findJob() method
 * polls the occupancy of the input queue. If the queue is
 * full, it will run slicetypeDecide() and output a mini-gop of frames to the
 * output queue. If the flush() method has been called (implying no new pictures
 * will be received) then the input queue is considered full if it has even one
 * picture left. getDecidedPicture() removes pictures from the output queue and
 * only blocks as a last resort. It does not start removing pictures until
 * m_filled is true, which occurs after *more than* the lookahead depth of
 * pictures have been input so slicetypeDecide() should have started prior to
 * output pictures being withdrawn. The first slicetypeDecide() will obviously
 * still require a blocking wait, but after this slicetypeDecide() will maintain
 * its lead over the encoder (because one picture is added to the input queue
 * each time one is removed from the output) and decides slice types of pictures
 * just ahead of when the encoder needs them */

/* Called by API thread */
void Lookahead::addPicture(Frame& curFrame, int sliceType)
{
    curFrame.m_lowres.sliceType = sliceType;

    /* determine if the lookahead is (over) filled enough for frames to begin to
     * be consumed by frame encoders */
    if (!m_filled)
    {
        if (!m_param->bframes & !m_param->lookaheadDepth)
            m_filled = true; /* zero-latency */
        else if (curFrame.m_poc >= m_param->lookaheadDepth + 2 + m_param->bframes)
            m_filled = true; /* full capacity plus mini-gop lag */
    }

    m_preLookaheadLock.acquire();

    m_inputLock.acquire();
    m_inputQueue.pushBack(curFrame);
    m_inputLock.release();

    m_preframes[m_preTotal++] = &curFrame;
    X265_CHECK(m_preTotal <= X265_LOOKAHEAD_MAX, "prelookahead overflow\n");
    
    m_preLookaheadLock.release();

    if (m_pool)
        tryWakeOne();
}

/* Called by API thread */
void Lookahead::flush()
{
    /* force slicetypeDecide to run until the input queue is empty */
    m_fullQueueSize = 1;
    m_filled = true;
}

void Lookahead::findJob(int workerThreadID)
{
    Frame* preFrame;
    bool   doDecide;

    if (!m_isActive)
        return;

    int tld = workerThreadID;
    if (workerThreadID < 0)
        tld = m_pool ? m_pool->m_numWorkers : 0;

    m_preLookaheadLock.acquire();
    do
    {
        preFrame = NULL;
        doDecide = false;

        if (m_preTotal > m_preAcquired)
            preFrame = m_preframes[m_preAcquired++];
        else
        {
            if (m_preTotal == m_preCompleted)
                m_preAcquired = m_preTotal = m_preCompleted = 0;

            /* the worker thread that performs the last pre-lookahead will generally get to run
             * slicetypeDecide() */
            m_inputLock.acquire();
            if (!m_sliceTypeBusy && !m_preTotal && m_inputQueue.size() >= m_fullQueueSize && m_isActive)
                 doDecide = m_sliceTypeBusy = true;
            m_inputLock.release();
        }
        m_preLookaheadLock.release();

        if (preFrame)
        {
#if DETAILED_CU_STATS
            ScopedElapsedTime filterPerfScope(m_preLookaheadElapsedTime);
            m_countPreLookahead++;
#endif
            ProfileScopeEvent(prelookahead);

            preFrame->m_lowres.init(preFrame->m_fencPic, preFrame->m_poc);
            if (m_bAdaptiveQuant)
                m_tld[tld].calcAdaptiveQuantFrame(preFrame, m_param);
            m_tld[tld].lowresIntraEstimate(preFrame->m_lowres);

            m_preLookaheadLock.acquire(); /* re-acquire for next pass */
            m_preCompleted++;
        }
        else if (doDecide)
        {
#if DETAILED_CU_STATS
            ScopedElapsedTime filterPerfScope(m_slicetypeDecideElapsedTime);
            m_countSlicetypeDecide++;
#endif
            ProfileScopeEvent(slicetypeDecideEV);

            slicetypeDecide();

            m_preLookaheadLock.acquire(); /* re-acquire for next pass */
            if (m_outputSignalRequired)
            {
                m_outputSignal.trigger();
                m_outputSignalRequired = false;
            }
            m_sliceTypeBusy = false;
        }
    }
    while (preFrame || doDecide);

    m_helpWanted = false;
}

/* Called by API thread */
Frame* Lookahead::getDecidedPicture()
{
    if (m_filled)
    {
        m_outputLock.acquire();
        Frame *out = m_outputQueue.popFront();
        m_outputLock.release();

        if (out)
            return out;

        /* process all pending pre-lookahead frames and run slicetypeDecide() if
         * necessary */
        findJob(-1);

        m_preLookaheadLock.acquire();
        bool wait = m_outputSignalRequired = m_sliceTypeBusy || m_preTotal;
        m_preLookaheadLock.release();

        if (wait)
            m_outputSignal.wait();

        return m_outputQueue.popFront();
    }
    else
        return NULL;
}

/* Called by rate-control to calculate the estimated SATD cost for a given
 * picture.  It assumes dpb->prepareEncode() has already been called for the
 * picture and all the references are established */
void Lookahead::getEstimatedPictureCost(Frame *curFrame)
{
    Lowres *frames[X265_LOOKAHEAD_MAX];

    // POC distances to each reference
    Slice *slice = curFrame->m_encData->m_slice;
    int p0 = 0, p1, b;
    int poc = slice->m_poc;
    int l0poc = slice->m_refPOCList[0][0];
    int l1poc = slice->m_refPOCList[1][0];

    switch (slice->m_sliceType)
    {
    case I_SLICE:
        frames[p0] = &curFrame->m_lowres;
        b = p1 = 0;
        break;

    case P_SLICE:
        b = p1 = poc - l0poc;
        frames[p0] = &slice->m_refPicList[0][0]->m_lowres;
        frames[b] = &curFrame->m_lowres;
        break;

    case B_SLICE:
        b = poc - l0poc;
        p1 = b + l1poc - poc;
        frames[p0] = &slice->m_refPicList[0][0]->m_lowres;
        frames[b] = &curFrame->m_lowres;
        frames[p1] = &slice->m_refPicList[1][0]->m_lowres;
        break;

    default:
        return;
    }

    if (m_param->rc.cuTree && !m_param->rc.bStatRead)
        /* update row satds based on cutree offsets */
        curFrame->m_lowres.satdCost = frameCostRecalculate(frames, p0, p1, b);
    else if (m_param->rc.aqMode)
        curFrame->m_lowres.satdCost = curFrame->m_lowres.costEstAq[b - p0][p1 - b];
    else
        curFrame->m_lowres.satdCost = curFrame->m_lowres.costEst[b - p0][p1 - b];

    if (m_param->rc.vbvBufferSize && m_param->rc.vbvMaxBitrate)
    {
        /* aggregate lowres row satds to CTU resolution */
        curFrame->m_lowres.lowresCostForRc = curFrame->m_lowres.lowresCosts[b - p0][p1 - b];
        uint32_t lowresRow = 0, lowresCol = 0, lowresCuIdx = 0, sum = 0;
        uint32_t scale = m_param->maxCUSize / (2 * X265_LOWRES_CU_SIZE);
        uint32_t numCuInHeight = (m_param->sourceHeight + g_maxCUSize - 1) / g_maxCUSize;
        uint32_t widthInLowresCu = (uint32_t)m_widthInCU, heightInLowresCu = (uint32_t)m_heightInCU;
        double *qp_offset = 0;
        /* Factor in qpoffsets based on Aq/Cutree in CU costs */
        if (m_param->rc.aqMode)
            qp_offset = (frames[b]->sliceType == X265_TYPE_B || !m_param->rc.cuTree) ? frames[b]->qpAqOffset : frames[b]->qpCuTreeOffset;

        for (uint32_t row = 0; row < numCuInHeight; row++)
        {
            lowresRow = row * scale;
            for (uint32_t cnt = 0; cnt < scale && lowresRow < heightInLowresCu; lowresRow++, cnt++)
            {
                sum = 0;
                lowresCuIdx = lowresRow * widthInLowresCu;
                for (lowresCol = 0; lowresCol < widthInLowresCu; lowresCol++, lowresCuIdx++)
                {
                    uint16_t lowresCuCost = curFrame->m_lowres.lowresCostForRc[lowresCuIdx] & LOWRES_COST_MASK;
                    if (qp_offset)
                    {
                        lowresCuCost = (uint16_t)((lowresCuCost * x265_exp2fix8(qp_offset[lowresCuIdx]) + 128) >> 8);
                        int32_t intraCuCost = curFrame->m_lowres.intraCost[lowresCuIdx]; 
                        curFrame->m_lowres.intraCost[lowresCuIdx] = (intraCuCost * x265_exp2fix8(qp_offset[lowresCuIdx]) + 128) >> 8;
                    }
                    curFrame->m_lowres.lowresCostForRc[lowresCuIdx] = lowresCuCost;
                    sum += lowresCuCost;
                }
                curFrame->m_encData->m_rowStat[row].satdForVbv += sum;
            }
        }
    }
}

/* called by API thread or worker thread with inputQueueLock acquired */
void Lookahead::slicetypeDecide()
{
    Lowres *frames[X265_LOOKAHEAD_MAX];
    Frame *list[X265_LOOKAHEAD_MAX];
    int maxSearch = X265_MIN(m_param->lookaheadDepth, X265_LOOKAHEAD_MAX);

    memset(frames, 0, sizeof(frames));
    memset(list, 0, sizeof(list));
    {
        ScopedLock lock(m_inputLock);
        Frame *curFrame = m_inputQueue.first();
        int j;
        for (j = 0; j < m_param->bframes + 2; j++)
        {
            if (!curFrame) break;
            list[j] = curFrame;
            curFrame = curFrame->m_next;
        }

        curFrame = m_inputQueue.first();
        frames[0] = m_lastNonB;
        for (j = 0; j < maxSearch; j++)
        {
            if (!curFrame) break;
            frames[j + 1] = &curFrame->m_lowres;
            curFrame = curFrame->m_next;
        }

        maxSearch = j;
    }

    if (m_lastNonB && !m_param->rc.bStatRead &&
        ((m_param->bFrameAdaptive && m_param->bframes) ||
         m_param->rc.cuTree || m_param->scenecutThreshold ||
         (m_param->lookaheadDepth && m_param->rc.vbvBufferSize)))
    {
        slicetypeAnalyse(frames, false);
    }

    int bframes, brefs;
    for (bframes = 0, brefs = 0;; bframes++)
    {
        Lowres& frm = list[bframes]->m_lowres;

        if (frm.sliceType == X265_TYPE_BREF && !m_param->bBPyramid && brefs == m_param->bBPyramid)
        {
            frm.sliceType = X265_TYPE_B;
            x265_log(m_param, X265_LOG_WARNING, "B-ref at frame %d incompatible with B-pyramid\n",
                     frm.frameNum);
        }

        /* pyramid with multiple B-refs needs a big enough dpb that the preceding P-frame stays available.
         * smaller dpb could be supported by smart enough use of mmco, but it's easier just to forbid it. */
        else if (frm.sliceType == X265_TYPE_BREF && m_param->bBPyramid && brefs &&
                 m_param->maxNumReferences <= (brefs + 3))
        {
            frm.sliceType = X265_TYPE_B;
            x265_log(m_param, X265_LOG_WARNING, "B-ref at frame %d incompatible with B-pyramid and %d reference frames\n",
                     frm.sliceType, m_param->maxNumReferences);
        }

        if (/* (!param->intraRefresh || frm.frameNum == 0) && */ frm.frameNum - m_lastKeyframe >= m_param->keyframeMax)
        {
            if (frm.sliceType == X265_TYPE_AUTO || frm.sliceType == X265_TYPE_I)
                frm.sliceType = m_param->bOpenGOP && m_lastKeyframe >= 0 ? X265_TYPE_I : X265_TYPE_IDR;
            bool warn = frm.sliceType != X265_TYPE_IDR;
            if (warn && m_param->bOpenGOP)
                warn &= frm.sliceType != X265_TYPE_I;
            if (warn)
            {
                x265_log(m_param, X265_LOG_WARNING, "specified frame type (%d) at %d is not compatible with keyframe interval\n",
                         frm.sliceType, frm.frameNum);
                frm.sliceType = m_param->bOpenGOP && m_lastKeyframe >= 0 ? X265_TYPE_I : X265_TYPE_IDR;
            }
        }
        if (frm.sliceType == X265_TYPE_I && frm.frameNum - m_lastKeyframe >= m_param->keyframeMin)
        {
            if (m_param->bOpenGOP)
            {
                m_lastKeyframe = frm.frameNum;
                frm.bKeyframe = true;
            }
            else
                frm.sliceType = X265_TYPE_IDR;
        }
        if (frm.sliceType == X265_TYPE_IDR)
        {
            /* Closed GOP */
            m_lastKeyframe = frm.frameNum;
            frm.bKeyframe = true;
            if (bframes > 0)
            {
                list[bframes - 1]->m_lowres.sliceType = X265_TYPE_P;
                bframes--;
            }
        }
        if (bframes == m_param->bframes || !list[bframes + 1])
        {
            if (IS_X265_TYPE_B(frm.sliceType))
                x265_log(m_param, X265_LOG_WARNING, "specified frame type is not compatible with max B-frames\n");
            if (frm.sliceType == X265_TYPE_AUTO || IS_X265_TYPE_B(frm.sliceType))
                frm.sliceType = X265_TYPE_P;
        }
        if (frm.sliceType == X265_TYPE_BREF)
            brefs++;
        if (frm.sliceType == X265_TYPE_AUTO)
            frm.sliceType = X265_TYPE_B;
        else if (!IS_X265_TYPE_B(frm.sliceType))
            break;
    }

    if (bframes)
        list[bframes - 1]->m_lowres.bLastMiniGopBFrame = true;
    list[bframes]->m_lowres.leadingBframes = bframes;
    m_lastNonB = &list[bframes]->m_lowres;
    m_histogram[bframes]++;

    /* insert a bref into the sequence */
    if (m_param->bBPyramid && bframes > 1 && !brefs)
    {
        list[bframes / 2]->m_lowres.sliceType = X265_TYPE_BREF;
        brefs++;
    }
    /* calculate the frame costs ahead of time for estimateFrameCost while we still have lowres */
    if (m_param->rc.rateControlMode != X265_RC_CQP)
    {
        int p0, p1, b;
        /* For zero latency tuning, calculate frame cost to be used later in RC */
        if (!maxSearch)
        {
            for (int i = 0; i <= bframes; i++)
               frames[i + 1] = &list[i]->m_lowres;
        }

        /* estimate new non-B cost */
        p1 = b = bframes + 1;
        p0 = (IS_X265_TYPE_I(frames[bframes + 1]->sliceType)) ? b : 0;

        CostEstimateGroup estGroup(*this, frames);

        estGroup.singleCost(p0, p1, b);

        if (bframes)
        {
            p0 = 0; // last nonb
            for (b = 1; b <= bframes; b++)
            {
                if (frames[b]->sliceType == X265_TYPE_B)
                    for (p1 = b; frames[p1]->sliceType == X265_TYPE_B; p1++)
                        ; // find new nonb or bref
                else
                    p1 = bframes + 1;

                estGroup.singleCost(p0, p1, b);

                if (frames[b]->sliceType == X265_TYPE_BREF)
                    p0 = b;
            }
        }
    }

    m_inputLock.acquire();
    /* dequeue all frames from inputQueue that are about to be enqueued
     * in the output queue. The order is important because Frame can
     * only be in one list at a time */
    int64_t pts[X265_BFRAME_MAX + 1];
    for (int i = 0; i <= bframes; i++)
    {
        Frame *curFrame;
        curFrame = m_inputQueue.popFront();
        pts[i] = curFrame->m_pts;
        maxSearch--;
    }
    m_inputLock.release();

    m_outputLock.acquire();
    /* add non-B to output queue */
    int idx = 0;
    list[bframes]->m_reorderedPts = pts[idx++];
    m_outputQueue.pushBack(*list[bframes]);

    /* Add B-ref frame next to P frame in output queue, the B-ref encode before non B-ref frame */
    if (bframes > 1 && m_param->bBPyramid)
    {
        for (int i = 0; i < bframes; i++)
        {
            if (list[i]->m_lowres.sliceType == X265_TYPE_BREF)
            {
                list[i]->m_reorderedPts = pts[idx++];
                m_outputQueue.pushBack(*list[i]);
            }
        }
    }

    /* add B frames to output queue */
    for (int i = 0; i < bframes; i++)
    {
        /* push all the B frames into output queue except B-ref, which already pushed into output queue */
        if (list[i]->m_lowres.sliceType != X265_TYPE_BREF)
        {
            list[i]->m_reorderedPts = pts[idx++];
            m_outputQueue.pushBack(*list[i]);
        }
    }
    m_outputLock.release();

    bool isKeyFrameAnalyse = (m_param->rc.cuTree || (m_param->rc.vbvBufferSize && m_param->lookaheadDepth)) && !m_param->rc.bStatRead;
    if (isKeyFrameAnalyse && IS_X265_TYPE_I(m_lastNonB->sliceType))
    {
        m_inputLock.acquire();
        Frame *curFrame = m_inputQueue.first();
        frames[0] = m_lastNonB;
        int j;
        for (j = 0; j < maxSearch; j++)
        {
            frames[j + 1] = &curFrame->m_lowres;
            curFrame = curFrame->m_next;
        }
        m_inputLock.release();

        frames[j + 1] = NULL;
        slicetypeAnalyse(frames, true);
    }
}

void Lookahead::vbvLookahead(Lowres **frames, int numFrames, int keyframe)
{
    int prevNonB = 0, curNonB = 1, idx = 0;
    while (curNonB < numFrames && frames[curNonB]->sliceType == X265_TYPE_B)
        curNonB++;
    int nextNonB = keyframe ? prevNonB : curNonB;
    int nextB = prevNonB + 1;
    int nextBRef = 0, curBRef = 0;
    if (m_param->bBPyramid && curNonB - prevNonB > 1)
        curBRef = (prevNonB + curNonB + 1) / 2;
    int miniGopEnd = keyframe ? prevNonB : curNonB;
    while (curNonB < numFrames + !keyframe)
    {
        /* P/I cost: This shouldn't include the cost of nextNonB */
        if (nextNonB != curNonB)
        {
            int p0 = IS_X265_TYPE_I(frames[curNonB]->sliceType) ? curNonB : prevNonB;
            frames[nextNonB]->plannedSatd[idx] = vbvFrameCost(frames, p0, curNonB, curNonB);
            frames[nextNonB]->plannedType[idx] = frames[curNonB]->sliceType;

            /* Save the nextNonB Cost in each B frame of the current miniGop */
            if (curNonB > miniGopEnd)
            {
                for (int j = nextB; j < miniGopEnd; j++)
                {
                    frames[j]->plannedSatd[frames[j]->indB] = frames[nextNonB]->plannedSatd[idx];
                    frames[j]->plannedType[frames[j]->indB++] = frames[nextNonB]->plannedType[idx];
                }
            }
            idx++;
        }

        /* Handle the B-frames: coded order */
        if (m_param->bBPyramid && curNonB - prevNonB > 1)
            nextBRef = (prevNonB + curNonB + 1) / 2;

        for (int i = prevNonB + 1; i < curNonB; i++, idx++)
        {
            int64_t satdCost = 0;
            int type = X265_TYPE_B;
            if (nextBRef)
            {
                if (i == nextBRef)
                {
                    satdCost = vbvFrameCost(frames, prevNonB, curNonB, nextBRef);
                    type = X265_TYPE_BREF;
                }
                else if (i < nextBRef)
                    satdCost = vbvFrameCost(frames, prevNonB, nextBRef, i);
                else
                    satdCost = vbvFrameCost(frames, nextBRef, curNonB, i);
            }
            else
                satdCost = vbvFrameCost(frames, prevNonB, curNonB, i);
            frames[nextNonB]->plannedSatd[idx] = satdCost;
            frames[nextNonB]->plannedType[idx] = type;
            /* Save the nextB Cost in each B frame of the current miniGop */

            for (int j = nextB; j < miniGopEnd; j++)
            {
                if (curBRef && curBRef == i)
                    break;
                if (j >= i && j !=nextBRef)
                    continue;
                frames[j]->plannedSatd[frames[j]->indB] = satdCost;
                frames[j]->plannedType[frames[j]->indB++] = type;
            }
        }
        prevNonB = curNonB;
        curNonB++;
        while (curNonB <= numFrames && frames[curNonB]->sliceType == X265_TYPE_B)
            curNonB++;
    }

    frames[nextNonB]->plannedType[idx] = X265_TYPE_AUTO;
}

int64_t Lookahead::vbvFrameCost(Lowres **frames, int p0, int p1, int b)
{
    CostEstimateGroup estGroup(*this, frames);
    int64_t cost = estGroup.singleCost(p0, p1, b);

    if (m_param->rc.aqMode)
    {
        if (m_param->rc.cuTree)
            return frameCostRecalculate(frames, p0, p1, b);
        else
            return frames[b]->costEstAq[b - p0][p1 - b];
    }

    return cost;
}

void Lookahead::slicetypeAnalyse(Lowres **frames, bool bKeyframe)
{
    int numFrames, origNumFrames, keyintLimit, framecnt;
    int maxSearch = X265_MIN(m_param->lookaheadDepth, X265_LOOKAHEAD_MAX);
    int cuCount = m_ncu;
    int resetStart;
    bool bIsVbvLookahead = m_param->rc.vbvBufferSize && m_param->lookaheadDepth;

    /* count undecided frames */
    for (framecnt = 0; framecnt < maxSearch; framecnt++)
    {
        Lowres *fenc = frames[framecnt + 1];
        if (!fenc || fenc->sliceType != X265_TYPE_AUTO)
            break;
    }

    if (!framecnt)
    {
        if (m_param->rc.cuTree)
            cuTree(frames, 0, bKeyframe);
        return;
    }

    frames[framecnt + 1] = NULL;

    keyintLimit = m_param->keyframeMax - frames[0]->frameNum + m_lastKeyframe - 1;
    origNumFrames = numFrames = X265_MIN(framecnt, keyintLimit);

    if (bIsVbvLookahead)
        numFrames = framecnt;
    else if (m_param->bOpenGOP && numFrames < framecnt)
        numFrames++;
    else if (numFrames == 0)
    {
        frames[1]->sliceType = X265_TYPE_I;
        return;
    }

    if (m_bBatchMotionSearch)
    {
        /* pre-calculate all motion searches, using many worker threads */
        CostEstimateGroup estGroup(*this, frames);
        for (int b = 2; b < numFrames; b++)
        {
            for (int i = 1; i <= m_param->bframes + 1; i++)
            {
                if (b >= i && frames[b]->lowresMvs[0][i - 1][0].x == 0x7FFF)
                    estGroup.add(b - i, b + i < numFrames ? b + i : b, b);
            }
        }
        /* auto-disable after the first batch if pool is small */
        m_bBatchMotionSearch &= m_pool->m_numWorkers >= 4;
        estGroup.finishBatch();

        if (m_bBatchFrameCosts)
        {
            /* pre-calculate all frame cost estimates, using many worker threads */
            for (int b = 2; b < numFrames; b++)
            {
                for (int i = 1; i <= m_param->bframes + 1; i++)
                {
                    if (b < i)
                        continue;

                    for (int j = 0; j <= m_param->bframes; j++)
                    {
                        if (b + j < numFrames && frames[b]->costEst[i][j] < 0)
                            estGroup.add(b - i, b + j, b);
                    }
                }
            }

            /* auto-disable after the first batch if the pool is not large */
            m_bBatchFrameCosts &= m_pool->m_numWorkers > 12;
            estGroup.finishBatch();
        }
    }

    int numBFrames = 0;
    int numAnalyzed = numFrames;
    if (m_param->scenecutThreshold && scenecut(frames, 0, 1, true, origNumFrames, maxSearch))
    {
        frames[1]->sliceType = X265_TYPE_I;
        return;
    }

    if (m_param->bframes)
    {
        if (m_param->bFrameAdaptive == X265_B_ADAPT_TRELLIS)
        {
            if (numFrames > 1)
            {
                char best_paths[X265_BFRAME_MAX + 1][X265_LOOKAHEAD_MAX + 1] = { "", "P" };
                int best_path_index = numFrames % (X265_BFRAME_MAX + 1);

                /* Perform the frame type analysis. */
                for (int j = 2; j <= numFrames; j++)
                    slicetypePath(frames, j, best_paths);

                numBFrames = (int)strspn(best_paths[best_path_index], "B");

                /* Load the results of the analysis into the frame types. */
                for (int j = 1; j < numFrames; j++)
                    frames[j]->sliceType = best_paths[best_path_index][j - 1] == 'B' ? X265_TYPE_B : X265_TYPE_P;
            }
            frames[numFrames]->sliceType = X265_TYPE_P;
        }
        else if (m_param->bFrameAdaptive == X265_B_ADAPT_FAST)
        {
            CostEstimateGroup estGroup(*this, frames);

            int64_t cost1p0, cost2p0, cost1b1, cost2p1;

            for (int i = 0; i <= numFrames - 2; )
            {
                cost2p1 = estGroup.singleCost(i + 0, i + 2, i + 2, true);
                if (frames[i + 2]->intraMbs[2] > cuCount / 2)
                {
                    frames[i + 1]->sliceType = X265_TYPE_P;
                    frames[i + 2]->sliceType = X265_TYPE_P;
                    i += 2;
                    continue;
                }

                cost1b1 = estGroup.singleCost(i + 0, i + 2, i + 1);
                cost1p0 = estGroup.singleCost(i + 0, i + 1, i + 1);
                cost2p0 = estGroup.singleCost(i + 1, i + 2, i + 2);

                if (cost1p0 + cost2p0 < cost1b1 + cost2p1)
                {
                    frames[i + 1]->sliceType = X265_TYPE_P;
                    i += 1;
                    continue;
                }

// arbitrary and untuned
#define INTER_THRESH 300
#define P_SENS_BIAS (50 - m_param->bFrameBias)
                frames[i + 1]->sliceType = X265_TYPE_B;

                int j;
                for (j = i + 2; j <= X265_MIN(i + m_param->bframes, numFrames - 1); j++)
                {
                    int64_t pthresh = X265_MAX(INTER_THRESH - P_SENS_BIAS * (j - i - 1), INTER_THRESH / 10);
                    int64_t pcost = estGroup.singleCost(i + 0, j + 1, j + 1, true);
                    if (pcost > pthresh * cuCount || frames[j + 1]->intraMbs[j - i + 1] > cuCount / 3)
                        break;
                    frames[j]->sliceType = X265_TYPE_B;
                }

                frames[j]->sliceType = X265_TYPE_P;
                i = j;
            }
            frames[numFrames]->sliceType = X265_TYPE_P;
            numBFrames = 0;
            while (numBFrames < numFrames && frames[numBFrames + 1]->sliceType == X265_TYPE_B)
                numBFrames++;
        }
        else
        {
            numBFrames = X265_MIN(numFrames - 1, m_param->bframes);
            for (int j = 1; j < numFrames; j++)
                frames[j]->sliceType = (j % (numBFrames + 1)) ? X265_TYPE_B : X265_TYPE_P;

            frames[numFrames]->sliceType = X265_TYPE_P;
        }

        /* Check scenecut on the first minigop. */
        for (int j = 1; j < numBFrames + 1; j++)
        {
            if (m_param->scenecutThreshold && scenecut(frames, j, j + 1, false, origNumFrames, maxSearch))
            {
                frames[j]->sliceType = X265_TYPE_P;
                numAnalyzed = j;
                break;
            }
        }

        resetStart = bKeyframe ? 1 : X265_MIN(numBFrames + 2, numAnalyzed + 1);
    }
    else
    {
        for (int j = 1; j <= numFrames; j++)
            frames[j]->sliceType = X265_TYPE_P;

        resetStart = bKeyframe ? 1 : 2;
    }

    if (m_param->rc.cuTree)
        cuTree(frames, X265_MIN(numFrames, m_param->keyframeMax), bKeyframe);

    // if (!param->bIntraRefresh)
    for (int j = keyintLimit + 1; j <= numFrames; j += m_param->keyframeMax)
    {
        frames[j]->sliceType = X265_TYPE_I;
        resetStart = X265_MIN(resetStart, j + 1);
    }

    if (bIsVbvLookahead)
        vbvLookahead(frames, numFrames, bKeyframe);

    /* Restore frame types for all frames that haven't actually been decided yet. */
    for (int j = resetStart; j <= numFrames; j++)
        frames[j]->sliceType = X265_TYPE_AUTO;
}

bool Lookahead::scenecut(Lowres **frames, int p0, int p1, bool bRealScenecut, int numFrames, int maxSearch)
{
    /* Only do analysis during a normal scenecut check. */
    if (bRealScenecut && m_param->bframes)
    {
        int origmaxp1 = p0 + 1;
        /* Look ahead to avoid coding short flashes as scenecuts. */
        if (m_param->bFrameAdaptive == X265_B_ADAPT_TRELLIS)
            /* Don't analyse any more frames than the trellis would have covered. */
            origmaxp1 += m_param->bframes;
        else
            origmaxp1++;
        int maxp1 = X265_MIN(origmaxp1, numFrames);

        /* Where A and B are scenes: AAAAAABBBAAAAAA
         * If BBB is shorter than (maxp1-p0), it is detected as a flash
         * and not considered a scenecut. */
        for (int cp1 = p1; cp1 <= maxp1; cp1++)
        {
            if (!scenecutInternal(frames, p0, cp1, false))
                /* Any frame in between p0 and cur_p1 cannot be a real scenecut. */
                for (int i = cp1; i > p0; i--)
                    frames[i]->bScenecut = false;
        }

        /* Where A-F are scenes: AAAAABBCCDDEEFFFFFF
         * If each of BB ... EE are shorter than (maxp1-p0), they are
         * detected as flashes and not considered scenecuts.
         * Instead, the first F frame becomes a scenecut.
         * If the video ends before F, no frame becomes a scenecut. */
        for (int cp0 = p0; cp0 <= maxp1; cp0++)
        {
            if (origmaxp1 > maxSearch || (cp0 < maxp1 && scenecutInternal(frames, cp0, maxp1, false)))
                /* If cur_p0 is the p0 of a scenecut, it cannot be the p1 of a scenecut. */
                frames[cp0]->bScenecut = false;
        }
    }

    /* Ignore frames that are part of a flash, i.e. cannot be real scenecuts. */
    if (!frames[p1]->bScenecut)
        return false;
    return scenecutInternal(frames, p0, p1, bRealScenecut);
}

bool Lookahead::scenecutInternal(Lowres **frames, int p0, int p1, bool bRealScenecut)
{
    Lowres *frame = frames[p1];

    CostEstimateGroup estGroup(*this, frames);
    estGroup.singleCost(p0, p1, p1);

    int64_t icost = frame->costEst[0][0];
    int64_t pcost = frame->costEst[p1 - p0][0];
    int gopSize = frame->frameNum - m_lastKeyframe;
    float threshMax = (float)(m_param->scenecutThreshold / 100.0);

    /* magic numbers pulled out of thin air */
    float threshMin = (float)(threshMax * 0.25);
    float bias;

    if (m_param->keyframeMin == m_param->keyframeMax)
        threshMin = threshMax;
    if (gopSize <= m_param->keyframeMin / 4)
        bias = threshMin / 4;
    else if (gopSize <= m_param->keyframeMin)
        bias = threshMin * gopSize / m_param->keyframeMin;
    else
    {
        bias = threshMin
            + (threshMax - threshMin)
            * (gopSize - m_param->keyframeMin)
            / (m_param->keyframeMax - m_param->keyframeMin);
    }

    bool res = pcost >= (1.0 - bias) * icost;
    if (res && bRealScenecut)
    {
        int imb = frame->intraMbs[p1 - p0];
        int pmb = m_ncu - imb;
        x265_log(m_param, X265_LOG_DEBUG, "scene cut at %d Icost:%d Pcost:%d ratio:%.4f bias:%.4f gop:%d (imb:%d pmb:%d)\n",
                 frame->frameNum, icost, pcost, 1. - (double)pcost / icost, bias, gopSize, imb, pmb);
    }
    return res;
}

void Lookahead::slicetypePath(Lowres **frames, int length, char(*best_paths)[X265_LOOKAHEAD_MAX + 1])
{
    char paths[2][X265_LOOKAHEAD_MAX + 1];
    int num_paths = X265_MIN(m_param->bframes + 1, length);
    int64_t best_cost = 1LL << 62;
    int idx = 0;

    /* Iterate over all currently possible paths */
    for (int path = 0; path < num_paths; path++)
    {
        /* Add suffixes to the current path */
        int len = length - (path + 1);
        memcpy(paths[idx], best_paths[len % (X265_BFRAME_MAX + 1)], len);
        memset(paths[idx] + len, 'B', path);
        strcpy(paths[idx] + len + path, "P");

        /* Calculate the actual cost of the current path */
        int64_t cost = slicetypePathCost(frames, paths[idx], best_cost);
        if (cost < best_cost)
        {
            best_cost = cost;
            idx ^= 1;
        }
    }

    /* Store the best path. */
    memcpy(best_paths[length % (X265_BFRAME_MAX + 1)], paths[idx ^ 1], length);
}

int64_t Lookahead::slicetypePathCost(Lowres **frames, char *path, int64_t threshold)
{
    int64_t cost = 0;
    int loc = 1;
    int cur_p = 0;

    CostEstimateGroup estGroup(*this, frames);

    path--; /* Since the 1st path element is really the second frame */
    while (path[loc])
    {
        int next_p = loc;
        /* Find the location of the next P-frame. */
        while (path[next_p] != 'P')
            next_p++;

        /* Add the cost of the P-frame found above */
        cost += estGroup.singleCost(cur_p, next_p, next_p);

        /* Early terminate if the cost we have found is larger than the best path cost so far */
        if (cost > threshold)
            break;

        if (m_param->bBPyramid && next_p - cur_p > 2)
        {
            int middle = cur_p + (next_p - cur_p) / 2;
            cost += estGroup.singleCost(cur_p, next_p, middle);

            for (int next_b = loc; next_b < middle && cost < threshold; next_b++)
                cost += estGroup.singleCost(cur_p, middle, next_b);

            for (int next_b = middle + 1; next_b < next_p && cost < threshold; next_b++)
                cost += estGroup.singleCost(middle, next_p, next_b);
        }
        else
        {
            for (int next_b = loc; next_b < next_p && cost < threshold; next_b++)
                cost += estGroup.singleCost(cur_p, next_p, next_b);
        }

        loc = next_p + 1;
        cur_p = next_p;
    }

    return cost;
}

void Lookahead::cuTree(Lowres **frames, int numframes, bool bIntra)
{
    int idx = !bIntra;
    int lastnonb, curnonb = 1;
    int bframes = 0;

    x265_emms();
    double totalDuration = 0.0;
    for (int j = 0; j <= numframes; j++)
        totalDuration += (double)m_param->fpsDenom / m_param->fpsNum;

    double averageDuration = totalDuration / (numframes + 1);

    int i = numframes;
    int cuCount = m_widthInCU * m_heightInCU;

    while (i > 0 && frames[i]->sliceType == X265_TYPE_B)
        i--;

    lastnonb = i;

    /* Lookaheadless MB-tree is not a theoretically distinct case; the same extrapolation could
     * be applied to the end of a lookahead buffer of any size.  However, it's most needed when
     * lookahead=0, so that's what's currently implemented. */
    if (!m_param->lookaheadDepth)
    {
        if (bIntra)
        {
            memset(frames[0]->propagateCost, 0, cuCount * sizeof(uint16_t));
            memcpy(frames[0]->qpCuTreeOffset, frames[0]->qpAqOffset, cuCount * sizeof(double));
            return;
        }
        std::swap(frames[lastnonb]->propagateCost, frames[0]->propagateCost);
        memset(frames[0]->propagateCost, 0, cuCount * sizeof(uint16_t));
    }
    else
    {
        if (lastnonb < idx)
            return;
        memset(frames[lastnonb]->propagateCost, 0, cuCount * sizeof(uint16_t));
    }

    CostEstimateGroup estGroup(*this, frames);

    while (i-- > idx)
    {
        curnonb = i;
        while (frames[curnonb]->sliceType == X265_TYPE_B && curnonb > 0)
            curnonb--;

        if (curnonb < idx)
            break;

        estGroup.singleCost(curnonb, lastnonb, lastnonb);

        memset(frames[curnonb]->propagateCost, 0, cuCount * sizeof(uint16_t));
        bframes = lastnonb - curnonb - 1;
        if (m_param->bBPyramid && bframes > 1)
        {
            int middle = (bframes + 1) / 2 + curnonb;
            estGroup.singleCost(curnonb, lastnonb, middle);
            memset(frames[middle]->propagateCost, 0, cuCount * sizeof(uint16_t));
            while (i > curnonb)
            {
                int p0 = i > middle ? middle : curnonb;
                int p1 = i < middle ? middle : lastnonb;
                if (i != middle)
                {
                    estGroup.singleCost(p0, p1, i);
                    estimateCUPropagate(frames, averageDuration, p0, p1, i, 0);
                }
                i--;
            }

            estimateCUPropagate(frames, averageDuration, curnonb, lastnonb, middle, 1);
        }
        else
        {
            while (i > curnonb)
            {
                estGroup.singleCost(curnonb, lastnonb, i);
                estimateCUPropagate(frames, averageDuration, curnonb, lastnonb, i, 0);
                i--;
            }
        }
        estimateCUPropagate(frames, averageDuration, curnonb, lastnonb, lastnonb, 1);
        lastnonb = curnonb;
    }

    if (!m_param->lookaheadDepth)
    {
        estGroup.singleCost(0, lastnonb, lastnonb);
        estimateCUPropagate(frames, averageDuration, 0, lastnonb, lastnonb, 1);
        std::swap(frames[lastnonb]->propagateCost, frames[0]->propagateCost);
    }

    cuTreeFinish(frames[lastnonb], averageDuration, lastnonb);
    if (m_param->bBPyramid && bframes > 1 && !m_param->rc.vbvBufferSize)
        cuTreeFinish(frames[lastnonb + (bframes + 1) / 2], averageDuration, 0);
}

void Lookahead::estimateCUPropagate(Lowres **frames, double averageDuration, int p0, int p1, int b, int referenced)
{
    uint16_t *refCosts[2] = { frames[p0]->propagateCost, frames[p1]->propagateCost };
    int32_t distScaleFactor = (((b - p0) << 8) + ((p1 - p0) >> 1)) / (p1 - p0);
    int32_t bipredWeight = m_param->bEnableWeightedBiPred ? 64 - (distScaleFactor >> 2) : 32;
    MV *mvs[2] = { frames[b]->lowresMvs[0][b - p0 - 1], frames[b]->lowresMvs[1][p1 - b - 1] };
    int32_t bipredWeights[2] = { bipredWeight, 64 - bipredWeight };

    memset(m_scratch, 0, m_widthInCU * sizeof(int));

    uint16_t *propagateCost = frames[b]->propagateCost;

    x265_emms();
    double fpsFactor = CLIP_DURATION((double)m_param->fpsDenom / m_param->fpsNum) / CLIP_DURATION(averageDuration);

    /* For non-referred frames the source costs are always zero, so just memset one row and re-use it. */
    if (!referenced)
        memset(frames[b]->propagateCost, 0, m_widthInCU * sizeof(uint16_t));

    int32_t StrideInCU = m_widthInCU;
    for (uint16_t blocky = 0; blocky < m_heightInCU; blocky++)
    {
        int cuIndex = blocky * StrideInCU;
        primitives.propagateCost(m_scratch, propagateCost,
                                 frames[b]->intraCost + cuIndex, frames[b]->lowresCosts[b - p0][p1 - b] + cuIndex,
                                 frames[b]->invQscaleFactor + cuIndex, &fpsFactor, m_widthInCU);

        if (referenced)
            propagateCost += m_widthInCU;

        for (uint16_t blockx = 0; blockx < m_widthInCU; blockx++, cuIndex++)
        {
            int32_t propagate_amount = m_scratch[blockx];
            /* Don't propagate for an intra block. */
            if (propagate_amount > 0)
            {
                /* Access width-2 bitfield. */
                int32_t lists_used = frames[b]->lowresCosts[b - p0][p1 - b][cuIndex] >> LOWRES_COST_SHIFT;
                /* Follow the MVs to the previous frame(s). */
                for (uint16_t list = 0; list < 2; list++)
                {
                    if ((lists_used >> list) & 1)
                    {
#define CLIP_ADD(s, x) (s) = (uint16_t)X265_MIN((s) + (x), (1 << 16) - 1)
                        int32_t listamount = propagate_amount;
                        /* Apply bipred weighting. */
                        if (lists_used == 3)
                            listamount = (listamount * bipredWeights[list] + 32) >> 6;

                        /* Early termination for simple case of mv0. */
                        if (!mvs[list][cuIndex].word)
                        {
                            CLIP_ADD(refCosts[list][cuIndex], listamount);
                            continue;
                        }

                        int32_t x = mvs[list][cuIndex].x;
                        int32_t y = mvs[list][cuIndex].y;
                        int32_t cux = (x >> 5) + blockx;
                        int32_t cuy = (y >> 5) + blocky;
                        int32_t idx0 = cux + cuy * StrideInCU;
                        int32_t idx1 = idx0 + 1;
                        int32_t idx2 = idx0 + StrideInCU;
                        int32_t idx3 = idx0 + StrideInCU + 1;
                        x &= 31;
                        y &= 31;
                        int32_t idx0weight = (32 - y) * (32 - x);
                        int32_t idx1weight = (32 - y) * x;
                        int32_t idx2weight = y * (32 - x);
                        int32_t idx3weight = y * x;

                        /* We could just clip the MVs, but pixels that lie outside the frame probably shouldn't
                         * be counted. */
                        if (cux < m_widthInCU - 1 && cuy < m_heightInCU - 1 && cux >= 0 && cuy >= 0)
                        {
                            CLIP_ADD(refCosts[list][idx0], (listamount * idx0weight + 512) >> 10);
                            CLIP_ADD(refCosts[list][idx1], (listamount * idx1weight + 512) >> 10);
                            CLIP_ADD(refCosts[list][idx2], (listamount * idx2weight + 512) >> 10);
                            CLIP_ADD(refCosts[list][idx3], (listamount * idx3weight + 512) >> 10);
                        }
                        else /* Check offsets individually */
                        {
                            if (cux < m_widthInCU && cuy < m_heightInCU && cux >= 0 && cuy >= 0)
                                CLIP_ADD(refCosts[list][idx0], (listamount * idx0weight + 512) >> 10);
                            if (cux + 1 < m_widthInCU && cuy < m_heightInCU && cux + 1 >= 0 && cuy >= 0)
                                CLIP_ADD(refCosts[list][idx1], (listamount * idx1weight + 512) >> 10);
                            if (cux < m_widthInCU && cuy + 1 < m_heightInCU && cux >= 0 && cuy + 1 >= 0)
                                CLIP_ADD(refCosts[list][idx2], (listamount * idx2weight + 512) >> 10);
                            if (cux + 1 < m_widthInCU && cuy + 1 < m_heightInCU && cux + 1 >= 0 && cuy + 1 >= 0)
                                CLIP_ADD(refCosts[list][idx3], (listamount * idx3weight + 512) >> 10);
                        }
                    }
                }
            }
        }
    }

    if (m_param->rc.vbvBufferSize && m_param->lookaheadDepth && referenced)
        cuTreeFinish(frames[b], averageDuration, b == p1 ? b - p0 : 0);
}

void Lookahead::cuTreeFinish(Lowres *frame, double averageDuration, int ref0Distance)
{
    int fpsFactor = (int)(CLIP_DURATION(averageDuration) / CLIP_DURATION((double)m_param->fpsDenom / m_param->fpsNum) * 256);
    double weightdelta = 0.0;

    if (ref0Distance && frame->weightedCostDelta[ref0Distance - 1] > 0)
        weightdelta = (1.0 - frame->weightedCostDelta[ref0Distance - 1]);

    /* Allow the strength to be adjusted via qcompress, since the two concepts
     * are very similar. */

    int cuCount = m_widthInCU * m_heightInCU;
    double strength = 5.0 * (1.0 - m_param->rc.qCompress);

    for (int cuIndex = 0; cuIndex < cuCount; cuIndex++)
    {
        int intracost = (frame->intraCost[cuIndex] * frame->invQscaleFactor[cuIndex] + 128) >> 8;
        if (intracost)
        {
            int propagateCost = (frame->propagateCost[cuIndex] * fpsFactor + 128) >> 8;
            double log2_ratio = X265_LOG2(intracost + propagateCost) - X265_LOG2(intracost) + weightdelta;
            frame->qpCuTreeOffset[cuIndex] = frame->qpAqOffset[cuIndex] - strength * log2_ratio;
        }
    }
}

/* If MB-tree changes the quantizers, we need to recalculate the frame cost without
 * re-running lookahead. */
int64_t Lookahead::frameCostRecalculate(Lowres** frames, int p0, int p1, int b)
{
    int64_t score = 0;
    int *rowSatd = frames[b]->rowSatds[b - p0][p1 - b];
    double *qp_offset = (frames[b]->sliceType == X265_TYPE_B) ? frames[b]->qpAqOffset : frames[b]->qpCuTreeOffset;

    x265_emms();
    for (int cuy = m_heightInCU - 1; cuy >= 0; cuy--)
    {
        rowSatd[cuy] = 0;
        for (int cux = m_widthInCU - 1; cux >= 0; cux--)
        {
            int cuxy = cux + cuy * m_widthInCU;
            int cuCost = frames[b]->lowresCosts[b - p0][p1 - b][cuxy] & LOWRES_COST_MASK;
            double qp_adj = qp_offset[cuxy];
            cuCost = (cuCost * x265_exp2fix8(qp_adj) + 128) >> 8;
            rowSatd[cuy] += cuCost;
            if ((cuy > 0 && cuy < m_heightInCU - 1 &&
                 cux > 0 && cux < m_widthInCU - 1) ||
                m_widthInCU <= 2 || m_heightInCU <= 2)
            {
                score += cuCost;
            }
        }
    }

    return score;
}


int64_t CostEstimateGroup::singleCost(int p0, int p1, int b, bool intraPenalty)
{
    LookaheadTLD& tld = m_lookahead.m_tld[m_lookahead.m_pool ? m_lookahead.m_pool->m_numWorkers : 0];
    return estimateFrameCost(tld, p0, p1, b, intraPenalty);
}

void CostEstimateGroup::add(int p0, int p1, int b, bool intraPenalty)
{
    X265_CHECK(m_batchMode || !m_jobTotal, "single CostEstimateGroup instance cannot mix batch modes\n");
    m_batchMode = true;

    Estimate& e = m_estimates[m_jobTotal++];
    e.p0 = p0;
    e.p1 = p1;
    e.b = b;
    e.bIntraPenalty = intraPenalty;

    if (m_jobTotal == MAX_BATCH_SIZE)
        finishBatch();
}

void CostEstimateGroup::finishBatch()
{
    if (m_lookahead.m_pool)
        tryBondPeers(*m_lookahead.m_pool, m_jobTotal);
    processTasks(-1);
    waitForExit();
    m_jobTotal = m_jobAcquired = 0;
}

void CostEstimateGroup::processTasks(int workerThreadID)
{
    ThreadPool* pool = m_lookahead.m_pool;
    int id = workerThreadID;
    if (workerThreadID < 0)
        id = pool ? pool->m_numWorkers : 0;
    LookaheadTLD& tld = m_lookahead.m_tld[id];

    m_lock.acquire();
    while (m_jobAcquired < m_jobTotal)
    {
        int i = m_jobAcquired++;
        m_lock.release();

        if (m_batchMode)
        {
#if DETAILED_CU_STATS
            ScopedElapsedTime filterPerfScope(tld.batchElapsedTime);
            tld.countBatches++;
#endif
            ProfileScopeEvent(estCostSingle);
            Estimate& e = m_estimates[i];

            estimateFrameCost(tld, e.p0, e.p1, e.b, e.bIntraPenalty);
        }
        else
        {
#if DETAILED_CU_STATS
            ScopedElapsedTime filterPerfScope(tld.coopSliceElapsedTime);
            tld.countCoopSlices++;
#endif
            ProfileScopeEvent(estCostCoop);
            X265_CHECK(i < MAX_COOP_SLICES, "impossible number of coop slices\n");

            int firstY = m_lookahead.m_numRowsPerSlice * i;
            int lastY = (i == m_jobTotal - 1) ? m_lookahead.m_heightInCU - 1 : m_lookahead.m_numRowsPerSlice * (i + 1) - 1;

            bool lastRow = true;
            for (int cuY = lastY; cuY >= firstY; cuY--)
            {
                m_frames[m_coop.b]->rowSatds[m_coop.b - m_coop.p0][m_coop.p1 - m_coop.b][cuY] = 0;

                for (int cuX = m_lookahead.m_widthInCU - 1; cuX >= 0; cuX--)
                    estimateCUCost(tld, cuX, cuY, m_coop.p0, m_coop.p1, m_coop.b, m_coop.bDoSearch, lastRow, i);

                lastRow = false;
            }
        }

        m_lock.acquire();
    }
    m_lock.release();
}

int64_t CostEstimateGroup::estimateFrameCost(LookaheadTLD& tld, int p0, int p1, int b, bool bIntraPenalty)
{
    Lowres*     fenc  = m_frames[b];
    x265_param* param = m_lookahead.m_param;
    int64_t     score = 0;

    if (fenc->costEst[b - p0][p1 - b] >= 0 && fenc->rowSatds[b - p0][p1 - b][0] != -1)
        score = fenc->costEst[b - p0][p1 - b];
    else
    {
        X265_CHECK(p0 != b, "I frame estimates should always be pre-calculated\n");

        bool bDoSearch[2];
        bDoSearch[0] = p0 < b && fenc->lowresMvs[0][b - p0 - 1][0].x == 0x7FFF;
        bDoSearch[1] = p1 > b && fenc->lowresMvs[1][p1 - b - 1][0].x == 0x7FFF;

        tld.weightedRef.isWeighted = false;
        if (param->bEnableWeightedPred && bDoSearch[0])
            tld.weightsAnalyse(*m_frames[b], *m_frames[p0]);

        fenc->costEst[b - p0][p1 - b] = 0;
        fenc->costEstAq[b - p0][p1 - b] = 0;

        ThreadPool* pool = m_lookahead.m_pool;
        if (!m_batchMode && pool && pool->m_numWorkers > 2 && ((p1 > b) || bDoSearch[0] || bDoSearch[1]))
        {
            /* Use cooperative mode if a thread pool is available and the cost estimate is
             * going to need motion searches or bidir measurements */

            memset(&m_slice, 0, sizeof(Slice) * m_lookahead.m_numCoopSlices);

            m_lock.acquire();
            X265_CHECK(!m_batchMode, "single CostEstimateGroup instance cannot mix batch modes\n");
            m_coop.p0 = p0;
            m_coop.p1 = p1;
            m_coop.b = b;
            m_coop.bDoSearch[0] = bDoSearch[0];
            m_coop.bDoSearch[1] = bDoSearch[1];
            m_jobTotal = m_lookahead.m_numCoopSlices;
            m_jobAcquired = 0;
            m_lock.release();

            tryBondPeers(*m_lookahead.m_pool, m_jobTotal);

            processTasks(-1);

            waitForExit();

            for (int i = 0; i < m_lookahead.m_numCoopSlices; i++)
            {
                fenc->costEst[b - p0][p1 - b] += m_slice[i].costEst;
                fenc->costEstAq[b - p0][p1 - b] += m_slice[i].costEstAq;
                fenc->intraMbs[b - p0] += m_slice[i].intraMbs;
            }
        }
        else
        {
            bool lastRow = true;
            for (int cuY = m_lookahead.m_heightInCU - 1; cuY >= 0; cuY--)
            {
                fenc->rowSatds[b - p0][p1 - b][cuY] = 0;

                for (int cuX = m_lookahead.m_widthInCU - 1; cuX >= 0; cuX--)
                    estimateCUCost(tld, cuX, cuY, p0, p1, b, bDoSearch, lastRow, -1);

                lastRow = false;
            }
        }

        score = fenc->costEst[b - p0][p1 - b];

        if (b != p1)
            score = score * 100 / (130 + param->bFrameBias);

        fenc->costEst[b - p0][p1 - b] = score;
    }

    if (bIntraPenalty)
        // arbitrary penalty for I-blocks after B-frames
        score += score * fenc->intraMbs[b - p0] / (tld.ncu * 8);

    return score;
}

void CostEstimateGroup::estimateCUCost(LookaheadTLD& tld, int cuX, int cuY, int p0, int p1, int b, bool bDoSearch[2], bool lastRow, int slice)
{
    Lowres *fref0 = m_frames[p0];
    Lowres *fref1 = m_frames[p1];
    Lowres *fenc  = m_frames[b];

    ReferencePlanes *wfref0 = tld.weightedRef.isWeighted ? &tld.weightedRef : fref0;

    const int widthInCU = m_lookahead.m_widthInCU;
    const int heightInCU = m_lookahead.m_heightInCU;
    const int bBidir = (b < p1);
    const int cuXY = cuX + cuY * widthInCU;
    const int cuSize = X265_LOWRES_CU_SIZE;
    const intptr_t pelOffset = cuSize * cuX + cuSize * cuY * fenc->lumaStride;

    if (bBidir || bDoSearch[0] || bDoSearch[1])
        tld.me.setSourcePU(fenc->lowresPlane[0], fenc->lumaStride, pelOffset, cuSize, cuSize);

    /* A small, arbitrary bias to avoid VBV problems caused by zero-residual lookahead blocks. */
    int lowresPenalty = 4;

    MV(*fencMVs[2]) = { &fenc->lowresMvs[0][b - p0 - 1][cuXY],
                        &fenc->lowresMvs[1][p1 - b - 1][cuXY] };
    int(*fencCosts[2]) = { &fenc->lowresMvCosts[0][b - p0 - 1][cuXY],
                           &fenc->lowresMvCosts[1][p1 - b - 1][cuXY] };

    MV mvmin, mvmax;
    int bcost = tld.me.COST_MAX;
    int listused = 0;

    // establish search bounds that don't cross extended frame boundaries
    mvmin.x = (int16_t)(-cuX * cuSize - 8);
    mvmin.y = (int16_t)(-cuY * cuSize - 8);
    mvmax.x = (int16_t)((widthInCU - cuX - 1) * cuSize + 8);
    mvmax.y = (int16_t)((heightInCU - cuY - 1) * cuSize + 8);

    for (int i = 0; i < 1 + bBidir; i++)
    {
        if (!bDoSearch[i])
        {
            COPY2_IF_LT(bcost, *fencCosts[i], listused, i + 1);
            continue;
        }

        int numc = 0;
        MV mvc[4], mvp;
        MV *fencMV = fencMVs[i];

        /* Reverse-order MV prediction */
        mvc[0] = 0;
        mvc[2] = 0;
#define MVC(mv) mvc[numc++] = mv;
        if (cuX < widthInCU - 1)
            MVC(fencMV[1]);
        if (!lastRow)
        {
            MVC(fencMV[widthInCU]);
            if (cuX > 0)
                MVC(fencMV[widthInCU - 1]);
            if (cuX < widthInCU - 1)
                MVC(fencMV[widthInCU + 1]);
        }
#undef MVC
        if (numc <= 1)
            mvp = mvc[0];
        else
            median_mv(mvp, mvc[0], mvc[1], mvc[2]);

        *fencCosts[i] = tld.me.motionEstimate(i ? fref1 : wfref0, mvmin, mvmax, mvp, numc, mvc, s_merange, *fencMVs[i]);
        COPY2_IF_LT(bcost, *fencCosts[i], listused, i + 1);
    }

    if (bBidir) /* B, also consider bidir */
    {
        /* NOTE: the wfref0 (weightp) is not used for BIDIR */

        /* avg(l0-mv, l1-mv) candidate */
        ALIGN_VAR_32(pixel, subpelbuf0[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
        ALIGN_VAR_32(pixel, subpelbuf1[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
        intptr_t stride0 = X265_LOWRES_CU_SIZE, stride1 = X265_LOWRES_CU_SIZE;
        pixel *src0 = fref0->lowresMC(pelOffset, *fencMVs[0], subpelbuf0, stride0);
        pixel *src1 = fref1->lowresMC(pelOffset, *fencMVs[1], subpelbuf1, stride1);

        ALIGN_VAR_32(pixel, ref[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
        primitives.pu[LUMA_8x8].pixelavg_pp(ref, X265_LOWRES_CU_SIZE, src0, stride0, src1, stride1, 32);
        int bicost = tld.me.bufSATD(ref, X265_LOWRES_CU_SIZE);
        COPY2_IF_LT(bcost, bicost, listused, 3);

        /* coloc candidate */
        src0 = fref0->lowresPlane[0] + pelOffset;
        src1 = fref1->lowresPlane[0] + pelOffset;
        primitives.pu[LUMA_8x8].pixelavg_pp(ref, X265_LOWRES_CU_SIZE, src0, fref0->lumaStride, src1, fref1->lumaStride, 32);
        bicost = tld.me.bufSATD(ref, X265_LOWRES_CU_SIZE);
        COPY2_IF_LT(bcost, bicost, listused, 3);

        bcost += lowresPenalty;
    }
    else /* P, also consider intra */
    {
        bcost += lowresPenalty;

        if (fenc->intraCost[cuXY] < bcost)
        {
            bcost = fenc->intraCost[cuXY];
            listused = 0;
        }
    }

    /* do not include edge blocks in the frame cost estimates, they are not very accurate */
    const bool bFrameScoreCU = (cuX > 0 && cuX < widthInCU - 1 &&
                                cuY > 0 && cuY < heightInCU - 1) || widthInCU <= 2 || heightInCU <= 2;

    int bcostAq = (bFrameScoreCU && fenc->invQscaleFactor) ? ((bcost * fenc->invQscaleFactor[cuXY] + 128) >> 8) : bcost;

    if (bFrameScoreCU)
    {
        if (slice < 0)
        {
            fenc->costEst[b - p0][p1 - b] += bcost;
            fenc->costEstAq[b - p0][p1 - b] += bcostAq;
            if (!listused)
                fenc->intraMbs[b - p0]++;
        }
        else
        {
            m_slice[slice].costEst += bcost;
            m_slice[slice].costEstAq += bcostAq;
            if (!listused)
                m_slice[slice].intraMbs++;
        }
    }

    fenc->rowSatds[b - p0][p1 - b][cuY] += bcostAq;
    fenc->lowresCosts[b - p0][p1 - b][cuXY] = (uint16_t)(X265_MIN(bcost, LOWRES_COST_MASK) | (listused << LOWRES_COST_SHIFT));
}
