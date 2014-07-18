/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2013, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     TComPrediction.cpp
    \brief    prediction class
*/

#include "TComPrediction.h"
#include "primitives.h"
#include "common.h"

using namespace x265;

//! \ingroup TLibCommon
//! \{

static const uint8_t intraFilterThreshold[5] =
{
    10, //4x4
    7,  //8x8
    1,  //16x16
    0,  //32x32
    10, //64x64
};

// ====================================================================================================================
// Constructor / destructor / initialize
// ====================================================================================================================

TComPrediction::TComPrediction()
{
    m_predBuf = NULL;
    m_refAbove = NULL;
    m_refAboveFlt = NULL;
    m_refLeft = NULL;
    m_refLeftFlt = NULL;
    m_immedVals = NULL;
}

TComPrediction::~TComPrediction()
{
    X265_FREE(m_predBuf);
    X265_FREE(m_refAbove);
    X265_FREE(m_refAboveFlt);
    X265_FREE(m_refLeft);
    X265_FREE(m_refLeftFlt);
    X265_FREE(m_immedVals);

    m_predYuv[0].destroy();
    m_predYuv[1].destroy();
    m_predShortYuv[0].destroy();
    m_predShortYuv[1].destroy();
    m_predTempYuv.destroy();
}

void TComPrediction::initTempBuff(int csp)
{
    m_hChromaShift = CHROMA_H_SHIFT(csp);
    m_vChromaShift = CHROMA_V_SHIFT(csp);

    if (m_predBuf == NULL)
    {
        int predBufHeight = ((MAX_CU_SIZE + 2) << 4);
        int predBufStride = ((MAX_CU_SIZE + 8) << 4);
        m_predBuf = X265_MALLOC(pixel, predBufStride * predBufHeight);

        m_refAbove = X265_MALLOC(pixel, 3 * MAX_CU_SIZE);
        m_refAboveFlt = X265_MALLOC(pixel, 3 * MAX_CU_SIZE);
        m_refLeft = X265_MALLOC(pixel, 3 * MAX_CU_SIZE);
        m_refLeftFlt = X265_MALLOC(pixel, 3 * MAX_CU_SIZE);

        m_predYuv[0].create(MAX_CU_SIZE, MAX_CU_SIZE, csp);
        m_predYuv[1].create(MAX_CU_SIZE, MAX_CU_SIZE, csp);
        m_predShortYuv[0].create(MAX_CU_SIZE, MAX_CU_SIZE, csp);
        m_predShortYuv[1].create(MAX_CU_SIZE, MAX_CU_SIZE, csp);
        m_predTempYuv.create(MAX_CU_SIZE, MAX_CU_SIZE, csp);

        m_immedVals = X265_MALLOC(int16_t, 64 * (64 + NTAPS_LUMA - 1));
    }
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

bool TComPrediction::filteringIntraReferenceSamples(uint32_t dirMode, uint32_t log2TrSize)
{
    bool bFilter;

    if (dirMode == DC_IDX || log2TrSize <= 2)
    {
        bFilter = false; // no smoothing for DC
    }
    else
    {
        int diff = std::min<int>(abs((int)dirMode - HOR_IDX), abs((int)dirMode - VER_IDX));
        uint32_t sizeIdx = log2TrSize - 2;
        bFilter = diff > intraFilterThreshold[sizeIdx];
    }

    return bFilter;
}

void TComPrediction::predIntraLumaAng(uint32_t dirMode, pixel* dst, intptr_t stride, uint32_t log2TrSize)
{
    int tuSize = 1 << log2TrSize;
    bool bUseFilteredPredictions = filteringIntraReferenceSamples(dirMode, log2TrSize);

    pixel *refLft, *refAbv;
    refLft = m_refLeft + tuSize - 1;
    refAbv = m_refAbove + tuSize - 1;

    if (bUseFilteredPredictions)
    {
        refLft = m_refLeftFlt + tuSize - 1;
        refAbv = m_refAboveFlt + tuSize - 1;
    }

    bool bFilter = log2TrSize <= 4 && dirMode != PLANAR_IDX;
    int sizeIdx = log2TrSize - 2;
    X265_CHECK(sizeIdx >= 0 && sizeIdx < 4, "intra block size is out of range\n");
    primitives.intra_pred[sizeIdx][dirMode](dst, stride, refLft, refAbv, dirMode, bFilter);
}

// Angular chroma
void TComPrediction::predIntraChromaAng(pixel* src, uint32_t dirMode, pixel* dst, intptr_t stride, uint32_t log2TrSizeC, int chFmt)
{
    int tuSize = 1 << log2TrSizeC;
    uint32_t tuSize2 = tuSize << 1;

    // Create the prediction
    pixel refAbv[3 * MAX_CU_SIZE];
    pixel refLft[3 * MAX_CU_SIZE];

    bool bUseFilteredPredictions = (chFmt == CHROMA_444 && filteringIntraReferenceSamples(dirMode, log2TrSizeC));

    if (bUseFilteredPredictions)
    {
        // generate filtered intra prediction samples
        // left and left above border + above and above right border + top left corner = length of 3. filter buffer
        int bufSize = tuSize2 + tuSize2 + 1;
        uint32_t wh = ADI_BUF_STRIDE * (tuSize2 + 1);         // number of elements in one buffer

        pixel* filterBuf  = src + wh;            // buffer for 2. filtering (sequential)
        pixel* filterBufN = filterBuf + bufSize; // buffer for 1. filtering (sequential)

        int l = 0;
        // left border from bottom to top
        for (int i = 0; i < tuSize2; i++)
        {
            filterBuf[l++] = src[ADI_BUF_STRIDE * (tuSize2 - i)];
        }

        // top left corner
        filterBuf[l++] = src[0];

        // above border from left to right
        memcpy(&filterBuf[l], &src[1], tuSize2 * sizeof(*filterBuf));

        // 1. filtering with [1 2 1]
        filterBufN[0] = filterBuf[0];
        filterBufN[bufSize - 1] = filterBuf[bufSize - 1];
        for (int i = 1; i < bufSize - 1; i++)
        {
            filterBufN[i] = (filterBuf[i - 1] + 2 * filterBuf[i] + filterBuf[i + 1] + 2) >> 2;
        }

        // initialization of ADI buffers
        int limit = tuSize2 + 1;
        memcpy(refAbv + tuSize - 1, filterBufN + tuSize2, limit * sizeof(pixel));
        for (int k = 0; k < limit; k++)
        {
            refLft[k + tuSize - 1] = filterBufN[tuSize2 - k];   // Smoothened
        }
    }
    else
    {
        int limit = (dirMode <= 25 && dirMode >= 11) ? (tuSize + 1 + 1) : (tuSize2 + 1);
        memcpy(refAbv + tuSize - 1, src, (limit) * sizeof(pixel));
        for (int k = 0; k < limit; k++)
        {
            refLft[k + tuSize - 1] = src[k * ADI_BUF_STRIDE];
        }
    }

    int sizeIdx = log2TrSizeC - 2;
    X265_CHECK(sizeIdx >= 0 && sizeIdx < 4, "intra block size is out of range\n");
    primitives.intra_pred[sizeIdx][dirMode](dst, stride, refLft + tuSize - 1, refAbv + tuSize - 1, dirMode, 0);
}

/** Function for checking identical motion.
 * \param TComDataCU* cu
 * \param uint32_t PartAddr
 */
bool TComPrediction::xCheckIdenticalMotion(TComDataCU* cu, uint32_t partAddr)
{
    X265_CHECK(cu->getSlice()->isInterB(), "identical motion check in P frame\n");
    if (!cu->getSlice()->m_pps->bUseWeightedBiPred)
    {
        int refIdxL0 = cu->getCUMvField(0)->getRefIdx(partAddr);
        int refIdxL1 = cu->getCUMvField(1)->getRefIdx(partAddr);
        if (refIdxL0 >= 0 && refIdxL1 >= 0)
        {
            int refPOCL0 = cu->getSlice()->m_refPOCList[0][refIdxL0];
            int refPOCL1 = cu->getSlice()->m_refPOCList[1][refIdxL1];
            if (refPOCL0 == refPOCL1 && cu->getCUMvField(0)->getMv(partAddr) == cu->getCUMvField(1)->getMv(partAddr))
                return true;
        }
    }
    return false;
}

void TComPrediction::motionCompensation(TComDataCU* cu, TComYuv* predYuv, int list, int partIdx, bool bLuma, bool bChroma)
{
    int  width;
    int  height;
    uint32_t partAddr;

    X265_CHECK(partIdx >= 0, "partidx is not positive\n");
    {
        cu->getPartIndexAndSize(partIdx, partAddr, width, height);
        if (cu->getSlice()->isInterP())
            list = REF_PIC_LIST_0;
        if (list != REF_PIC_LIST_X)
        {
            if (cu->getSlice()->m_pps->bUseWeightPred)
            {
                ShortYuv* shortYuv = &m_predShortYuv[0];
                int refId = cu->getCUMvField(list)->getRefIdx(partAddr);
                X265_CHECK(refId >= 0, "refidx is not positive\n");

                MV mv = cu->getCUMvField(list)->getMv(partAddr);
                cu->clipMv(mv);
                if (bLuma)
                    xPredInterLumaBlk(cu, cu->getSlice()->m_refPicList[list][refId]->getPicYuvRec(), partAddr, &mv, width, height, shortYuv);
                if (bChroma)
                    xPredInterChromaBlk(cu, cu->getSlice()->m_refPicList[list][refId]->getPicYuvRec(), partAddr, &mv, width, height, shortYuv);

                xWeightedPredictionUni(cu, shortYuv, partAddr, width, height, list, predYuv, -1, bLuma, bChroma);
            }
            else
                xPredInterUni(cu, partAddr, width, height, list, predYuv, bLuma, bChroma);
        }
        else
        {
            if (xCheckIdenticalMotion(cu, partAddr))
                xPredInterUni(cu, partAddr, width, height, REF_PIC_LIST_0, predYuv, bLuma, bChroma);
            else
                xPredInterBi(cu, partAddr, width, height, predYuv, bLuma, bChroma);
        }
    }
}

void TComPrediction::xPredInterUni(TComDataCU* cu, uint32_t partAddr, int width, int height, int list, TComYuv* outPredYuv, bool bLuma, bool bChroma)
{
    int refIdx = cu->getCUMvField(list)->getRefIdx(partAddr);

    X265_CHECK(refIdx >= 0, "refidx is not positive\n");

    MV mv = cu->getCUMvField(list)->getMv(partAddr);
    cu->clipMv(mv);

    if (bLuma)
        xPredInterLumaBlk(cu, cu->getSlice()->m_refPicList[list][refIdx]->getPicYuvRec(), partAddr, &mv, width, height, outPredYuv);

    if (bChroma)
        xPredInterChromaBlk(cu, cu->getSlice()->m_refPicList[list][refIdx]->getPicYuvRec(), partAddr, &mv, width, height, outPredYuv);
}

void TComPrediction::xPredInterUni(TComDataCU* cu, uint32_t partAddr, int width, int height, int list, ShortYuv* outPredYuv, bool bLuma, bool bChroma)
{
    int refIdx = cu->getCUMvField(list)->getRefIdx(partAddr);

    X265_CHECK(refIdx >= 0, "refidx is not positive\n");

    MV mv = cu->getCUMvField(list)->getMv(partAddr);
    cu->clipMv(mv);

    if (bLuma)
        xPredInterLumaBlk(cu, cu->getSlice()->m_refPicList[list][refIdx]->getPicYuvRec(), partAddr, &mv, width, height, outPredYuv);
    if (bChroma)
        xPredInterChromaBlk(cu, cu->getSlice()->m_refPicList[list][refIdx]->getPicYuvRec(), partAddr, &mv, width, height, outPredYuv);
}

void TComPrediction::xPredInterBi(TComDataCU* cu, uint32_t partAddr, int width, int height, TComYuv* outPredYuv, bool bLuma, bool bChroma)
{
    X265_CHECK(cu->getSlice()->isInterB(), "biprediction in P frame\n");

    int refIdx[2];
    refIdx[0] = cu->getCUMvField(REF_PIC_LIST_0)->getRefIdx(partAddr);
    refIdx[1] = cu->getCUMvField(REF_PIC_LIST_1)->getRefIdx(partAddr);

    if (refIdx[0] >= 0 && refIdx[1] >= 0)
    {
        for (int list = 0; list < 2; list++)
        {
            X265_CHECK(refIdx[list] < cu->getSlice()->m_numRefIdx[list], "refidx out of range\n");

            xPredInterUni(cu, partAddr, width, height, list, &m_predShortYuv[list], bLuma, bChroma);
        }

        if (cu->getSlice()->m_pps->bUseWeightedBiPred)
            xWeightedPredictionBi(cu, &m_predShortYuv[0], &m_predShortYuv[1], refIdx[0], refIdx[1], partAddr, width, height, outPredYuv, bLuma, bChroma);
        else
            outPredYuv->addAvg(&m_predShortYuv[0], &m_predShortYuv[1], partAddr, width, height, bLuma, bChroma);
    }
    else if (cu->getSlice()->m_pps->bUseWeightedBiPred)
    {
        for (int list = 0; list < 2; list++)
        {
            if (refIdx[list] < 0) continue;

            X265_CHECK(refIdx[list] < cu->getSlice()->m_numRefIdx[list], "refidx out of range\n");

            xPredInterUni(cu, partAddr, width, height, list, &m_predShortYuv[list], bLuma, bChroma);
        }

        xWeightedPredictionBi(cu, &m_predShortYuv[0], &m_predShortYuv[1], refIdx[0], refIdx[1], partAddr, width, height, outPredYuv, bLuma, bChroma);
    }
    else if (refIdx[0] >= 0)
    {
        const int list = 0;

        X265_CHECK(refIdx[list] < cu->getSlice()->m_numRefIdx[list], "refidx out of range\n");

        xPredInterUni(cu, partAddr, width, height, list, outPredYuv, bLuma, bChroma);
    }
    else
    {
        X265_CHECK(refIdx[1] >= 0, "refidx[1] was not positive\n");

        const int list = 1;

        X265_CHECK(refIdx[list] < cu->getSlice()->m_numRefIdx[list], "refidx out of range\n");

        xPredInterUni(cu, partAddr, width, height, list, outPredYuv, bLuma, bChroma);
    }
}

/**
 * \brief Generate motion-compensated luma block
 *
 * \param cu       Pointer to current CU
 * \param refPic   Pointer to reference picture
 * \param partAddr Address of block within CU
 * \param mv       Motion vector
 * \param width    Width of block
 * \param height   Height of block
 * \param dstPic   Pointer to destination picture
 */
void TComPrediction::xPredInterLumaBlk(TComDataCU *cu, TComPicYuv *refPic, uint32_t partAddr, MV *mv, int width, int height, TComYuv *dstPic)
{
    int dstStride = dstPic->getStride();
    pixel *dst    = dstPic->getLumaAddr(partAddr);

    int srcStride = refPic->getStride();
    int srcOffset = (mv->x >> 2) + (mv->y >> 2) * srcStride;
    int partEnum = partitionFromSizes(width, height);
    pixel* src = refPic->getLumaAddr(cu->getAddr(), cu->getZorderIdxInCU() + partAddr) + srcOffset;

    int xFrac = mv->x & 0x3;
    int yFrac = mv->y & 0x3;

    if ((yFrac | xFrac) == 0)
    {
        primitives.luma_copy_pp[partEnum](dst, dstStride, src, srcStride);
    }
    else if (yFrac == 0)
    {
        primitives.luma_hpp[partEnum](src, srcStride, dst, dstStride, xFrac);
    }
    else if (xFrac == 0)
    {
        primitives.luma_vpp[partEnum](src, srcStride, dst, dstStride, yFrac);
    }
    else
    {
        int tmpStride = width;
        int filterSize = NTAPS_LUMA;
        int halfFilterSize = (filterSize >> 1);
        primitives.luma_hps[partEnum](src, srcStride, m_immedVals, tmpStride, xFrac, 1);
        primitives.luma_vsp[partEnum](m_immedVals + (halfFilterSize - 1) * tmpStride, tmpStride, dst, dstStride, yFrac);
    }
}

//Motion compensated block for biprediction
void TComPrediction::xPredInterLumaBlk(TComDataCU *cu, TComPicYuv *refPic, uint32_t partAddr, MV *mv, int width, int height, ShortYuv *dstPic)
{
    int refStride = refPic->getStride();
    int refOffset = (mv->x >> 2) + (mv->y >> 2) * refStride;
    pixel *ref    = refPic->getLumaAddr(cu->getAddr(), cu->getZorderIdxInCU() + partAddr) + refOffset;

    int dstStride = dstPic->m_width;
    int16_t *dst  = dstPic->getLumaAddr(partAddr);

    int xFrac = mv->x & 0x3;
    int yFrac = mv->y & 0x3;

    int partEnum = partitionFromSizes(width, height);

    X265_CHECK((width % 4) + (height % 4) == 0, "width or height not divisible by 4\n");
    X265_CHECK(dstStride == MAX_CU_SIZE, "stride expected to be max cu size\n");

    if ((yFrac | xFrac) == 0)
    {
        primitives.luma_p2s(ref, refStride, dst, width, height);
    }
    else if (yFrac == 0)
    {
        primitives.luma_hps[partEnum](ref, refStride, dst, dstStride, xFrac, 0);
    }
    else if (xFrac == 0)
    {
        primitives.luma_vps[partEnum](ref, refStride, dst, dstStride, yFrac);
    }
    else
    {
        int tmpStride = width;
        int filterSize = NTAPS_LUMA;
        int halfFilterSize = (filterSize >> 1);
        primitives.luma_hps[partEnum](ref, refStride, m_immedVals, tmpStride, xFrac, 1);
        primitives.luma_vss[partEnum](m_immedVals + (halfFilterSize - 1) * tmpStride, tmpStride, dst, dstStride, yFrac);
    }
}

/**
 * \brief Generate motion-compensated chroma block
 *
 * \param cu       Pointer to current CU
 * \param refPic   Pointer to reference picture
 * \param partAddr Address of block within CU
 * \param mv       Motion vector
 * \param width    Width of block
 * \param height   Height of block
 * \param dstPic   Pointer to destination picture
 */
void TComPrediction::xPredInterChromaBlk(TComDataCU *cu, TComPicYuv *refPic, uint32_t partAddr, MV *mv, int width, int height, TComYuv *dstPic)
{
    int refStride = refPic->getCStride();
    int dstStride = dstPic->getCStride();

    int shiftHor = (2 + cu->getHorzChromaShift());
    int shiftVer = (2 + cu->getVertChromaShift());

    int refOffset = (mv->x >> shiftHor) + (mv->y >> shiftVer) * refStride;

    pixel* refCb = refPic->getCbAddr(cu->getAddr(), cu->getZorderIdxInCU() + partAddr) + refOffset;
    pixel* refCr = refPic->getCrAddr(cu->getAddr(), cu->getZorderIdxInCU() + partAddr) + refOffset;

    pixel* dstCb = dstPic->getCbAddr(partAddr);
    pixel* dstCr = dstPic->getCrAddr(partAddr);

    int xFrac = mv->x & ((1 << shiftHor) - 1);
    int yFrac = mv->y & ((1 << shiftVer) - 1);

    int partEnum = partitionFromSizes(width, height);
    int csp = cu->getChromaFormat();

    if ((yFrac | xFrac) == 0)
    {
        primitives.chroma[csp].copy_pp[partEnum](dstCb, dstStride, refCb, refStride);
        primitives.chroma[csp].copy_pp[partEnum](dstCr, dstStride, refCr, refStride);
    }
    else if (yFrac == 0)
    {
        primitives.chroma[csp].filter_hpp[partEnum](refCb, refStride, dstCb, dstStride, xFrac << (1 - cu->getHorzChromaShift()));
        primitives.chroma[csp].filter_hpp[partEnum](refCr, refStride, dstCr, dstStride, xFrac << (1 - cu->getHorzChromaShift()));
    }
    else if (xFrac == 0)
    {
        primitives.chroma[csp].filter_vpp[partEnum](refCb, refStride, dstCb, dstStride, yFrac << (1 - cu->getVertChromaShift()));
        primitives.chroma[csp].filter_vpp[partEnum](refCr, refStride, dstCr, dstStride, yFrac << (1 - cu->getVertChromaShift()));
    }
    else
    {
        int extStride = width >> m_hChromaShift;
        int filterSize = NTAPS_CHROMA;
        int halfFilterSize = (filterSize >> 1);

        primitives.chroma[csp].filter_hps[partEnum](refCb, refStride, m_immedVals, extStride, xFrac << (1 - cu->getHorzChromaShift()), 1);
        primitives.chroma[csp].filter_vsp[partEnum](m_immedVals + (halfFilterSize - 1) * extStride, extStride, dstCb, dstStride, yFrac << (1 - cu->getVertChromaShift()));

        primitives.chroma[csp].filter_hps[partEnum](refCr, refStride, m_immedVals, extStride, xFrac << (1 - cu->getHorzChromaShift()), 1);
        primitives.chroma[csp].filter_vsp[partEnum](m_immedVals + (halfFilterSize - 1) * extStride, extStride, dstCr, dstStride, yFrac << (1 - cu->getVertChromaShift()));
    }
}

// Generate motion compensated block when biprediction
void TComPrediction::xPredInterChromaBlk(TComDataCU *cu, TComPicYuv *refPic, uint32_t partAddr, MV *mv, int width, int height, ShortYuv *dstPic)
{
    int refStride = refPic->getCStride();
    int dstStride = dstPic->m_cwidth;

    int shiftHor = (2 + cu->getHorzChromaShift());
    int shiftVer = (2 + cu->getVertChromaShift());

    int refOffset = (mv->x >> shiftHor) + (mv->y >> shiftVer) * refStride;

    pixel* refCb = refPic->getCbAddr(cu->getAddr(), cu->getZorderIdxInCU() + partAddr) + refOffset;
    pixel* refCr = refPic->getCrAddr(cu->getAddr(), cu->getZorderIdxInCU() + partAddr) + refOffset;

    int16_t* dstCb = dstPic->getCbAddr(partAddr);
    int16_t* dstCr = dstPic->getCrAddr(partAddr);

    int xFrac = mv->x & ((1 << shiftHor) - 1);
    int yFrac = mv->y & ((1 << shiftVer) - 1);

    int partEnum = partitionFromSizes(width, height);
    int csp = cu->getChromaFormat();

    uint32_t cxWidth = width   >> m_hChromaShift;
    uint32_t cxHeight = height >> m_vChromaShift;

    X265_CHECK(((cxWidth | cxHeight) % 2) == 0, "chroma block size expected to be multiple of 2\n");

    if ((yFrac | xFrac) == 0)
    {
        primitives.chroma_p2s[csp](refCb, refStride, dstCb, cxWidth, cxHeight);
        primitives.chroma_p2s[csp](refCr, refStride, dstCr, cxWidth, cxHeight);
    }
    else if (yFrac == 0)
    {
        primitives.chroma[csp].filter_hps[partEnum](refCb, refStride, dstCb, dstStride, xFrac << (1 - cu->getHorzChromaShift()), 0);
        primitives.chroma[csp].filter_hps[partEnum](refCr, refStride, dstCr, dstStride, xFrac << (1 - cu->getHorzChromaShift()), 0);
    }
    else if (xFrac == 0)
    {
        primitives.chroma[csp].filter_vps[partEnum](refCb, refStride, dstCb, dstStride, yFrac << (1 - cu->getVertChromaShift()));
        primitives.chroma[csp].filter_vps[partEnum](refCr, refStride, dstCr, dstStride, yFrac << (1 - cu->getVertChromaShift()));
    }
    else
    {
        int extStride = cxWidth;
        int filterSize = NTAPS_CHROMA;
        int halfFilterSize = (filterSize >> 1);
        primitives.chroma[csp].filter_hps[partEnum](refCb, refStride, m_immedVals, extStride, xFrac << (1 - cu->getHorzChromaShift()), 1);
        primitives.chroma[csp].filter_vss[partEnum](m_immedVals + (halfFilterSize - 1) * extStride, extStride, dstCb, dstStride, yFrac << (1 - cu->getVertChromaShift()));
        primitives.chroma[csp].filter_hps[partEnum](refCr, refStride, m_immedVals, extStride, xFrac << (1 - cu->getHorzChromaShift()), 1);
        primitives.chroma[csp].filter_vss[partEnum](m_immedVals + (halfFilterSize - 1) * extStride, extStride, dstCr, dstStride, yFrac << (1 - cu->getVertChromaShift()));
    }
}

//! \}
