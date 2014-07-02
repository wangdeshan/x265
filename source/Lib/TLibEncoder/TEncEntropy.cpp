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

/** \file     TEncEntropy.cpp
    \brief    entropy encoder class
*/

#include "TEncEntropy.h"
#include "TLibCommon/TypeDef.h"
#include "TLibCommon/TComSampleAdaptiveOffset.h"

using namespace x265;

//! \ingroup TLibEncoder
//! \{

void TEncEntropy::encodeInterDirPU(TComDataCU* cu, uint32_t absPartIdx)
{
    if (!cu->getSlice()->isInterB())
    {
        return;
    }

    m_entropyCoder->codeInterDir(cu, absPartIdx);
}

bool TEncEntropy::isNextTUSection(TComTURecurse *tuIterator)
{
    if (tuIterator->m_splitMode == DONT_SPLIT)
    {
        tuIterator->m_section++;
        return false;
    }
    else
    {
        tuIterator->m_absPartIdxTURelCU += tuIterator->m_absPartIdxStep;

        tuIterator->m_section++;
        return tuIterator->m_section < (1 << tuIterator->m_splitMode);
    }
}

void TEncEntropy::initTUEntropySection(TComTURecurse *tuIterator, uint32_t splitMode, uint32_t absPartIdxStep, uint32_t m_absPartIdxTU)
{
    tuIterator->m_section           = 0;
    tuIterator->m_absPartIdxTURelCU = m_absPartIdxTU;
    tuIterator->m_splitMode         = splitMode;
    tuIterator->m_absPartIdxStep    = absPartIdxStep >> partIdxStepShift[splitMode];
}

void TEncEntropy::xEncodeTransform(TComDataCU* cu, uint32_t offsetLuma, uint32_t offsetChroma, uint32_t absPartIdx, uint32_t absPartIdxStep, uint32_t depth, uint32_t tuSize, uint32_t trIdx, bool& bCodeDQP)
{
    const uint32_t subdiv = cu->getTransformIdx(absPartIdx) + cu->getDepth(absPartIdx) > depth;
    const uint32_t log2TrSize = cu->getSlice()->getSPS()->getLog2MaxCodingBlockSize() - depth;
    uint32_t hChromaShift = cu->getHorzChromaShift();
    uint32_t vChromaShift = cu->getVertChromaShift();
    uint32_t cbfY = cu->getCbf(absPartIdx, TEXT_LUMA, trIdx);
    uint32_t cbfU = cu->getCbf(absPartIdx, TEXT_CHROMA_U, trIdx);
    uint32_t cbfV = cu->getCbf(absPartIdx, TEXT_CHROMA_V, trIdx);

    if (trIdx == 0)
    {
        m_bakAbsPartIdxCU = absPartIdx;
    }

    if ((log2TrSize == 2) && !(cu->getChromaFormat() == CHROMA_444))
    {
        uint32_t partNum = cu->getPic()->getNumPartInCU() >> ((depth - 1) << 1);
        if ((absPartIdx & (partNum - 1)) == 0)
        {
            m_bakAbsPartIdx   = absPartIdx;
            m_bakChromaOffset = offsetChroma;
        }
        else if ((absPartIdx & (partNum - 1)) == (partNum - 1))
        {
            cbfU = cu->getCbf(m_bakAbsPartIdx, TEXT_CHROMA_U, trIdx);
            cbfV = cu->getCbf(m_bakAbsPartIdx, TEXT_CHROMA_V, trIdx);
        }
    }

    if (cu->getPredictionMode(absPartIdx) == MODE_INTRA && cu->getPartitionSize(absPartIdx) == SIZE_NxN && depth == cu->getDepth(absPartIdx))
    {
        X265_CHECK(subdiv, "subdivision state failure\n");
    }
    else if (cu->getPredictionMode(absPartIdx) == MODE_INTER && (cu->getPartitionSize(absPartIdx) != SIZE_2Nx2N) && depth == cu->getDepth(absPartIdx) &&  (cu->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1))
    {
        if (log2TrSize > cu->getQuadtreeTULog2MinSizeInCU(absPartIdx))
        {
            X265_CHECK(subdiv, "subdivision state failure\n");
        }
        else
        {
            X265_CHECK(!subdiv, "subdivision state failure\n");
        }
    }
    else if (log2TrSize > cu->getSlice()->getSPS()->getQuadtreeTULog2MaxSize())
    {
        X265_CHECK(subdiv, "subdivision state failure\n");
    }
    else if (log2TrSize == cu->getSlice()->getSPS()->getQuadtreeTULog2MinSize())
    {
        X265_CHECK(!subdiv, "subdivision state failure\n");
    }
    else if (log2TrSize == cu->getQuadtreeTULog2MinSizeInCU(absPartIdx))
    {
        X265_CHECK(!subdiv, "subdivision state failure\n");
    }
    else
    {
        X265_CHECK(log2TrSize > cu->getQuadtreeTULog2MinSizeInCU(absPartIdx), "transform size failure\n");
        m_entropyCoder->codeTransformSubdivFlag(subdiv, 5 - log2TrSize);
    }

    const uint32_t trDepthCurr = depth - cu->getDepth(absPartIdx);
    const bool bFirstCbfOfCU = trDepthCurr == 0;

    bool mCodeAll = true;
    const uint32_t numPels = (tuSize * tuSize) >> (hChromaShift + vChromaShift);
    if (numPels < (MIN_TU_SIZE * MIN_TU_SIZE))
    {
        mCodeAll = false;
    }

    if (bFirstCbfOfCU || mCodeAll)
    {
        if (bFirstCbfOfCU || cu->getCbf(absPartIdx, TEXT_CHROMA_U, trDepthCurr - 1))
        {
            m_entropyCoder->codeQtCbf(cu, absPartIdx, TEXT_CHROMA_U, trDepthCurr, absPartIdxStep, (tuSize >> hChromaShift), (tuSize >> vChromaShift), (subdiv == 0));
        }
        if (bFirstCbfOfCU || cu->getCbf(absPartIdx, TEXT_CHROMA_V, trDepthCurr - 1))
        {
            m_entropyCoder->codeQtCbf(cu, absPartIdx, TEXT_CHROMA_V, trDepthCurr, absPartIdxStep, (tuSize >> hChromaShift), (tuSize >> vChromaShift), (subdiv == 0));
        }
    }
    else
    {
        X265_CHECK(cu->getCbf(absPartIdx, TEXT_CHROMA_U, trDepthCurr) == cu->getCbf(absPartIdx, TEXT_CHROMA_U, trDepthCurr - 1), "chroma xform size match failure\n");
        X265_CHECK(cu->getCbf(absPartIdx, TEXT_CHROMA_V, trDepthCurr) == cu->getCbf(absPartIdx, TEXT_CHROMA_V, trDepthCurr - 1), "chroma xform size match failure\n");
    }

    if (subdiv)
    {
        tuSize >>= 1;
        uint32_t numCoeff  = tuSize * tuSize;
        uint32_t numCoeffC = (numCoeff >> (hChromaShift + vChromaShift));
        trIdx++;
        ++depth;
        absPartIdxStep >>= 2;
        const uint32_t partNum = cu->getPic()->getNumPartInCU() >> (depth << 1);

        xEncodeTransform(cu, offsetLuma, offsetChroma, absPartIdx, absPartIdxStep, depth, tuSize, trIdx, bCodeDQP);

        absPartIdx += partNum;
        offsetLuma += numCoeff;
        offsetChroma += numCoeffC;
        xEncodeTransform(cu, offsetLuma, offsetChroma, absPartIdx, absPartIdxStep, depth, tuSize, trIdx, bCodeDQP);

        absPartIdx += partNum;
        offsetLuma += numCoeff;
        offsetChroma += numCoeffC;
        xEncodeTransform(cu, offsetLuma, offsetChroma, absPartIdx, absPartIdxStep, depth, tuSize, trIdx, bCodeDQP);

        absPartIdx += partNum;
        offsetLuma += numCoeff;
        offsetChroma += numCoeffC;
        xEncodeTransform(cu, offsetLuma, offsetChroma, absPartIdx, absPartIdxStep, depth, tuSize, trIdx, bCodeDQP);
    }
    else
    {
        {
            DTRACE_CABAC_VL(g_nSymbolCounter++);
            DTRACE_CABAC_T("\tTrIdx: abspart=");
            DTRACE_CABAC_V(absPartIdx);
            DTRACE_CABAC_T("\tdepth=");
            DTRACE_CABAC_V(depth);
            DTRACE_CABAC_T("\ttrdepth=");
            DTRACE_CABAC_V(cu->getTransformIdx(absPartIdx));
            DTRACE_CABAC_T("\n");
        }

        if (cu->getPredictionMode(absPartIdx) != MODE_INTRA && depth == cu->getDepth(absPartIdx) && !cu->getCbf(absPartIdx, TEXT_CHROMA_U, 0) && !cu->getCbf(absPartIdx, TEXT_CHROMA_V, 0))
        {
            X265_CHECK(cu->getCbf(absPartIdx, TEXT_LUMA, 0), "CBF should have been set\n");
        }
        else
        {
            m_entropyCoder->codeQtCbf(cu, absPartIdx, TEXT_LUMA, cu->getTransformIdx(absPartIdx));
        }

        if (cbfY || cbfU || cbfV)
        {
            // dQP: only for LCU once
            if (cu->getSlice()->getPPS()->getUseDQP())
            {
                if (bCodeDQP)
                {
                    encodeQP(cu, m_bakAbsPartIdxCU);
                    bCodeDQP = false;
                }
            }
        }
        if (cbfY)
        {
            m_entropyCoder->codeCoeffNxN(cu, (cu->getCoeffY() + offsetLuma), absPartIdx, log2TrSize, TEXT_LUMA);
        }

        int chFmt = cu->getChromaFormat();
        if ((log2TrSize == 2) && !(chFmt == CHROMA_444))
        {
            uint32_t partNum = cu->getPic()->getNumPartInCU() >> ((depth - 1) << 1);
            if ((absPartIdx & (partNum - 1)) == (partNum - 1))
            {
                const uint32_t log2TrSizeC = 2;
                const bool splitIntoSubTUs = (chFmt == CHROMA_422);

                uint32_t curPartNum = cu->getPic()->getNumPartInCU() >> ((depth - 1) << 1);

                for (uint32_t chromaId = TEXT_CHROMA_U; chromaId <= TEXT_CHROMA_V; chromaId++)
                {
                    TComTURecurse tuIterator;
                    initTUEntropySection(&tuIterator, splitIntoSubTUs ? VERTICAL_SPLIT : DONT_SPLIT, curPartNum, m_bakAbsPartIdx);
                    coeff_t* coeffChroma = cu->getCoeff((TextType)chromaId);
                    do
                    {
                        uint32_t cbf = cu->getCbf(tuIterator.m_absPartIdxTURelCU, (TextType)chromaId, trIdx + splitIntoSubTUs);
                        if (cbf)
                        {
                            uint32_t subTUOffset = tuIterator.m_section << (log2TrSizeC * 2);
                            m_entropyCoder->codeCoeffNxN(cu, (coeffChroma + m_bakChromaOffset + subTUOffset), tuIterator.m_absPartIdxTURelCU, log2TrSizeC, (TextType)chromaId);
                        }
                    }
                    while (isNextTUSection(&tuIterator));
                }
            }
        }
        else
        {
            uint32_t log2TrSizeC = log2TrSize - hChromaShift;
            const bool splitIntoSubTUs = (chFmt == CHROMA_422);
            uint32_t curPartNum = cu->getPic()->getNumPartInCU() >> (depth << 1);
            for (uint32_t chromaId = TEXT_CHROMA_U; chromaId <= TEXT_CHROMA_V; chromaId++)
            {
                TComTURecurse tuIterator;
                initTUEntropySection(&tuIterator, splitIntoSubTUs ? VERTICAL_SPLIT : DONT_SPLIT, curPartNum, absPartIdx);
                coeff_t* coeffChroma = cu->getCoeff((TextType)chromaId);
                do
                {
                    uint32_t cbf = cu->getCbf(tuIterator.m_absPartIdxTURelCU, (TextType)chromaId, trIdx + splitIntoSubTUs);
                    if (cbf)
                    {
                        uint32_t subTUOffset = tuIterator.m_section << (log2TrSizeC * 2);
                        m_entropyCoder->codeCoeffNxN(cu, (coeffChroma + offsetChroma + subTUOffset), tuIterator.m_absPartIdxTURelCU, log2TrSizeC, (TextType)chromaId);
                    }
                }
                while (isNextTUSection(&tuIterator));
            }
        }
    }
}

void TEncEntropy::encodePredInfo(TComDataCU* cu, uint32_t absPartIdx)
{
    if (cu->isIntra(absPartIdx)) // If it is Intra mode, encode intra prediction mode.
    {
        encodeIntraDirModeLuma(cu, absPartIdx, true);
        int chFmt = cu->getChromaFormat();
        if (chFmt != CHROMA_400)
        {
            encodeIntraDirModeChroma(cu, absPartIdx);

            if ((chFmt == CHROMA_444) && (cu->getPartitionSize(absPartIdx) == SIZE_NxN))
            {
                uint32_t partOffset = (cu->getPic()->getNumPartInCU() >> (cu->getDepth(absPartIdx) << 1)) >> 2;
                encodeIntraDirModeChroma(cu, absPartIdx + partOffset);
                encodeIntraDirModeChroma(cu, absPartIdx + partOffset * 2);
                encodeIntraDirModeChroma(cu, absPartIdx + partOffset * 3);
            }
        }
    }
    else                        // if it is Inter mode, encode motion vector and reference index
    {
        encodePUWise(cu, absPartIdx);
    }
}

/** encode motion information for every PU block */
void TEncEntropy::encodePUWise(TComDataCU* cu, uint32_t absPartIdx)
{
    PartSize partSize = cu->getPartitionSize(absPartIdx);
    uint32_t numPU = (partSize == SIZE_2Nx2N ? 1 : (partSize == SIZE_NxN ? 4 : 2));
    uint32_t depth = cu->getDepth(absPartIdx);
    uint32_t puOffset = (g_puOffset[uint32_t(partSize)] << ((cu->getSlice()->getSPS()->getMaxCUDepth() - depth) << 1)) >> 4;

    for (uint32_t partIdx = 0, subPartIdx = absPartIdx; partIdx < numPU; partIdx++, subPartIdx += puOffset)
    {
        encodeMergeFlag(cu, subPartIdx);
        if (cu->getMergeFlag(subPartIdx))
        {
            encodeMergeIndex(cu, subPartIdx);
        }
        else
        {
            uint32_t interDir = cu->getInterDir(subPartIdx);
            encodeInterDirPU(cu, subPartIdx);
            for (uint32_t refListIdx = 0; refListIdx < 2; refListIdx++)
            {
                if (interDir & (1 << refListIdx))
                {
                    X265_CHECK(cu->getSlice()->getNumRefIdx(refListIdx) > 0, "numRefs should have been > 0\n");

                    encodeRefFrmIdxPU(cu, subPartIdx, refListIdx);
                    encodeMvdPU(cu, subPartIdx, refListIdx);
                    encodeMVPIdxPU(cu, subPartIdx, refListIdx);
                }
            }
        }
    }
}

/** encode reference frame index for a PU block */
void TEncEntropy::encodeRefFrmIdxPU(TComDataCU* cu, uint32_t absPartIdx, int list)
{
    X265_CHECK(!cu->isIntra(absPartIdx), "intra block expected\n");
    {
        if ((cu->getSlice()->getNumRefIdx(list) == 1))
        {
            return;
        }

        X265_CHECK(cu->getInterDir(absPartIdx) & (1 << list), "inter dir failure\n");
        {
            m_entropyCoder->codeRefFrmIdx(cu, absPartIdx, list);
        }
    }
}

void TEncEntropy::encodeCoeff(TComDataCU* cu, uint32_t absPartIdx, uint32_t depth, uint32_t cuSize, bool& bCodeDQP)
{
    uint32_t lumaOffset   = absPartIdx << cu->getPic()->getLog2UnitSize() * 2;
    uint32_t chromaOffset = lumaOffset >> (cu->getHorzChromaShift() + cu->getVertChromaShift());

    if (cu->isIntra(absPartIdx))
    {
        DTRACE_CABAC_VL(g_nSymbolCounter++)
        DTRACE_CABAC_T("\tdecodeTransformIdx()\tCUDepth=")
        DTRACE_CABAC_V(depth)
        DTRACE_CABAC_T("\n")
    }
    else
    {
        if (!(cu->getMergeFlag(absPartIdx) && cu->getPartitionSize(absPartIdx) == SIZE_2Nx2N))
        {
            m_entropyCoder->codeQtRootCbf(cu, absPartIdx);
        }
        if (!cu->getQtRootCbf(absPartIdx))
        {
            return;
        }
    }

    uint32_t absPartIdxStep = cu->getPic()->getNumPartInCU() >> (depth << 1);
    xEncodeTransform(cu, lumaOffset, chromaOffset, absPartIdx, absPartIdxStep, depth, cuSize, 0, bCodeDQP);
}

void TEncEntropy::encodeSaoOffset(SaoLcuParam* saoLcuParam, uint32_t compIdx)
{
    uint32_t symbol;
    int i;

    symbol = saoLcuParam->typeIdx + 1;
    if (compIdx != 2)
    {
        m_entropyCoder->codeSaoTypeIdx(symbol);
    }
    if (symbol)
    {
        if (saoLcuParam->typeIdx < 4 && compIdx != 2)
        {
            saoLcuParam->subTypeIdx = saoLcuParam->typeIdx;
        }
        int offsetTh = 1 << X265_MIN(X265_DEPTH - 5, 5);
        if (saoLcuParam->typeIdx == SAO_BO)
        {
            for (i = 0; i < saoLcuParam->length; i++)
            {
                uint32_t absOffset = ((saoLcuParam->offset[i] < 0) ? -saoLcuParam->offset[i] : saoLcuParam->offset[i]);
                m_entropyCoder->codeSaoMaxUvlc(absOffset, offsetTh - 1);
            }

            for (i = 0; i < saoLcuParam->length; i++)
            {
                if (saoLcuParam->offset[i] != 0)
                {
                    uint32_t sign = (saoLcuParam->offset[i] < 0) ? 1 : 0;
                    m_entropyCoder->codeSAOSign(sign);
                }
            }

            symbol = (uint32_t)(saoLcuParam->subTypeIdx);
            m_entropyCoder->codeSaoUflc(5, symbol);
        }
        else if (saoLcuParam->typeIdx < 4)
        {
            m_entropyCoder->codeSaoMaxUvlc(saoLcuParam->offset[0], offsetTh - 1);
            m_entropyCoder->codeSaoMaxUvlc(saoLcuParam->offset[1], offsetTh - 1);
            m_entropyCoder->codeSaoMaxUvlc(-saoLcuParam->offset[2], offsetTh - 1);
            m_entropyCoder->codeSaoMaxUvlc(-saoLcuParam->offset[3], offsetTh - 1);
            if (compIdx != 2)
            {
                symbol = (uint32_t)(saoLcuParam->subTypeIdx);
                m_entropyCoder->codeSaoUflc(2, symbol);
            }
        }
    }
}

void TEncEntropy::encodeSaoUnitInterleaving(int compIdx, bool saoFlag, int rx, int ry, SaoLcuParam* saoLcuParam, int cuAddrInSlice, int cuAddrUpInSlice, int allowMergeLeft, int allowMergeUp)
{
    if (saoFlag)
    {
        if (rx > 0 && cuAddrInSlice != 0 && allowMergeLeft)
        {
            m_entropyCoder->codeSaoMerge(saoLcuParam->mergeLeftFlag);
        }
        else
        {
            saoLcuParam->mergeLeftFlag = 0;
        }
        if (saoLcuParam->mergeLeftFlag == 0)
        {
            if ((ry > 0) && (cuAddrUpInSlice >= 0) && allowMergeUp)
            {
                m_entropyCoder->codeSaoMerge(saoLcuParam->mergeUpFlag);
            }
            else
            {
                saoLcuParam->mergeUpFlag = 0;
            }
            if (!saoLcuParam->mergeUpFlag)
            {
                encodeSaoOffset(saoLcuParam, compIdx);
            }
        }
    }
}

//! \}
