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
    \brief    neighbouring pixel access classes
*/

#include "TComPic.h"
#include "TComPattern.h"
#include "TComDataCU.h"

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Tables
// ====================================================================================================================

const UChar TComPattern::m_aucIntraFilter[5] =
{
    10, //4x4
    7, //8x8
    1, //16x16
    0, //32x32
    10, //64x64
};
// ====================================================================================================================
// Public member functions (TComPatternParam)
// ====================================================================================================================

/** \param  piTexture     pixel data
 \param  iRoiWidth     pattern width
 \param  iRoiHeight    pattern height
 \param  iStride       buffer stride
 \param  iOffsetLeft   neighbour offset (left)
 \param  iOffsetAbove  neighbour offset (above)
 */
Void TComPatternParam::setPatternParamPel(Pel* piTexture,
                                          Int  iRoiWidth,
                                          Int  iRoiHeight,
                                          Int  iStride,
                                          Int  iOffsetLeft,
                                          Int  iOffsetAbove)
{
    m_piPatternOrigin = piTexture;
    m_iROIWidth       = iRoiWidth;
    m_iROIHeight      = iRoiHeight;
    m_iPatternStride  = iStride;
    m_iOffsetLeft     = iOffsetLeft;
    m_iOffsetAbove    = iOffsetAbove;
}

/**
 \param  cu          CU data structure
 \param  iComp         component index (0=Y, 1=Cb, 2=Cr)
 \param  iRoiWidth     pattern width
 \param  iRoiHeight    pattern height
 \param  iStride       buffer stride
 \param  iOffsetLeft   neighbour offset (left)
 \param  iOffsetAbove  neighbour offset (above)
 \param  absPartIdx  part index
 */
Void TComPatternParam::setPatternParamCU(TComDataCU* cu,
                                         UChar       iComp,
                                         UChar       iRoiWidth,
                                         UChar       iRoiHeight,
                                         Int         iOffsetLeft,
                                         Int         iOffsetAbove,
                                         UInt        absPartIdx)
{
    m_iOffsetLeft   = iOffsetLeft;
    m_iOffsetAbove  = iOffsetAbove;

    m_iROIWidth     = iRoiWidth;
    m_iROIHeight    = iRoiHeight;

    UInt uiAbsZorderIdx = cu->getZorderIdxInCU() + absPartIdx;

    if (iComp == 0)
    {
        m_iPatternStride  = cu->getPic()->getStride();
        m_piPatternOrigin = cu->getPic()->getPicYuvRec()->getLumaAddr(cu->getAddr(), uiAbsZorderIdx) - m_iOffsetAbove * m_iPatternStride - m_iOffsetLeft;
    }
    else
    {
        m_iPatternStride = cu->getPic()->getCStride();
        if (iComp == 1)
        {
            m_piPatternOrigin = cu->getPic()->getPicYuvRec()->getCbAddr(cu->getAddr(), uiAbsZorderIdx) - m_iOffsetAbove * m_iPatternStride - m_iOffsetLeft;
        }
        else
        {
            m_piPatternOrigin = cu->getPic()->getPicYuvRec()->getCrAddr(cu->getAddr(), uiAbsZorderIdx) - m_iOffsetAbove * m_iPatternStride - m_iOffsetLeft;
        }
    }
}

// ====================================================================================================================
// Public member functions (TComPattern)
// ====================================================================================================================

Void TComPattern::initPattern(Pel* piY,
                              Pel* piCb,
                              Pel* piCr,
                              Int  iRoiWidth,
                              Int  iRoiHeight,
                              Int  iStride,
                              Int  iOffsetLeft,
                              Int  iOffsetAbove)
{
    m_cPatternY.setPatternParamPel(piY,  iRoiWidth,      iRoiHeight,      iStride,      iOffsetLeft,      iOffsetAbove);
    m_cPatternCb.setPatternParamPel(piCb, iRoiWidth >> 1, iRoiHeight >> 1, iStride >> 1, iOffsetLeft >> 1, iOffsetAbove >> 1);
    m_cPatternCr.setPatternParamPel(piCr, iRoiWidth >> 1, iRoiHeight >> 1, iStride >> 1, iOffsetLeft >> 1, iOffsetAbove >> 1);
}

Void TComPattern::initPattern(TComDataCU* cu, UInt uiPartDepth, UInt absPartIdx)
{
    Int   uiOffsetLeft  = 0;
    Int   uiOffsetAbove = 0;

    UChar width          = cu->getWidth(0) >> uiPartDepth;
    UChar height         = cu->getHeight(0) >> uiPartDepth;

    UInt  uiAbsZorderIdx   = cu->getZorderIdxInCU() + absPartIdx;
    UInt  uiCurrPicPelX    = cu->getCUPelX() + g_rasterToPelX[g_zscanToRaster[uiAbsZorderIdx]];
    UInt  uiCurrPicPelY    = cu->getCUPelY() + g_rasterToPelY[g_zscanToRaster[uiAbsZorderIdx]];

    if (uiCurrPicPelX != 0)
    {
        uiOffsetLeft = 1;
    }

    if (uiCurrPicPelY != 0)
    {
        uiOffsetAbove = 1;
    }

    m_cPatternY.setPatternParamCU(cu, 0, width,      height,      uiOffsetLeft, uiOffsetAbove, absPartIdx);
    m_cPatternCb.setPatternParamCU(cu, 1, width >> 1, height >> 1, uiOffsetLeft, uiOffsetAbove, absPartIdx);
    m_cPatternCr.setPatternParamCU(cu, 2, width >> 1, height >> 1, uiOffsetLeft, uiOffsetAbove, absPartIdx);
}

Void TComPattern::initAdiPattern(TComDataCU* cu, UInt uiZorderIdxInPart, UInt uiPartDepth, Pel* piAdiBuf, Int iOrgBufStride, Int iOrgBufHeight)
{
    Pel*  piRoiOrigin;
    Pel*  piAdiTemp;
    UInt  uiCuWidth   = cu->getWidth(0) >> uiPartDepth;
    UInt  uiCuHeight  = cu->getHeight(0) >> uiPartDepth;
    UInt  uiCuWidth2  = uiCuWidth << 1;
    UInt  uiCuHeight2 = uiCuHeight << 1;
    UInt  width;
    UInt  height;
    Int   iPicStride = cu->getPic()->getStride();
    Int   iUnitSize = 0;
    Int   iNumUnitsInCu = 0;
    Int   iTotalUnits = 0;
    Bool  bNeighborFlags[4 * MAX_NUM_SPU_W + 1];
    Int   iNumIntraNeighbor = 0;

    UInt uiPartIdxLT, uiPartIdxRT, uiPartIdxLB;

    cu->deriveLeftRightTopIdxAdi(uiPartIdxLT, uiPartIdxRT, uiZorderIdxInPart, uiPartDepth);
    cu->deriveLeftBottomIdxAdi(uiPartIdxLB,              uiZorderIdxInPart, uiPartDepth);

    iUnitSize      = g_maxCUWidth >> g_maxCUDepth;
    iNumUnitsInCu  = uiCuWidth / iUnitSize;
    iTotalUnits    = (iNumUnitsInCu << 2) + 1;

    bNeighborFlags[iNumUnitsInCu * 2] = isAboveLeftAvailable(cu, uiPartIdxLT);
    iNumIntraNeighbor  += (Int)(bNeighborFlags[iNumUnitsInCu * 2]);
    iNumIntraNeighbor  += isAboveAvailable(cu, uiPartIdxLT, uiPartIdxRT, bNeighborFlags + (iNumUnitsInCu * 2) + 1);
    iNumIntraNeighbor  += isAboveRightAvailable(cu, uiPartIdxLT, uiPartIdxRT, bNeighborFlags + (iNumUnitsInCu * 3) + 1);
    iNumIntraNeighbor  += isLeftAvailable(cu, uiPartIdxLT, uiPartIdxLB, bNeighborFlags + (iNumUnitsInCu * 2) - 1);
    iNumIntraNeighbor  += isBelowLeftAvailable(cu, uiPartIdxLT, uiPartIdxLB, bNeighborFlags + iNumUnitsInCu   - 1);

    width = uiCuWidth2 + 1;
    height = uiCuHeight2 + 1;

    if (((width << 2) > iOrgBufStride) || ((height << 2) > iOrgBufHeight))
    {
        return;
    }

    piRoiOrigin = cu->getPic()->getPicYuvRec()->getLumaAddr(cu->getAddr(), cu->getZorderIdxInCU() + uiZorderIdxInPart);
    piAdiTemp   = piAdiBuf;

    fillReferenceSamples(g_bitDepthY, piRoiOrigin, piAdiTemp, bNeighborFlags, iNumIntraNeighbor, iUnitSize, iNumUnitsInCu, iTotalUnits, uiCuWidth, uiCuHeight, width, height, iPicStride);

    Int   i;
    // generate filtered intra prediction samples
    Int iBufSize = uiCuHeight2 + uiCuWidth2 + 1; // left and left above border + above and above right border + top left corner = length of 3. filter buffer

    UInt uiWH = ADI_BUF_STRIDE * height;       // number of elements in one buffer

    Pel* piFilteredBuf1 = piAdiBuf + uiWH;      // 1. filter buffer
    Pel* piFilteredBuf2 = piFilteredBuf1 + uiWH; // 2. filter buffer
    Pel* piFilterBuf = piFilteredBuf2 + uiWH;   // buffer for 2. filtering (sequential)
    Pel* piFilterBufN = piFilterBuf + iBufSize; // buffer for 1. filtering (sequential)

    Int l = 0;
    // left border from bottom to top
    for (i = 0; i < uiCuHeight2; i++)
    {
        piFilterBuf[l++] = piAdiTemp[ADI_BUF_STRIDE * (uiCuHeight2 - i)];
    }

    // top left corner
    piFilterBuf[l++] = piAdiTemp[0];

    // above border from left to right
    memcpy(&piFilterBuf[l], &piAdiTemp[1], uiCuWidth2 * sizeof(*piFilterBuf));

    if (cu->getSlice()->getSPS()->getUseStrongIntraSmoothing())
    {
        Int blkSize = 32;
        Int bottomLeft = piFilterBuf[0];
        Int topLeft = piFilterBuf[uiCuHeight2];
        Int topRight = piFilterBuf[iBufSize - 1];
        Int threshold = 1 << (g_bitDepthY - 5);
        Bool bilinearLeft = abs(bottomLeft + topLeft - 2 * piFilterBuf[uiCuHeight]) < threshold;
        Bool bilinearAbove  = abs(topLeft + topRight - 2 * piFilterBuf[uiCuHeight2 + uiCuHeight]) < threshold;

        if (uiCuWidth >= blkSize && (bilinearLeft && bilinearAbove))
        {
            Int shift = g_convertToBit[uiCuWidth] + 3; // log2(uiCuHeight2)
            piFilterBufN[0] = piFilterBuf[0];
            piFilterBufN[uiCuHeight2] = piFilterBuf[uiCuHeight2];
            piFilterBufN[iBufSize - 1] = piFilterBuf[iBufSize - 1];
            //TODO: Performance Primitive???
            for (i = 1; i < uiCuHeight2; i++)
            {
                piFilterBufN[i] = ((uiCuHeight2 - i) * bottomLeft + i * topLeft + uiCuHeight) >> shift;
            }

            for (i = 1; i < uiCuWidth2; i++)
            {
                piFilterBufN[uiCuHeight2 + i] = ((uiCuWidth2 - i) * topLeft + i * topRight + uiCuWidth) >> shift;
            }
        }
        else
        {
            // 1. filtering with [1 2 1]
            piFilterBufN[0] = piFilterBuf[0];
            piFilterBufN[iBufSize - 1] = piFilterBuf[iBufSize - 1];
            for (i = 1; i < iBufSize - 1; i++)
            {
                piFilterBufN[i] = (piFilterBuf[i - 1] + 2 * piFilterBuf[i] + piFilterBuf[i + 1] + 2) >> 2;
            }
        }
    }
    else
    {
        // 1. filtering with [1 2 1]
        piFilterBufN[0] = piFilterBuf[0];
        piFilterBufN[iBufSize - 1] = piFilterBuf[iBufSize - 1];
        for (i = 1; i < iBufSize - 1; i++)
        {
            piFilterBufN[i] = (piFilterBuf[i - 1] + 2 * piFilterBuf[i] + piFilterBuf[i + 1] + 2) >> 2;
        }
    }

    // fill 1. filter buffer with filtered values
    l = 0;
    for (i = 0; i < uiCuHeight2; i++)
    {
        piFilteredBuf1[ADI_BUF_STRIDE * (uiCuHeight2 - i)] = piFilterBufN[l++];
    }

    piFilteredBuf1[0] = piFilterBufN[l++];
    memcpy(&piFilteredBuf1[1], &piFilterBufN[l], uiCuWidth2 * sizeof(*piFilteredBuf1));
}

//Overloaded initialiation of ADI buffers to support buffered references for xpredIntraAngBufRef
Void TComPattern::initAdiPattern(TComDataCU* cu, UInt uiZorderIdxInPart, UInt uiPartDepth, Pel* piAdiBuf, Int iOrgBufStride, Int iOrgBufHeight, Pel* refAbove, Pel* refLeft, Pel* refAboveFlt, Pel* refLeftFlt)
{
    initAdiPattern(cu, uiZorderIdxInPart, uiPartDepth, piAdiBuf, iOrgBufStride, iOrgBufHeight);
    UInt  uiCuWidth   = cu->getWidth(0) >> uiPartDepth;
    UInt  uiCuHeight  = cu->getHeight(0) >> uiPartDepth;
    UInt  uiCuWidth2  = uiCuWidth << 1;
    UInt  uiCuHeight2 = uiCuHeight << 1;

    refAbove += uiCuWidth - 1;
    refAboveFlt += uiCuWidth - 1;
    refLeft += uiCuWidth - 1;
    refLeftFlt += uiCuWidth - 1;

    //  ADI_BUF_STRIDE * (2 * height + 1);
    memcpy(refAbove, piAdiBuf, (uiCuWidth2 + 1) * sizeof(Pel));
    memcpy(refAboveFlt, piAdiBuf + ADI_BUF_STRIDE * (2 * uiCuHeight + 1), (uiCuWidth2 + 1) * sizeof(Pel));

    for (int k = 0; k < uiCuHeight2 + 1; k++)
    {
        refLeft[k] = piAdiBuf[k * ADI_BUF_STRIDE];
        refLeftFlt[k] = (piAdiBuf + ADI_BUF_STRIDE * (uiCuHeight2 + 1))[k * ADI_BUF_STRIDE];   // Smoothened
    }
}

Void TComPattern::initAdiPatternChroma(TComDataCU* cu, UInt uiZorderIdxInPart, UInt uiPartDepth, Pel* piAdiBuf, Int iOrgBufStride, Int iOrgBufHeight)
{
    Pel*  piRoiOrigin;
    Pel*  piAdiTemp;
    UInt  uiCuWidth  = cu->getWidth(0) >> uiPartDepth;
    UInt  uiCuHeight = cu->getHeight(0) >> uiPartDepth;
    UInt  width;
    UInt  height;
    Int   iPicStride = cu->getPic()->getCStride();

    Int   iUnitSize = 0;
    Int   iNumUnitsInCu = 0;
    Int   iTotalUnits = 0;
    Bool  bNeighborFlags[4 * MAX_NUM_SPU_W + 1];
    Int   iNumIntraNeighbor = 0;

    UInt uiPartIdxLT, uiPartIdxRT, uiPartIdxLB;

    cu->deriveLeftRightTopIdxAdi(uiPartIdxLT, uiPartIdxRT, uiZorderIdxInPart, uiPartDepth);
    cu->deriveLeftBottomIdxAdi(uiPartIdxLB,              uiZorderIdxInPart, uiPartDepth);

    iUnitSize      = (g_maxCUWidth >> g_maxCUDepth) >> 1; // for chroma
    iNumUnitsInCu  = (uiCuWidth / iUnitSize) >> 1;          // for chroma
    iTotalUnits    = (iNumUnitsInCu << 2) + 1;

    bNeighborFlags[iNumUnitsInCu * 2] = isAboveLeftAvailable(cu, uiPartIdxLT);
    iNumIntraNeighbor  += (Int)(bNeighborFlags[iNumUnitsInCu * 2]);
    iNumIntraNeighbor  += isAboveAvailable(cu, uiPartIdxLT, uiPartIdxRT, bNeighborFlags + (iNumUnitsInCu * 2) + 1);
    iNumIntraNeighbor  += isAboveRightAvailable(cu, uiPartIdxLT, uiPartIdxRT, bNeighborFlags + (iNumUnitsInCu * 3) + 1);
    iNumIntraNeighbor  += isLeftAvailable(cu, uiPartIdxLT, uiPartIdxLB, bNeighborFlags + (iNumUnitsInCu * 2) - 1);
    iNumIntraNeighbor  += isBelowLeftAvailable(cu, uiPartIdxLT, uiPartIdxLB, bNeighborFlags + iNumUnitsInCu   - 1);

    uiCuWidth = uiCuWidth >> 1; // for chroma
    uiCuHeight = uiCuHeight >> 1; // for chroma

    width = uiCuWidth * 2 + 1;
    height = uiCuHeight * 2 + 1;

    if ((4 * width > iOrgBufStride) || (4 * height > iOrgBufHeight))
    {
        return;
    }

    // get Cb pattern
    piRoiOrigin = cu->getPic()->getPicYuvRec()->getCbAddr(cu->getAddr(), cu->getZorderIdxInCU() + uiZorderIdxInPart);
    piAdiTemp   = piAdiBuf;

    fillReferenceSamples(g_bitDepthC, piRoiOrigin, piAdiTemp, bNeighborFlags, iNumIntraNeighbor, iUnitSize, iNumUnitsInCu, iTotalUnits, uiCuWidth, uiCuHeight, width, height, iPicStride);

    // get Cr pattern
    piRoiOrigin = cu->getPic()->getPicYuvRec()->getCrAddr(cu->getAddr(), cu->getZorderIdxInCU() + uiZorderIdxInPart);
    piAdiTemp   = piAdiBuf + ADI_BUF_STRIDE * height;

    fillReferenceSamples(g_bitDepthC, piRoiOrigin, piAdiTemp, bNeighborFlags, iNumIntraNeighbor, iUnitSize, iNumUnitsInCu, iTotalUnits, uiCuWidth, uiCuHeight, width, height, iPicStride);
}

Void TComPattern::fillReferenceSamples(Int bitDepth, Pel* piRoiOrigin, Pel* piAdiTemp, Bool* bNeighborFlags, Int iNumIntraNeighbor, Int iUnitSize, Int iNumUnitsInCu, Int iTotalUnits, UInt uiCuWidth, UInt uiCuHeight, UInt width, UInt height, Int iPicStride)
{
    Pel* piRoiTemp;
    Int  i, j;
    Int  iDCValue = 1 << (bitDepth - 1);

    if (iNumIntraNeighbor == 0)
    {
        // Fill border with DC value
        for (i = 0; i < width; i++)
        {
            piAdiTemp[i] = iDCValue;
        }

        for (i = 1; i < height; i++)
        {
            piAdiTemp[i * ADI_BUF_STRIDE] = iDCValue;
        }
    }
    else if (iNumIntraNeighbor == iTotalUnits)
    {
        // Fill top-left border with rec. samples
        piRoiTemp = piRoiOrigin - iPicStride - 1;
        piAdiTemp[0] = piRoiTemp[0];

        // Fill left border with rec. samples
        // Fill below left border with rec. samples
        piRoiTemp = piRoiOrigin - 1;

        for (i = 0; i < 2 * uiCuHeight; i++)
        {
            piAdiTemp[(1 + i) * ADI_BUF_STRIDE] = piRoiTemp[0];
            piRoiTemp += iPicStride;
        }

        // Fill top border with rec. samples
        // Fill top right border with rec. samples
        piRoiTemp = piRoiOrigin - iPicStride;
        memcpy(&piAdiTemp[1], piRoiTemp, 2 * uiCuWidth * sizeof(*piAdiTemp));
    }
    else // reference samples are partially available
    {
        Int  iNumUnits2 = iNumUnitsInCu << 1;
        Int  iTotalSamples = iTotalUnits * iUnitSize;
        Pel  piAdiLine[5 * MAX_CU_SIZE];
        Pel  *piAdiLineTemp;
        Bool *pbNeighborFlags;
        Int  iNext, iCurr;
        Pel  piRef = 0;

        // Initialize
        for (i = 0; i < iTotalSamples; i++)
        {
            piAdiLine[i] = iDCValue;
        }

        // Fill top-left sample
        piRoiTemp = piRoiOrigin - iPicStride - 1;
        piAdiLineTemp = piAdiLine + (iNumUnits2 * iUnitSize);
        pbNeighborFlags = bNeighborFlags + iNumUnits2;
        if (*pbNeighborFlags)
        {
            piAdiLineTemp[0] = piRoiTemp[0];
            for (i = 1; i < iUnitSize; i++)
            {
                piAdiLineTemp[i] = piAdiLineTemp[0];
            }
        }

        // Fill left & below-left samples
        piRoiTemp += iPicStride;
        piAdiLineTemp--;
        pbNeighborFlags--;
        for (j = 0; j < iNumUnits2; j++)
        {
            if (*pbNeighborFlags)
            {
                for (i = 0; i < iUnitSize; i++)
                {
                    piAdiLineTemp[-i] = piRoiTemp[i * iPicStride];
                }
            }
            piRoiTemp += iUnitSize * iPicStride;
            piAdiLineTemp -= iUnitSize;
            pbNeighborFlags--;
        }

        // Fill above & above-right samples
        piRoiTemp = piRoiOrigin - iPicStride;
        piAdiLineTemp = piAdiLine + ((iNumUnits2 + 1) * iUnitSize);
        pbNeighborFlags = bNeighborFlags + iNumUnits2 + 1;
        for (j = 0; j < iNumUnits2; j++)
        {
            if (*pbNeighborFlags)
            {
                memcpy(piAdiLineTemp, piRoiTemp, iUnitSize * sizeof(*piAdiTemp));
            }
            piRoiTemp += iUnitSize;
            piAdiLineTemp += iUnitSize;
            pbNeighborFlags++;
        }

        // Pad reference samples when necessary
        iCurr = 0;
        iNext = 1;
        piAdiLineTemp = piAdiLine;
        while (iCurr < iTotalUnits)
        {
            if (!bNeighborFlags[iCurr])
            {
                if (iCurr == 0)
                {
                    while (iNext < iTotalUnits && !bNeighborFlags[iNext])
                    {
                        iNext++;
                    }

                    piRef = piAdiLine[iNext * iUnitSize];
                    // Pad unavailable samples with new value
                    while (iCurr < iNext)
                    {
                        for (i = 0; i < iUnitSize; i++)
                        {
                            piAdiLineTemp[i] = piRef;
                        }

                        piAdiLineTemp += iUnitSize;
                        iCurr++;
                    }
                }
                else
                {
                    piRef = piAdiLine[iCurr * iUnitSize - 1];
                    for (i = 0; i < iUnitSize; i++)
                    {
                        piAdiLineTemp[i] = piRef;
                    }

                    piAdiLineTemp += iUnitSize;
                    iCurr++;
                }
            }
            else
            {
                piAdiLineTemp += iUnitSize;
                iCurr++;
            }
        }

        // Copy processed samples
        piAdiLineTemp = piAdiLine + height + iUnitSize - 2;
        memcpy(piAdiTemp, piAdiLineTemp, width * sizeof(*piAdiTemp));

        piAdiLineTemp = piAdiLine + height - 1;
        for (i = 1; i < height; i++)
        {
            piAdiTemp[i * ADI_BUF_STRIDE] = piAdiLineTemp[-i];
        }
    }
}

Pel* TComPattern::getAdiOrgBuf(Int /*iCuWidth*/, Int /*iCuHeight*/, Pel* piAdiBuf)
{
    return piAdiBuf;
}

Pel* TComPattern::getAdiCbBuf(Int /*iCuWidth*/, Int /*iCuHeight*/, Pel* piAdiBuf)
{
    return piAdiBuf;
}

Pel* TComPattern::getAdiCrBuf(Int iCuWidth, Int iCuHeight, Pel* piAdiBuf)
{
    return piAdiBuf + ADI_BUF_STRIDE * (iCuHeight * 2 + 1);
}

/** Get pointer to reference samples for intra prediction
 * \param uiDirMode   prediction mode index
 * \param log2BlkSize size of block (2 = 4x4, 3 = 8x8, 4 = 16x16, 5 = 32x32, 6 = 64x64)
 * \param piAdiBuf    pointer to unfiltered reference samples
 * \return            pointer to (possibly filtered) reference samples
 *
 * The prediction mode index is used to determine whether a smoothed reference sample buffer is returned.
 */
Pel* TComPattern::getPredictorPtr(UInt uiDirMode, UInt log2BlkSize, Pel* piAdiBuf)
{
    Pel* piSrc;

    assert(log2BlkSize >= 2 && log2BlkSize < 7);
    Int diff = min<Int>(abs((Int)uiDirMode - HOR_IDX), abs((Int)uiDirMode - VER_IDX));
    UChar ucFiltIdx = diff > m_aucIntraFilter[log2BlkSize - 2] ? 1 : 0;
    if (uiDirMode == DC_IDX)
    {
        ucFiltIdx = 0; //no smoothing for DC or LM chroma
    }

    assert(ucFiltIdx <= 1);

    Int width  = 1 << log2BlkSize;
    Int height = 1 << log2BlkSize;

    piSrc = getAdiOrgBuf(width, height, piAdiBuf);

    if (ucFiltIdx)
    {
        piSrc += ADI_BUF_STRIDE * (2 * height + 1);
    }

    return piSrc;
}

Bool TComPattern::isAboveLeftAvailable(TComDataCU* cu, UInt uiPartIdxLT)
{
    Bool bAboveLeftFlag;
    UInt uiPartAboveLeft;
    TComDataCU* pcCUAboveLeft = cu->getPUAboveLeft(uiPartAboveLeft, uiPartIdxLT);

    if (cu->getSlice()->getPPS()->getConstrainedIntraPred())
    {
        bAboveLeftFlag = (pcCUAboveLeft && pcCUAboveLeft->getPredictionMode(uiPartAboveLeft) == MODE_INTRA);
    }
    else
    {
        bAboveLeftFlag = (pcCUAboveLeft ? true : false);
    }
    return bAboveLeftFlag;
}

Int TComPattern::isAboveAvailable(TComDataCU* cu, UInt uiPartIdxLT, UInt uiPartIdxRT, Bool *bValidFlags)
{
    const UInt uiRasterPartBegin = g_zscanToRaster[uiPartIdxLT];
    const UInt uiRasterPartEnd = g_zscanToRaster[uiPartIdxRT] + 1;
    const UInt uiIdxStep = 1;
    Bool *pbValidFlags = bValidFlags;
    Int iNumIntra = 0;

    for (UInt uiRasterPart = uiRasterPartBegin; uiRasterPart < uiRasterPartEnd; uiRasterPart += uiIdxStep)
    {
        UInt uiPartAbove;
        TComDataCU* pcCUAbove = cu->getPUAbove(uiPartAbove, g_rasterToZscan[uiRasterPart]);
        if (cu->getSlice()->getPPS()->getConstrainedIntraPred())
        {
            if (pcCUAbove && pcCUAbove->getPredictionMode(uiPartAbove) == MODE_INTRA)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        else
        {
            if (pcCUAbove)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        pbValidFlags++;
    }

    return iNumIntra;
}

Int TComPattern::isLeftAvailable(TComDataCU* cu, UInt uiPartIdxLT, UInt uiPartIdxLB, Bool *bValidFlags)
{
    const UInt uiRasterPartBegin = g_zscanToRaster[uiPartIdxLT];
    const UInt uiRasterPartEnd = g_zscanToRaster[uiPartIdxLB] + 1;
    const UInt uiIdxStep = cu->getPic()->getNumPartInWidth();
    Bool *pbValidFlags = bValidFlags;
    Int iNumIntra = 0;

    for (UInt uiRasterPart = uiRasterPartBegin; uiRasterPart < uiRasterPartEnd; uiRasterPart += uiIdxStep)
    {
        UInt uiPartLeft;
        TComDataCU* pcCULeft = cu->getPULeft(uiPartLeft, g_rasterToZscan[uiRasterPart]);
        if (cu->getSlice()->getPPS()->getConstrainedIntraPred())
        {
            if (pcCULeft && pcCULeft->getPredictionMode(uiPartLeft) == MODE_INTRA)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        else
        {
            if (pcCULeft)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        pbValidFlags--; // opposite direction
    }

    return iNumIntra;
}

Int TComPattern::isAboveRightAvailable(TComDataCU* cu, UInt uiPartIdxLT, UInt uiPartIdxRT, Bool *bValidFlags)
{
    const UInt uiNumUnitsInPU = g_zscanToRaster[uiPartIdxRT] - g_zscanToRaster[uiPartIdxLT] + 1;
    Bool *pbValidFlags = bValidFlags;
    Int iNumIntra = 0;

    for (UInt uiOffset = 1; uiOffset <= uiNumUnitsInPU; uiOffset++)
    {
        UInt uiPartAboveRight;
        TComDataCU* pcCUAboveRight = cu->getPUAboveRightAdi(uiPartAboveRight, uiPartIdxRT, uiOffset);
        if (cu->getSlice()->getPPS()->getConstrainedIntraPred())
        {
            if (pcCUAboveRight && pcCUAboveRight->getPredictionMode(uiPartAboveRight) == MODE_INTRA)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        else
        {
            if (pcCUAboveRight)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        pbValidFlags++;
    }

    return iNumIntra;
}

Int TComPattern::isBelowLeftAvailable(TComDataCU* cu, UInt uiPartIdxLT, UInt uiPartIdxLB, Bool *bValidFlags)
{
    const UInt uiNumUnitsInPU = (g_zscanToRaster[uiPartIdxLB] - g_zscanToRaster[uiPartIdxLT]) / cu->getPic()->getNumPartInWidth() + 1;
    Bool *pbValidFlags = bValidFlags;
    Int iNumIntra = 0;

    for (UInt uiOffset = 1; uiOffset <= uiNumUnitsInPU; uiOffset++)
    {
        UInt uiPartBelowLeft;
        TComDataCU* pcCUBelowLeft = cu->getPUBelowLeftAdi(uiPartBelowLeft, uiPartIdxLB, uiOffset);
        if (cu->getSlice()->getPPS()->getConstrainedIntraPred())
        {
            if (pcCUBelowLeft && pcCUBelowLeft->getPredictionMode(uiPartBelowLeft) == MODE_INTRA)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        else
        {
            if (pcCUBelowLeft)
            {
                iNumIntra++;
                *pbValidFlags = true;
            }
            else
            {
                *pbValidFlags = false;
            }
        }
        pbValidFlags--; // opposite direction
    }

    return iNumIntra;
}

//! \}
