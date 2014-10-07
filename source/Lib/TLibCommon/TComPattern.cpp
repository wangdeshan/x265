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

/** \file     TComPattern.cpp
    \brief    neighboring pixel access classes
*/

#include "frame.h"
#include "TComPattern.h"
#include "TComDataCU.h"
#include "TComRom.h"

using namespace x265;

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Public member functions (TComPattern)
// ====================================================================================================================

void TComPattern::initAdiPattern(const TComDataCU& cu, const CU& cuData, uint32_t zOrderIdxInPart, uint32_t partDepth, pixel* adiBuf,
                                 pixel* refAbove, pixel* refLeft, pixel* refAboveFlt, pixel* refLeftFlt, int dirMode)
{
    IntraNeighbors intraNeighbors;

    initIntraNeighbors(cu, zOrderIdxInPart, partDepth, true, &intraNeighbors);
    uint32_t tuSize = intraNeighbors.tuSize;
    uint32_t tuSize2 = tuSize << 1;

    pixel* adiOrigin = const_cast<Frame*>(cu.m_pic)->getPicYuvRec()->getLumaAddr(cu.m_cuAddr, cuData.encodeIdx + zOrderIdxInPart);
    int picStride = cu.m_pic->m_origPicYuv->m_stride;

    fillReferenceSamples(adiOrigin, picStride, adiBuf, intraNeighbors);

    // initialization of ADI buffers
    const int bufOffset = tuSize - 1;
    refAbove += bufOffset;
    refLeft += bufOffset;

    //  ADI_BUF_STRIDE * (2 * tuSize + 1);
    memcpy(refAbove, adiBuf, (tuSize2 + 1) * sizeof(pixel));
    for (int k = 0; k < tuSize2 + 1; k++)
        refLeft[k] = adiBuf[k * ADI_BUF_STRIDE];
    
    bool bUseFilteredPredictions = (dirMode == ALL_IDX ? (8 | 16 | 32) & tuSize : g_intraFilterFlags[dirMode] & tuSize);

    if (bUseFilteredPredictions)
    {
        // generate filtered intra prediction samples
        refAboveFlt += bufOffset;
        refLeftFlt += bufOffset;

        bool bStrongSmoothing = (tuSize == 32 && cu.m_slice->m_sps->bUseStrongIntraSmoothing);

        if (bStrongSmoothing)
        {
            const int trSize  = 32;
            const int trSize2 = 32 * 2;
            const int threshold = 1 << (X265_DEPTH - 5);
            int refBL = refLeft[trSize2];
            int refTL = refAbove[0];
            int refTR = refAbove[trSize2];
            bStrongSmoothing = (abs(refBL + refTL - 2 * refLeft[trSize])  < threshold &&
                                abs(refTL + refTR - 2 * refAbove[trSize]) < threshold);

            if (bStrongSmoothing)
            {
                // bilinear interpolation
                const int shift = 5 + 1; // intraNeighbors.log2TrSize + 1;
                int init = (refTL << shift) + tuSize;
                int delta;

                refLeftFlt[0] = refAboveFlt[0] = refAbove[0];

                //TODO: Performance Primitive???
                delta = refBL - refTL;
                for (int i = 1; i < trSize2; i++)
                    refLeftFlt[i] = (init + delta * i) >> shift;
                refLeftFlt[trSize2] = refLeft[trSize2];

                delta = refTR - refTL;
                for (int i = 1; i < trSize2; i++)
                    refAboveFlt[i] = (init + delta * i) >> shift;
                refAboveFlt[trSize2] = refAbove[trSize2];

                return;
            }
        }

        refLeft[-1] = refAbove[1];
        for (int i = 0; i < tuSize2; i++)
            refLeftFlt[i] = (refLeft[i - 1] + 2 * refLeft[i] + refLeft[i + 1] + 2) >> 2;
        refLeftFlt[tuSize2] = refLeft[tuSize2];

        refAboveFlt[0] = refLeftFlt[0];
        for (int i = 1; i < tuSize2; i++)
            refAboveFlt[i] = (refAbove[i - 1] + 2 * refAbove[i] + refAbove[i + 1] + 2) >> 2;
        refAboveFlt[tuSize2] = refAbove[tuSize2];
    }
}

void TComPattern::initAdiPatternChroma(const TComDataCU& cu, const CU& cuData, uint32_t zOrderIdxInPart, uint32_t partDepth, pixel* adiBuf, uint32_t chromaId)
{
    IntraNeighbors intraNeighbors;

    initIntraNeighbors(cu, zOrderIdxInPart, partDepth, false, &intraNeighbors);
    uint32_t tuSize = intraNeighbors.tuSize;

    const pixel* adiOrigin = const_cast<Frame*>(cu.m_pic)->getPicYuvRec()->getChromaAddr(chromaId, cu.m_cuAddr, cuData.encodeIdx + zOrderIdxInPart);
    int picStride = cu.m_pic->m_origPicYuv->m_strideC;
    pixel* adiRef = getAdiChromaBuf(chromaId, tuSize, adiBuf);

    fillReferenceSamples(adiOrigin, picStride, adiRef, intraNeighbors);
}

void TComPattern::initIntraNeighbors(const TComDataCU& cu, uint32_t zOrderIdxInPart, uint32_t partDepth, bool isLuma, IntraNeighbors *intraNeighbors)
{
    uint32_t log2TrSize = cu.getLog2CUSize(0) - partDepth;
    int log2UnitWidth  = LOG2_UNIT_SIZE;
    int log2UnitHeight = LOG2_UNIT_SIZE;

    if (!isLuma)
    {
        log2TrSize     -= cu.m_hChromaShift;
        log2UnitWidth  -= cu.m_hChromaShift;
        log2UnitHeight -= cu.m_vChromaShift;
    }

    int   numIntraNeighbor = 0;
    bool *bNeighborFlags = intraNeighbors->bNeighborFlags;

    uint32_t partIdxLT, partIdxRT, partIdxLB;

    cu.deriveLeftRightTopIdxAdi(partIdxLT, partIdxRT, zOrderIdxInPart, partDepth);

    uint32_t tuSize  = 1 << log2TrSize;
    int  tuWidthInUnits  = tuSize >> log2UnitWidth;
    int  tuHeightInUnits = tuSize >> log2UnitHeight;
    int  aboveUnits      = tuWidthInUnits << 1;
    int  leftUnits       = tuHeightInUnits << 1;
    int  partIdxStride   = cu.m_pic->getNumPartInCUSize();
    partIdxLB            = g_rasterToZscan[g_zscanToRaster[partIdxLT] + ((tuHeightInUnits - 1) * partIdxStride)];

    bNeighborFlags[leftUnits] = isAboveLeftAvailable(cu, partIdxLT);
    numIntraNeighbor += (int)(bNeighborFlags[leftUnits]);
    numIntraNeighbor += isAboveAvailable(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1));
    numIntraNeighbor += isAboveRightAvailable(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1 + tuWidthInUnits));
    numIntraNeighbor += isLeftAvailable(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits - 1));
    numIntraNeighbor += isBelowLeftAvailable(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits   - 1 - tuHeightInUnits));

    intraNeighbors->numIntraNeighbor = numIntraNeighbor;
    intraNeighbors->totalUnits       = aboveUnits + leftUnits + 1;
    intraNeighbors->aboveUnits       = aboveUnits;
    intraNeighbors->leftUnits        = leftUnits;
    intraNeighbors->unitWidth        = 1 << log2UnitWidth;
    intraNeighbors->unitHeight       = 1 << log2UnitHeight;
    intraNeighbors->tuSize           = tuSize;
    intraNeighbors->log2TrSize       = log2TrSize;
}

void TComPattern::fillReferenceSamples(const pixel* adiOrigin, int picStride, pixel* adiRef, const IntraNeighbors& intraNeighbors)
{
    int numIntraNeighbor = intraNeighbors.numIntraNeighbor;
    int totalUnits       = intraNeighbors.totalUnits;
    uint32_t tuSize      = intraNeighbors.tuSize;

    uint32_t refSize = tuSize * 2 + 1;
    int  i, j;
    int  dcValue = 1 << (X265_DEPTH - 1);

    if (numIntraNeighbor == 0)
    {
        // Fill border with DC value
        for (i = 0; i < refSize; i++)
            adiRef[i] = dcValue;

        for (i = 1; i < refSize; i++)
            adiRef[i * ADI_BUF_STRIDE] = dcValue;
    }
    else if (numIntraNeighbor == totalUnits)
    {
        // Fill top border with rec. samples
        const pixel* adiTemp = adiOrigin - picStride - 1;
        memcpy(adiRef, adiTemp, refSize * sizeof(*adiRef));

        // Fill left border with rec. samples
        adiTemp = adiOrigin - 1;
        for (i = 1; i < refSize; i++)
        {
            adiRef[i * ADI_BUF_STRIDE] = adiTemp[0];
            adiTemp += picStride;
        }
    }
    else // reference samples are partially available
    {
        const bool *bNeighborFlags = intraNeighbors.bNeighborFlags;
        int aboveUnits       = intraNeighbors.aboveUnits;
        int leftUnits        = intraNeighbors.leftUnits;
        int unitWidth        = intraNeighbors.unitWidth;
        int unitHeight       = intraNeighbors.unitHeight;
        int  totalSamples = (leftUnits * unitHeight) + ((aboveUnits + 1) * unitWidth);
        pixel pAdiLine[5 * MAX_CU_SIZE];
        pixel *pAdiLineTemp;
        const bool  *pNeighborFlags;
        int   next, curr;

        // Initialize
        for (i = 0; i < totalSamples; i++)
        {
            pAdiLine[i] = dcValue;
        }

        // Fill top-left sample
        const pixel* adiTemp =  adiOrigin - picStride - 1;
        pAdiLineTemp = pAdiLine + (leftUnits * unitHeight);
        pNeighborFlags = bNeighborFlags + leftUnits;
        if (*pNeighborFlags)
        {
            pixel topLeftVal = adiTemp[0];
            for (i = 0; i < unitWidth; i++)
            {
                pAdiLineTemp[i] = topLeftVal;
            }
        }

        // Fill left & below-left samples
        adiTemp += picStride;
        pAdiLineTemp--;
        pNeighborFlags--;
        for (j = 0; j < leftUnits; j++)
        {
            if (*pNeighborFlags)
            {
                for (i = 0; i < unitHeight; i++)
                {
                    pAdiLineTemp[-i] = adiTemp[i * picStride];
                }
            }
            adiTemp += unitHeight * picStride;
            pAdiLineTemp -= unitHeight;
            pNeighborFlags--;
        }

        // Fill above & above-right samples
        adiTemp = adiOrigin - picStride;
        pAdiLineTemp = pAdiLine + (leftUnits * unitHeight) + unitWidth;
        pNeighborFlags = bNeighborFlags + leftUnits + 1;
        for (j = 0; j < aboveUnits; j++)
        {
            if (*pNeighborFlags)
                memcpy(pAdiLineTemp, adiTemp, unitWidth * sizeof(*adiTemp));
            adiTemp += unitWidth;
            pAdiLineTemp += unitWidth;
            pNeighborFlags++;
        }

        // Pad reference samples when necessary
        curr = 0;
        next = 1;
        pAdiLineTemp = pAdiLine;
        int pAdiLineTopRowOffset = leftUnits * (unitHeight - unitWidth);
        if (!bNeighborFlags[0])
        {
            // very bottom unit of bottom-left; at least one unit will be valid.
            while (next < totalUnits && !bNeighborFlags[next])
            {
                next++;
            }

            pixel *pAdiLineNext = pAdiLine + ((next < leftUnits) ? (next * unitHeight) : (pAdiLineTopRowOffset + (next * unitWidth)));
            const pixel refSample = *pAdiLineNext;
            // Pad unavailable samples with new value
            int nextOrTop = std::min<int>(next, leftUnits);
            // fill left column
            while (curr < nextOrTop)
            {
                for (i = 0; i < unitHeight; i++)
                {
                    pAdiLineTemp[i] = refSample;
                }

                pAdiLineTemp += unitHeight;
                curr++;
            }

            // fill top row
            while (curr < next)
            {
                for (i = 0; i < unitWidth; i++)
                {
                    pAdiLineTemp[i] = refSample;
                }

                pAdiLineTemp += unitWidth;
                curr++;
            }
        }

        // pad all other reference samples.
        while (curr < totalUnits)
        {
            if (!bNeighborFlags[curr]) // samples not available
            {
                int numSamplesInCurrUnit = (curr >= leftUnits) ? unitWidth : unitHeight;
                const pixel refSample = *(pAdiLineTemp - 1);
                for (i = 0; i < numSamplesInCurrUnit; i++)
                {
                    pAdiLineTemp[i] = refSample;
                }

                pAdiLineTemp += numSamplesInCurrUnit;
                curr++;
            }
            else
            {
                pAdiLineTemp += (curr >= leftUnits) ? unitWidth : unitHeight;
                curr++;
            }
        }

        // Copy processed samples
        pAdiLineTemp = pAdiLine + refSize + unitWidth - 2;
        memcpy(adiRef, pAdiLineTemp, refSize * sizeof(*adiRef));

        pAdiLineTemp = pAdiLine + refSize - 1;
        for (i = 1; i < refSize; i++)
        {
            adiRef[i * ADI_BUF_STRIDE] = pAdiLineTemp[-i];
        }
    }
}

bool TComPattern::isAboveLeftAvailable(const TComDataCU& cu, uint32_t partIdxLT)
{
    uint32_t partAboveLeft;
    const TComDataCU* cuAboveLeft = cu.getPUAboveLeft(partAboveLeft, partIdxLT);

    if (!cu.m_slice->m_pps->bConstrainedIntraPred)
        return cuAboveLeft ? true : false;
    else
        return cuAboveLeft && cuAboveLeft->isIntra(partAboveLeft);
}

int TComPattern::isAboveAvailable(const TComDataCU& cu, uint32_t partIdxLT, uint32_t partIdxRT, bool *bValidFlags)
{
    const uint32_t rasterPartBegin = g_zscanToRaster[partIdxLT];
    const uint32_t rasterPartEnd = g_zscanToRaster[partIdxRT] + 1;
    const uint32_t idxStep = 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t rasterPart = rasterPartBegin; rasterPart < rasterPartEnd; rasterPart += idxStep)
    {
        uint32_t partAbove;
        const TComDataCU* cuAbove = cu.getPUAbove(partAbove, g_rasterToZscan[rasterPart]);
        if (cuAbove && (!cu.m_slice->m_pps->bConstrainedIntraPred || cuAbove->isIntra(partAbove)))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr++;
    }

    return numIntra;
}

int TComPattern::isLeftAvailable(const TComDataCU& cu, uint32_t partIdxLT, uint32_t partIdxLB, bool *bValidFlags)
{
    const uint32_t rasterPartBegin = g_zscanToRaster[partIdxLT];
    const uint32_t rasterPartEnd = g_zscanToRaster[partIdxLB] + 1;
    const uint32_t idxStep = cu.m_pic->getNumPartInCUSize();
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t rasterPart = rasterPartBegin; rasterPart < rasterPartEnd; rasterPart += idxStep)
    {
        uint32_t partLeft;
        const TComDataCU* cuLeft = cu.getPULeft(partLeft, g_rasterToZscan[rasterPart]);
        if (cuLeft && (!cu.m_slice->m_pps->bConstrainedIntraPred || cuLeft->isIntra(partLeft)))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr--; // opposite direction
    }

    return numIntra;
}

int TComPattern::isAboveRightAvailable(const TComDataCU& cu, uint32_t partIdxLT, uint32_t partIdxRT, bool *bValidFlags)
{
    const uint32_t numUnitsInPU = g_zscanToRaster[partIdxRT] - g_zscanToRaster[partIdxLT] + 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t offset = 1; offset <= numUnitsInPU; offset++)
    {
        uint32_t partAboveRight;
        const TComDataCU* cuAboveRight = cu.getPUAboveRightAdi(partAboveRight, partIdxRT, offset);
        if (cuAboveRight && (!cu.m_slice->m_pps->bConstrainedIntraPred || cuAboveRight->isIntra(partAboveRight)))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr++;
    }

    return numIntra;
}

int TComPattern::isBelowLeftAvailable(const TComDataCU& cu, uint32_t partIdxLT, uint32_t partIdxLB, bool *bValidFlags)
{
    const uint32_t numUnitsInPU = (g_zscanToRaster[partIdxLB] - g_zscanToRaster[partIdxLT]) / cu.m_pic->getNumPartInCUSize() + 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t offset = 1; offset <= numUnitsInPU; offset++)
    {
        uint32_t partBelowLeft;
        const TComDataCU* cuBelowLeft = cu.getPUBelowLeftAdi(partBelowLeft, partIdxLB, offset);
        if (cuBelowLeft && (!cu.m_slice->m_pps->bConstrainedIntraPred || cuBelowLeft->isIntra(partBelowLeft)))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr--; // opposite direction
    }

    return numIntra;
}

//! \}
