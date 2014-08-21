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

/** \file     TComDataCU.cpp
    \brief    CU data structure
    \todo     not all entities are documented
*/

#include "common.h"
#include "TComDataCU.h"
#include "frame.h"
#include "mv.h"

using namespace x265;

static MV scaleMv(MV mv, int scale)
{
    int mvx = Clip3(-32768, 32767, (scale * mv.x + 127 + (scale * mv.x < 0)) >> 8);
    int mvy = Clip3(-32768, 32767, (scale * mv.y + 127 + (scale * mv.y < 0)) >> 8);

    return MV((int16_t)mvx, (int16_t)mvy);
}

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Constructor / destructor / create / destroy
// ====================================================================================================================

TComDataCU::TComDataCU()
{
    /* TODO: can we use memset here? */
    m_pic   = NULL;
    m_slice = NULL;
    m_cuAboveLeft  = NULL;
    m_cuAboveRight = NULL;
    m_cuAbove = NULL;
    m_cuLeft  = NULL;
    m_chromaFormat = 0;
    m_baseQp       = 0;
    m_DataCUMemPool.qpMemBlock             = NULL;
    m_DataCUMemPool.depthMemBlock          = NULL;
    m_DataCUMemPool.log2CUSizeMemBlock     = NULL;
    m_DataCUMemPool.skipFlagMemBlock       = NULL;
    m_DataCUMemPool.partSizeMemBlock       = NULL;
    m_DataCUMemPool.predModeMemBlock       = NULL;
    m_DataCUMemPool.cuTQBypassMemBlock     = NULL;
    m_DataCUMemPool.mergeFlagMemBlock      = NULL;
    m_DataCUMemPool.lumaIntraDirMemBlock   = NULL;
    m_DataCUMemPool.chromaIntraDirMemBlock = NULL;
    m_DataCUMemPool.interDirMemBlock       = NULL;
    m_DataCUMemPool.trIdxMemBlock          = NULL;
    m_DataCUMemPool.transformSkipMemBlock  = NULL;
    m_DataCUMemPool.cbfMemBlock            = NULL;
    m_DataCUMemPool.mvpIdxMemBlock         = NULL;
    m_DataCUMemPool.trCoeffMemBlock        = NULL;
    m_DataCUMemPool.m_tqBypassYuvMemBlock  = NULL;
}

TComDataCU::~TComDataCU()
{}


bool TComDataCU::initialize(uint32_t numPartition, uint32_t sizeL, uint32_t sizeC, uint32_t numBlocks, bool isLossless)
{
    bool ok = true;

    ok &= m_cuMvFieldMemPool.initialize(numPartition, numBlocks);

    CHECKED_MALLOC(m_DataCUMemPool.qpMemBlock, char,  numPartition * numBlocks);

    CHECKED_MALLOC(m_DataCUMemPool.depthMemBlock, uint8_t, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.log2CUSizeMemBlock, uint8_t, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.skipFlagMemBlock, bool, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.partSizeMemBlock, char, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.predModeMemBlock, char, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.cuTQBypassMemBlock, bool, numPartition * numBlocks);

    CHECKED_MALLOC(m_DataCUMemPool.mergeFlagMemBlock, bool,  numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.lumaIntraDirMemBlock, uint8_t, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.chromaIntraDirMemBlock, uint8_t, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.interDirMemBlock, uint8_t, numPartition * numBlocks);

    CHECKED_MALLOC(m_DataCUMemPool.trIdxMemBlock, uint8_t, numPartition * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.transformSkipMemBlock, uint8_t, numPartition * 3 * numBlocks);

    CHECKED_MALLOC(m_DataCUMemPool.cbfMemBlock, uint8_t, numPartition * 3 * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.mvpIdxMemBlock, uint8_t, numPartition * 2 * numBlocks);
    CHECKED_MALLOC(m_DataCUMemPool.trCoeffMemBlock, coeff_t, (sizeL + sizeC * 2) * numBlocks);

    if (isLossless)
        CHECKED_MALLOC(m_DataCUMemPool.m_tqBypassYuvMemBlock, pixel, (sizeL + sizeC * 2) * numBlocks);

    return ok;

fail:
    ok = false;
    return ok;
}

void TComDataCU::create(TComDataCU *cu, uint32_t numPartition, uint32_t cuSize, int csp, int index, bool isLossless)
{
    m_hChromaShift = CHROMA_H_SHIFT(csp);
    m_vChromaShift = CHROMA_V_SHIFT(csp);
    m_chromaFormat = csp;

    m_pic           = NULL;
    m_slice         = NULL;
    m_numPartitions = numPartition;

    uint32_t sizeL = cuSize * cuSize;
    uint32_t sizeC = sizeL >> (m_hChromaShift + m_vChromaShift);

    m_cuMvField[0].create(&cu->m_cuMvFieldMemPool, numPartition, index, 0);
    m_cuMvField[1].create(&cu->m_cuMvFieldMemPool, numPartition, index, 1);

    m_qp                 = cu->m_DataCUMemPool.qpMemBlock             + index * numPartition;
    m_depth              = cu->m_DataCUMemPool.depthMemBlock          + index * numPartition;
    m_log2CUSize         = cu->m_DataCUMemPool.log2CUSizeMemBlock     + index * numPartition;
    m_skipFlag           = cu->m_DataCUMemPool.skipFlagMemBlock       + index * numPartition;
    m_partSizes          = cu->m_DataCUMemPool.partSizeMemBlock       + index * numPartition;
    m_predModes          = cu->m_DataCUMemPool.predModeMemBlock       + index * numPartition;
    m_cuTransquantBypass = cu->m_DataCUMemPool.cuTQBypassMemBlock     + index * numPartition;

    m_bMergeFlags        = cu->m_DataCUMemPool.mergeFlagMemBlock      + index * numPartition;
    m_lumaIntraDir       = cu->m_DataCUMemPool.lumaIntraDirMemBlock   + index * numPartition;
    m_chromaIntraDir     = cu->m_DataCUMemPool.chromaIntraDirMemBlock + index * numPartition;
    m_interDir           = cu->m_DataCUMemPool.interDirMemBlock       + index * numPartition;

    m_trIdx              = cu->m_DataCUMemPool.trIdxMemBlock          + index * numPartition;
    m_transformSkip[0]   = cu->m_DataCUMemPool.transformSkipMemBlock  + index * numPartition * 3;
    m_transformSkip[1]   = m_transformSkip[0]                         + numPartition;
    m_transformSkip[2]   = m_transformSkip[0]                         + numPartition * 2;

    m_cbf[0]             = cu->m_DataCUMemPool.cbfMemBlock            + index * numPartition * 3;
    m_cbf[1]             = m_cbf[0]                                   + numPartition;
    m_cbf[2]             = m_cbf[0]                                   + numPartition * 2;

    m_mvpIdx[0]          = cu->m_DataCUMemPool.mvpIdxMemBlock         + index * numPartition * 2;
    m_mvpIdx[1]          = m_mvpIdx[0]                                + numPartition;

    m_trCoeff[0]         = cu->m_DataCUMemPool.trCoeffMemBlock        + index * (sizeL + sizeC * 2);
    m_trCoeff[1]         = m_trCoeff[0]                               + sizeL;
    m_trCoeff[2]         = m_trCoeff[0]                               + sizeL + sizeC;

    if (isLossless)
    {
        m_tqBypassOrigYuv[0] = cu->m_DataCUMemPool.m_tqBypassYuvMemBlock  + index * (sizeL + sizeC * 2);
        m_tqBypassOrigYuv[1] = m_tqBypassOrigYuv[0]                       + sizeL;
        m_tqBypassOrigYuv[2] = m_tqBypassOrigYuv[0]                       + sizeL + sizeC;
    }

    memset(m_partSizes, SIZE_NONE, numPartition * sizeof(*m_partSizes));
}

void TComDataCU::destroy()
{
    if (m_DataCUMemPool.qpMemBlock)
    {
        X265_FREE(m_DataCUMemPool.qpMemBlock);
        m_DataCUMemPool.qpMemBlock = NULL;
    }

    if (m_DataCUMemPool.depthMemBlock)
    {
        X265_FREE(m_DataCUMemPool.depthMemBlock);
        m_DataCUMemPool.depthMemBlock = NULL;
    }

    if (m_DataCUMemPool.log2CUSizeMemBlock)
    {
        X265_FREE(m_DataCUMemPool.log2CUSizeMemBlock);
        m_DataCUMemPool.log2CUSizeMemBlock = NULL;
    }

    if (m_DataCUMemPool.cbfMemBlock)
    {
        X265_FREE(m_DataCUMemPool.cbfMemBlock);
        m_DataCUMemPool.cbfMemBlock = NULL;
    }

    if (m_DataCUMemPool.interDirMemBlock)
    {
        X265_FREE(m_DataCUMemPool.interDirMemBlock);
        m_DataCUMemPool.interDirMemBlock = NULL;
    }

    if (m_DataCUMemPool.mergeFlagMemBlock)
    {
        X265_FREE(m_DataCUMemPool.mergeFlagMemBlock);
        m_DataCUMemPool.mergeFlagMemBlock = NULL;
    }

    if (m_DataCUMemPool.lumaIntraDirMemBlock)
    {
        X265_FREE(m_DataCUMemPool.lumaIntraDirMemBlock);
        m_DataCUMemPool.lumaIntraDirMemBlock = NULL;
    }

    if(m_DataCUMemPool.chromaIntraDirMemBlock)
    {
        X265_FREE(m_DataCUMemPool.chromaIntraDirMemBlock);
        m_DataCUMemPool.chromaIntraDirMemBlock = NULL;
    }

    if (m_DataCUMemPool.trIdxMemBlock)
    {
        X265_FREE(m_DataCUMemPool.trIdxMemBlock);
        m_DataCUMemPool.trIdxMemBlock = NULL;
    }

    if (m_DataCUMemPool.transformSkipMemBlock)
    {
        X265_FREE(m_DataCUMemPool.transformSkipMemBlock);
        m_DataCUMemPool.transformSkipMemBlock = NULL;
    }

    if (m_DataCUMemPool.trCoeffMemBlock)
    {
        X265_FREE(m_DataCUMemPool.trCoeffMemBlock);
        m_DataCUMemPool.trCoeffMemBlock = NULL;
    }

    if (m_DataCUMemPool.mvpIdxMemBlock)
    {
        X265_FREE(m_DataCUMemPool.mvpIdxMemBlock);
        m_DataCUMemPool.mvpIdxMemBlock = NULL;
    }

    if (m_DataCUMemPool.cuTQBypassMemBlock)
    {
        X265_FREE(m_DataCUMemPool.cuTQBypassMemBlock);
        m_DataCUMemPool.cuTQBypassMemBlock = NULL;
    }

    if (m_DataCUMemPool.skipFlagMemBlock)
    {
        X265_FREE(m_DataCUMemPool.skipFlagMemBlock);
        m_DataCUMemPool.skipFlagMemBlock = NULL;
    }

    if (m_DataCUMemPool.partSizeMemBlock)
    {
        X265_FREE(m_DataCUMemPool.partSizeMemBlock);
        m_DataCUMemPool.partSizeMemBlock = NULL;
    }

    if (m_DataCUMemPool.predModeMemBlock)
    {
        X265_FREE(m_DataCUMemPool.predModeMemBlock);
        m_DataCUMemPool.predModeMemBlock = NULL;
    }

    if (m_DataCUMemPool.m_tqBypassYuvMemBlock)
    {
        X265_FREE(m_DataCUMemPool.m_tqBypassYuvMemBlock);
        m_DataCUMemPool.m_tqBypassYuvMemBlock = NULL;
    }

    m_cuMvFieldMemPool.destroy();
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

// --------------------------------------------------------------------------------------------------------------------
// Initialization
// --------------------------------------------------------------------------------------------------------------------

/**
 - initialize top-level CU
 - internal buffers are already created
 - set values before encoding a CU
 .
 \param  pic     picture (TComPic) class pointer
 \param  cuAddr  CU address
 */
void TComDataCU::initCU(Frame* pic, uint32_t cuAddr)
{
    m_pic              = pic;
    m_slice            = pic->m_picSym->m_slice;
    m_cuAddr           = cuAddr;
    m_cuPelX           = (cuAddr % pic->getFrameWidthInCU()) << g_maxLog2CUSize;
    m_cuPelY           = (cuAddr / pic->getFrameWidthInCU()) << g_maxLog2CUSize;
    m_absIdxInLCU      = 0;
    m_psyEnergy        = 0;
    m_totalPsyCost     = MAX_INT64;
    m_totalRDCost      = MAX_INT64;
    m_sa8dCost         = MAX_INT64;
    m_totalDistortion  = 0;
    m_totalBits        = 0;
    m_mvBits           = 0;
    m_coeffBits        = 0;
    m_numPartitions    = pic->getNumPartInCU();
    char* qp           = pic->getCU(getAddr())->getQP();
    m_baseQp           = pic->getCU(getAddr())->m_baseQp;
    for (int i = 0; i < 4; i++)
    {
        m_avgCost[i] = 0;
        m_count[i] = 0;
    }

    X265_CHECK(m_numPartitions > 0, "unexpected partition count\n");

    memset(m_skipFlag,           false,         m_numPartitions * sizeof(*m_skipFlag));
    memset(m_predModes,          MODE_NONE,     m_numPartitions * sizeof(*m_predModes));
    memset(m_partSizes,          SIZE_NONE,     m_numPartitions * sizeof(*m_partSizes));
    memset(m_cuTransquantBypass, false,         m_numPartitions * sizeof(*m_cuTransquantBypass));
    memset(m_depth,              0,             m_numPartitions * sizeof(*m_depth));
    memset(m_trIdx,              0,             m_numPartitions * sizeof(*m_trIdx));
    memset(m_transformSkip[0],   0,             m_numPartitions * sizeof(*m_transformSkip[0]));
    memset(m_transformSkip[1],   0,             m_numPartitions * sizeof(*m_transformSkip[1]));
    memset(m_transformSkip[2],   0,             m_numPartitions * sizeof(*m_transformSkip[2]));
    memset(m_log2CUSize,         g_maxLog2CUSize, m_numPartitions * sizeof(*m_log2CUSize));
    memset(m_bMergeFlags,        false,         m_numPartitions * sizeof(*m_bMergeFlags));
    memset(m_lumaIntraDir,       DC_IDX,        m_numPartitions * sizeof(*m_lumaIntraDir));
    memset(m_chromaIntraDir,     0,             m_numPartitions * sizeof(*m_chromaIntraDir));
    memset(m_interDir,           0,             m_numPartitions * sizeof(*m_interDir));
    memset(m_cbf[0],             0,             m_numPartitions * sizeof(*m_cbf[0]));
    memset(m_cbf[1],             0,             m_numPartitions * sizeof(*m_cbf[1]));
    memset(m_cbf[2],             0,             m_numPartitions * sizeof(*m_cbf[2]));
    if (qp != m_qp)
        memcpy(m_qp,             qp,            m_numPartitions * sizeof(*m_qp));

    m_cuMvField[0].clearMvField();
    m_cuMvField[1].clearMvField();

    if (m_slice->m_pps->bTransquantBypassEnabled)
    {
        uint32_t y_tmp = 1 << (g_maxLog2CUSize * 2);
        uint32_t c_tmp = 1 << (g_maxLog2CUSize * 2 - m_hChromaShift - m_vChromaShift);
        memset(m_tqBypassOrigYuv[0], 0, sizeof(pixel) * y_tmp);
        memset(m_tqBypassOrigYuv[1], 0, sizeof(pixel) * c_tmp);
        memset(m_tqBypassOrigYuv[2], 0, sizeof(pixel) * c_tmp);
    }

    uint32_t widthInCU = pic->getFrameWidthInCU();
    m_cuLeft = (m_cuAddr % widthInCU) ? pic->getCU(m_cuAddr - 1) : NULL;
    m_cuAbove = (m_cuAddr / widthInCU) ? pic->getCU(m_cuAddr - widthInCU) : NULL;
    m_cuAboveLeft = (m_cuLeft && m_cuAbove) ? pic->getCU(m_cuAddr - widthInCU - 1) : NULL;
    m_cuAboveRight = (m_cuAbove && ((m_cuAddr % widthInCU) < (widthInCU - 1))) ? pic->getCU(m_cuAddr - widthInCU + 1) : NULL;
}

// initialize prediction data
void TComDataCU::initEstData()
{
    m_psyEnergy        = 0;
    m_totalPsyCost     = MAX_INT64;
    m_totalRDCost      = MAX_INT64;
    m_sa8dCost         = MAX_INT64;
    m_totalDistortion  = 0;
    m_totalBits        = 0;
    m_mvBits           = 0;
    m_coeffBits        = 0;

    m_cuMvField[0].clearMvField();
    m_cuMvField[1].clearMvField();
}

// initialize Sub partition
void TComDataCU::initSubCU(TComDataCU* cu, uint32_t partUnitIdx, uint32_t depth, int qp)
{
    X265_CHECK(partUnitIdx < 4, "part unit should be less than 4\n");
    uint8_t log2CUSize = g_maxLog2CUSize - depth;
    uint32_t partOffset = (cu->getTotalNumPart() >> 2) * partUnitIdx;

    m_pic              = cu->m_pic;
    m_slice            = cu->m_slice;
    m_cuAddr           = cu->getAddr();
    m_absIdxInLCU      = cu->getZorderIdxInCU() + partOffset;

    m_cuPelX           = cu->getCUPelX() + ((partUnitIdx &  1) << log2CUSize);
    m_cuPelY           = cu->getCUPelY() + ((partUnitIdx >> 1) << log2CUSize);

    m_psyEnergy        = 0;
    m_totalPsyCost     = MAX_INT64;
    m_totalRDCost      = MAX_INT64;
    m_sa8dCost         = MAX_INT64;
    m_totalDistortion  = 0;
    m_totalBits        = 0;
    m_mvBits           = 0;
    m_coeffBits        = 0;

    m_numPartitions    = cu->getTotalNumPart() >> 2;

    for (int i = 0; i < 4; i++)
    {
        m_avgCost[i] = cu->m_avgCost[i];
        m_count[i] = cu->m_count[i];
    }

    int sizeInBool = sizeof(bool) * m_numPartitions;
    int sizeInChar = sizeof(char) * m_numPartitions;

    memset(m_qp,                 qp,     sizeInChar);
    memset(m_lumaIntraDir,       DC_IDX, sizeInChar);
    memset(m_chromaIntraDir,     0,      sizeInChar);
    memset(m_trIdx,              0,      sizeInChar);
    memset(m_transformSkip[0],   0,      sizeInChar);
    memset(m_transformSkip[1],   0,      sizeInChar);
    memset(m_transformSkip[2],   0,      sizeInChar);
    memset(m_cbf[0],             0,      sizeInChar);
    memset(m_cbf[1],             0,      sizeInChar);
    memset(m_cbf[2],             0,      sizeInChar);
    memset(m_depth,              depth,  sizeInChar);
    memset(m_log2CUSize,         log2CUSize, sizeInChar);
    memset(m_partSizes,          SIZE_NONE, sizeInChar);
    memset(m_predModes,          MODE_NONE, sizeInChar);
    memset(m_skipFlag,           false, sizeInBool);
    memset(m_cuTransquantBypass, false, sizeInBool);

    if (m_slice->m_sliceType != I_SLICE)
    {
        memset(m_bMergeFlags,      0, sizeInBool);
        memset(m_interDir,         0, sizeInChar);

        m_cuMvField[0].clearMvField();
        m_cuMvField[1].clearMvField();
    }

    m_cuLeft        = cu->getCULeft();
    m_cuAbove       = cu->getCUAbove();
    m_cuAboveLeft   = cu->getCUAboveLeft();
    m_cuAboveRight  = cu->getCUAboveRight();
}

void TComDataCU::copyToSubCU(TComDataCU* cu, uint32_t partUnitIdx, uint32_t depth)
{
    X265_CHECK(partUnitIdx < 4, "part unit should be less than 4\n");

    uint32_t partOffset = (cu->getTotalNumPart() >> 2) * partUnitIdx;

    m_pic              = cu->m_pic;
    m_slice            = cu->m_slice;
    m_cuAddr           = cu->getAddr();
    m_absIdxInLCU      = cu->getZorderIdxInCU() + partOffset;

    m_cuPelX           = cu->getCUPelX() + ((partUnitIdx &  1) << (g_maxLog2CUSize - depth));
    m_cuPelY           = cu->getCUPelY() + ((partUnitIdx >> 1) << (g_maxLog2CUSize - depth));

    m_psyEnergy        = 0;
    m_totalPsyCost     = MAX_INT64;
    m_totalRDCost      = MAX_INT64;
    m_sa8dCost         = MAX_INT64;
    m_totalDistortion  = 0;
    m_totalBits        = 0;
    m_mvBits           = 0;
    m_coeffBits        = 0;
    m_numPartitions    = cu->getTotalNumPart() >> 2;

    TComDataCU* otherCU = m_pic->getCU(m_cuAddr);
    int sizeInChar  = sizeof(char) * m_numPartitions;

    memcpy(m_skipFlag, otherCU->getSkipFlag() + m_absIdxInLCU, sizeof(*m_skipFlag) * m_numPartitions);
    memcpy(m_qp, otherCU->getQP() + m_absIdxInLCU, sizeInChar);

    memcpy(m_partSizes, otherCU->getPartitionSize() + m_absIdxInLCU, sizeof(*m_partSizes) * m_numPartitions);
    memcpy(m_predModes, otherCU->getPredictionMode() + m_absIdxInLCU, sizeof(*m_predModes) * m_numPartitions);

    memcpy(m_lumaIntraDir, otherCU->getLumaIntraDir() + m_absIdxInLCU, sizeInChar);
    memcpy(m_depth, otherCU->getDepth() + m_absIdxInLCU, sizeInChar);
    memcpy(m_log2CUSize, otherCU->getLog2CUSize() + m_absIdxInLCU, sizeInChar);
}

// --------------------------------------------------------------------------------------------------------------------
// Copy
// --------------------------------------------------------------------------------------------------------------------

// Copy small CU to bigger CU.
// One of quarter parts overwritten by predicted sub part.
void TComDataCU::copyPartFrom(TComDataCU* cu, uint32_t partUnitIdx, uint32_t depth, bool isRDObasedAnalysis)
{
    X265_CHECK(partUnitIdx < 4, "part unit should be less than 4\n");
    if (isRDObasedAnalysis)
    {
        m_totalPsyCost += cu->m_totalPsyCost;
        m_totalRDCost  += cu->m_totalRDCost;
    }

    m_psyEnergy        += cu->m_psyEnergy;
    m_totalDistortion  += cu->m_totalDistortion;
    m_totalBits        += cu->m_totalBits;
    m_mvBits           += cu->m_mvBits;
    m_coeffBits        += cu->m_coeffBits;

    uint32_t offset       = cu->getTotalNumPart() * partUnitIdx;
    uint32_t numPartition = cu->getTotalNumPart();
    int sizeInBool  = sizeof(bool) * numPartition;
    int sizeInChar  = sizeof(char) * numPartition;
    memcpy(m_skipFlag  + offset, cu->getSkipFlag(),       sizeof(*m_skipFlag)   * numPartition);
    memcpy(m_qp        + offset, cu->getQP(),             sizeInChar);
    memcpy(m_partSizes + offset, cu->getPartitionSize(),  sizeof(*m_partSizes) * numPartition);
    memcpy(m_predModes + offset, cu->getPredictionMode(), sizeof(*m_predModes) * numPartition);
    memcpy(m_cuTransquantBypass + offset, cu->getCUTransquantBypass(), sizeof(*m_cuTransquantBypass) * numPartition);
    memcpy(m_bMergeFlags      + offset, cu->getMergeFlag(),         sizeInBool);
    memcpy(m_lumaIntraDir     + offset, cu->getLumaIntraDir(),      sizeInChar);
    memcpy(m_chromaIntraDir   + offset, cu->getChromaIntraDir(),    sizeInChar);
    memcpy(m_interDir         + offset, cu->getInterDir(),          sizeInChar);
    memcpy(m_trIdx            + offset, cu->getTransformIdx(),      sizeInChar);
    memcpy(m_transformSkip[0] + offset, cu->getTransformSkip(TEXT_LUMA),     sizeInChar);
    memcpy(m_transformSkip[1] + offset, cu->getTransformSkip(TEXT_CHROMA_U), sizeInChar);
    memcpy(m_transformSkip[2] + offset, cu->getTransformSkip(TEXT_CHROMA_V), sizeInChar);

    memcpy(m_cbf[0] + offset, cu->getCbf(TEXT_LUMA), sizeInChar);
    memcpy(m_cbf[1] + offset, cu->getCbf(TEXT_CHROMA_U), sizeInChar);
    memcpy(m_cbf[2] + offset, cu->getCbf(TEXT_CHROMA_V), sizeInChar);

    memcpy(m_depth  + offset, cu->getDepth(),  sizeInChar);
    memcpy(m_log2CUSize + offset, cu->getLog2CUSize(), sizeInChar);

    memcpy(m_mvpIdx[0] + offset, cu->getMVPIdx(REF_PIC_LIST_0), sizeInChar);
    memcpy(m_mvpIdx[1] + offset, cu->getMVPIdx(REF_PIC_LIST_1), sizeInChar);

    m_cuAboveLeft      = cu->getCUAboveLeft();
    m_cuAboveRight     = cu->getCUAboveRight();
    m_cuAbove          = cu->getCUAbove();
    m_cuLeft           = cu->getCULeft();

    m_cuMvField[0].copyFrom(cu->getCUMvField(REF_PIC_LIST_0), cu->getTotalNumPart(), offset);
    m_cuMvField[1].copyFrom(cu->getCUMvField(REF_PIC_LIST_1), cu->getTotalNumPart(), offset);

    uint32_t tmp  = 1 << ((g_maxLog2CUSize - depth) * 2);
    uint32_t tmp2 = partUnitIdx * tmp;
    memcpy(m_trCoeff[0] + tmp2, cu->getCoeffY(), sizeof(coeff_t) * tmp);

    uint32_t tmpC  = tmp  >> (m_hChromaShift + m_vChromaShift);
    uint32_t tmpC2 = tmp2 >> (m_hChromaShift + m_vChromaShift);
    memcpy(m_trCoeff[1] + tmpC2, cu->m_trCoeff[1], sizeof(coeff_t) * tmpC);
    memcpy(m_trCoeff[2] + tmpC2, cu->m_trCoeff[2], sizeof(coeff_t) * tmpC);

    if (m_slice->m_pps->bTransquantBypassEnabled)
    {
        memcpy(m_tqBypassOrigYuv[0] + tmp2, cu->getLumaOrigYuv(), sizeof(pixel) * tmp);

        memcpy(m_tqBypassOrigYuv[1] + tmpC2, cu->getChromaOrigYuv(1), sizeof(pixel) * tmpC);
        memcpy(m_tqBypassOrigYuv[2] + tmpC2, cu->getChromaOrigYuv(2), sizeof(pixel) * tmpC);
    }
}

// Copy current predicted part to a CU in picture.
// It is used to predict for next part
void TComDataCU::copyToPic(uint32_t depth)
{
    TComDataCU* cu = m_pic->getCU(m_cuAddr);

    cu->m_psyEnergy       = m_psyEnergy;
    cu->m_totalPsyCost    = m_totalPsyCost;
    cu->m_totalRDCost     = m_totalRDCost;
    cu->m_totalDistortion = m_totalDistortion;
    cu->m_totalBits       = m_totalBits;
    cu->m_mvBits          = m_mvBits;
    cu->m_coeffBits       = m_coeffBits;

    int sizeInBool  = sizeof(bool) * m_numPartitions;
    int sizeInChar  = sizeof(char) * m_numPartitions;

    memcpy(cu->getSkipFlag() + m_absIdxInLCU, m_skipFlag, sizeof(*m_skipFlag) * m_numPartitions);

    memcpy(cu->getQP() + m_absIdxInLCU, m_qp, sizeInChar);

    memcpy(cu->getPartitionSize()  + m_absIdxInLCU, m_partSizes, sizeof(*m_partSizes) * m_numPartitions);
    memcpy(cu->getPredictionMode() + m_absIdxInLCU, m_predModes, sizeof(*m_predModes) * m_numPartitions);
    memcpy(cu->getCUTransquantBypass() + m_absIdxInLCU, m_cuTransquantBypass, sizeof(*m_cuTransquantBypass) * m_numPartitions);
    memcpy(cu->getMergeFlag()         + m_absIdxInLCU, m_bMergeFlags,      sizeInBool);
    memcpy(cu->getLumaIntraDir()      + m_absIdxInLCU, m_lumaIntraDir,     sizeInChar);
    memcpy(cu->getChromaIntraDir()    + m_absIdxInLCU, m_chromaIntraDir,   sizeInChar);
    memcpy(cu->getInterDir()          + m_absIdxInLCU, m_interDir,         sizeInChar);
    memcpy(cu->getTransformIdx()      + m_absIdxInLCU, m_trIdx,            sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_LUMA)     + m_absIdxInLCU, m_transformSkip[0], sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_CHROMA_U) + m_absIdxInLCU, m_transformSkip[1], sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_CHROMA_V) + m_absIdxInLCU, m_transformSkip[2], sizeInChar);

    memcpy(cu->getCbf(TEXT_LUMA)     + m_absIdxInLCU, m_cbf[0], sizeInChar);
    memcpy(cu->getCbf(TEXT_CHROMA_U) + m_absIdxInLCU, m_cbf[1], sizeInChar);
    memcpy(cu->getCbf(TEXT_CHROMA_V) + m_absIdxInLCU, m_cbf[2], sizeInChar);

    memcpy(cu->getDepth()  + m_absIdxInLCU, m_depth,  sizeInChar);
    memcpy(cu->getLog2CUSize() + m_absIdxInLCU, m_log2CUSize, sizeInChar);

    memcpy(cu->getMVPIdx(REF_PIC_LIST_0) + m_absIdxInLCU, m_mvpIdx[0], sizeInChar);
    memcpy(cu->getMVPIdx(REF_PIC_LIST_1) + m_absIdxInLCU, m_mvpIdx[1], sizeInChar);

    m_cuMvField[0].copyTo(cu->getCUMvField(REF_PIC_LIST_0), m_absIdxInLCU);
    m_cuMvField[1].copyTo(cu->getCUMvField(REF_PIC_LIST_1), m_absIdxInLCU);

    uint32_t tmpY  = 1 << ((g_maxLog2CUSize - depth) * 2);
    uint32_t tmpY2 = m_absIdxInLCU << LOG2_UNIT_SIZE * 2;
    memcpy(cu->getCoeffY() + tmpY2, m_trCoeff[0], sizeof(coeff_t) * tmpY);

    uint32_t tmpC  = tmpY  >> (m_hChromaShift + m_vChromaShift);
    uint32_t tmpC2 = tmpY2 >> (m_hChromaShift + m_vChromaShift);
    memcpy(cu->m_trCoeff[1] + tmpC2, m_trCoeff[1], sizeof(coeff_t) * tmpC);
    memcpy(cu->m_trCoeff[2] + tmpC2, m_trCoeff[2], sizeof(coeff_t) * tmpC);

    if (m_slice->m_pps->bTransquantBypassEnabled)
    {
        uint32_t tmp  = 1 << ((g_maxLog2CUSize - depth) * 2);
        uint32_t tmp2 = m_absIdxInLCU << LOG2_UNIT_SIZE * 2;
        memcpy(cu->getLumaOrigYuv() + tmp2, m_tqBypassOrigYuv[0], sizeof(pixel) * tmp);

        memcpy(cu->getChromaOrigYuv(1) + tmpC2, m_tqBypassOrigYuv[1], sizeof(pixel) * tmpC);
        memcpy(cu->getChromaOrigYuv(2) + tmpC2, m_tqBypassOrigYuv[2], sizeof(pixel) * tmpC);
    }
}

void TComDataCU::copyCodedToPic(uint32_t depth)
{
    TComDataCU* cu = m_pic->getCU(m_cuAddr);

    int sizeInChar  = sizeof(uint8_t) * m_numPartitions;

    memcpy(cu->getSkipFlag() + m_absIdxInLCU, m_skipFlag, sizeof(*m_skipFlag) * m_numPartitions);
    memcpy(cu->getTransformIdx() + m_absIdxInLCU, m_trIdx, sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_LUMA) + m_absIdxInLCU, m_transformSkip[0], sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_CHROMA_U) + m_absIdxInLCU, m_transformSkip[1], sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_CHROMA_V) + m_absIdxInLCU, m_transformSkip[2], sizeInChar);
    memcpy(cu->getChromaIntraDir() + m_absIdxInLCU, m_chromaIntraDir, sizeInChar);
    memcpy(cu->getQP() + m_absIdxInLCU, m_qp, sizeof(char) * m_numPartitions);

    memcpy(cu->getCbf(TEXT_LUMA) + m_absIdxInLCU, m_cbf[0], sizeInChar);
    memcpy(cu->getCbf(TEXT_CHROMA_U) + m_absIdxInLCU, m_cbf[1], sizeInChar);
    memcpy(cu->getCbf(TEXT_CHROMA_V) + m_absIdxInLCU, m_cbf[2], sizeInChar);

    uint32_t tmpY  = 1 << ((g_maxLog2CUSize - depth) * 2);
    uint32_t tmpY2 = m_absIdxInLCU << LOG2_UNIT_SIZE * 2;
    memcpy(cu->getCoeffY() + tmpY2, m_trCoeff[0], sizeof(coeff_t) * tmpY);
    tmpY  >>= m_hChromaShift + m_vChromaShift;
    tmpY2 >>= m_hChromaShift + m_vChromaShift;
    memcpy(cu->m_trCoeff[1] + tmpY2, m_trCoeff[1], sizeof(coeff_t) * tmpY);
    memcpy(cu->m_trCoeff[2] + tmpY2, m_trCoeff[2], sizeof(coeff_t) * tmpY);
}

void TComDataCU::copyToPic(uint32_t depth, uint32_t partIdx, uint32_t partDepth)
{
    TComDataCU* cu = m_pic->getCU(m_cuAddr);
    uint32_t qNumPart  = m_numPartitions >> (partDepth << 1);

    uint32_t partStart = partIdx * qNumPart;
    uint32_t partOffset  = m_absIdxInLCU + partStart;

    cu->m_psyEnergy       = m_psyEnergy;
    cu->m_totalPsyCost    = m_totalPsyCost;
    cu->m_totalRDCost     = m_totalRDCost;
    cu->m_totalDistortion = m_totalDistortion;
    cu->m_totalBits       = m_totalBits;
    cu->m_mvBits          = m_mvBits;
    cu->m_coeffBits       = m_coeffBits;

    int sizeInBool  = sizeof(bool) * qNumPart;
    int sizeInChar  = sizeof(char) * qNumPart;
    memcpy(cu->getSkipFlag() + partOffset, m_skipFlag,   sizeof(*m_skipFlag)   * qNumPart);

    memcpy(cu->getQP() + partOffset, m_qp, sizeInChar);
    memcpy(cu->getPartitionSize()  + partOffset, m_partSizes, sizeof(*m_partSizes) * qNumPart);
    memcpy(cu->getPredictionMode() + partOffset, m_predModes, sizeof(*m_predModes) * qNumPart);
    memcpy(cu->getCUTransquantBypass() + partOffset, m_cuTransquantBypass, sizeof(*m_cuTransquantBypass) * qNumPart);
    memcpy(cu->getMergeFlag()         + partOffset, m_bMergeFlags,      sizeInBool);
    memcpy(cu->getLumaIntraDir()      + partOffset, m_lumaIntraDir,     sizeInChar);
    memcpy(cu->getChromaIntraDir()    + partOffset, m_chromaIntraDir,   sizeInChar);
    memcpy(cu->getInterDir()          + partOffset, m_interDir,         sizeInChar);
    memcpy(cu->getTransformIdx()      + partOffset, m_trIdx,            sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_LUMA)     + partOffset, m_transformSkip[0], sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_CHROMA_U) + partOffset, m_transformSkip[1], sizeInChar);
    memcpy(cu->getTransformSkip(TEXT_CHROMA_V) + partOffset, m_transformSkip[2], sizeInChar);
    memcpy(cu->getCbf(TEXT_LUMA)     + partOffset, m_cbf[0], sizeInChar);
    memcpy(cu->getCbf(TEXT_CHROMA_U) + partOffset, m_cbf[1], sizeInChar);
    memcpy(cu->getCbf(TEXT_CHROMA_V) + partOffset, m_cbf[2], sizeInChar);

    memcpy(cu->getDepth()  + partOffset, m_depth,  sizeInChar);
    memcpy(cu->getLog2CUSize() + partOffset, m_log2CUSize, sizeInChar);

    memcpy(cu->getMVPIdx(REF_PIC_LIST_0) + partOffset, m_mvpIdx[0], sizeInChar);
    memcpy(cu->getMVPIdx(REF_PIC_LIST_1) + partOffset, m_mvpIdx[1], sizeInChar);
    m_cuMvField[0].copyTo(cu->getCUMvField(REF_PIC_LIST_0), m_absIdxInLCU, partStart, qNumPart);
    m_cuMvField[1].copyTo(cu->getCUMvField(REF_PIC_LIST_1), m_absIdxInLCU, partStart, qNumPart);

    uint32_t tmpY  = 1 << ((g_maxLog2CUSize - depth - partDepth) * 2);
    uint32_t tmpY2 = partOffset << LOG2_UNIT_SIZE * 2;
    memcpy(cu->getCoeffY() + tmpY2, m_trCoeff[0],  sizeof(coeff_t) * tmpY);

    uint32_t tmpC  = tmpY >> (m_hChromaShift + m_vChromaShift);
    uint32_t tmpC2 = tmpY2 >> (m_hChromaShift + m_vChromaShift);
    memcpy(cu->m_trCoeff[1] + tmpC2, m_trCoeff[1], sizeof(coeff_t) * tmpC);
    memcpy(cu->m_trCoeff[2] + tmpC2, m_trCoeff[2], sizeof(coeff_t) * tmpC);

    if (m_slice->m_pps->bTransquantBypassEnabled)
    {
        memcpy(cu->getLumaOrigYuv() + tmpY2, m_tqBypassOrigYuv[0], sizeof(pixel) * tmpY);

        memcpy(cu->getChromaOrigYuv(1) + tmpC2, m_tqBypassOrigYuv[1], sizeof(pixel) * tmpC);
        memcpy(cu->getChromaOrigYuv(2) + tmpC2, m_tqBypassOrigYuv[2], sizeof(pixel) * tmpC);
    }
}

// --------------------------------------------------------------------------------------------------------------------
// Other public functions
// --------------------------------------------------------------------------------------------------------------------

TComDataCU* TComDataCU::getPULeft(uint32_t& lPartUnitIdx, uint32_t curPartUnitIdx)
{
    uint32_t absPartIdx       = g_zscanToRaster[curPartUnitIdx];
    uint32_t numPartInCUSize  = m_pic->getNumPartInCUSize();

    if (!RasterAddress::isZeroCol(absPartIdx, numPartInCUSize))
    {
        uint32_t absZorderCUIdx   = g_zscanToRaster[m_absIdxInLCU];
        lPartUnitIdx = g_rasterToZscan[absPartIdx - 1];
        if (RasterAddress::isEqualCol(absPartIdx, absZorderCUIdx, numPartInCUSize))
        {
            return m_pic->getCU(getAddr());
        }
        else
        {
            lPartUnitIdx -= m_absIdxInLCU;
            return this;
        }
    }

    lPartUnitIdx = g_rasterToZscan[absPartIdx + numPartInCUSize - 1];
    return m_cuLeft;
}

TComDataCU* TComDataCU::getPUAbove(uint32_t& aPartUnitIdx, uint32_t curPartUnitIdx, bool planarAtLCUBoundary)
{
    uint32_t absPartIdx       = g_zscanToRaster[curPartUnitIdx];
    uint32_t numPartInCUSize  = m_pic->getNumPartInCUSize();

    if (!RasterAddress::isZeroRow(absPartIdx, numPartInCUSize))
    {
        uint32_t absZorderCUIdx   = g_zscanToRaster[m_absIdxInLCU];
        aPartUnitIdx = g_rasterToZscan[absPartIdx - numPartInCUSize];
        if (RasterAddress::isEqualRow(absPartIdx, absZorderCUIdx, numPartInCUSize))
        {
            return m_pic->getCU(getAddr());
        }
        else
        {
            aPartUnitIdx -= m_absIdxInLCU;
            return this;
        }
    }

    if (planarAtLCUBoundary)
        return NULL;

    aPartUnitIdx = g_rasterToZscan[absPartIdx + m_pic->getNumPartInCU() - numPartInCUSize];
    return m_cuAbove;
}

TComDataCU* TComDataCU::getPUAboveLeft(uint32_t& alPartUnitIdx, uint32_t curPartUnitIdx)
{
    uint32_t absPartIdx      = g_zscanToRaster[curPartUnitIdx];
    uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();

    if (!RasterAddress::isZeroCol(absPartIdx, numPartInCUSize))
    {
        if (!RasterAddress::isZeroRow(absPartIdx, numPartInCUSize))
        {
            uint32_t absZorderCUIdx  = g_zscanToRaster[m_absIdxInLCU];
            alPartUnitIdx = g_rasterToZscan[absPartIdx - numPartInCUSize - 1];
            if (RasterAddress::isEqualRowOrCol(absPartIdx, absZorderCUIdx, numPartInCUSize))
            {
                return m_pic->getCU(getAddr());
            }
            else
            {
                alPartUnitIdx -= m_absIdxInLCU;
                return this;
            }
        }
        alPartUnitIdx = g_rasterToZscan[absPartIdx + m_pic->getNumPartInCU() - numPartInCUSize - 1];
        return m_cuAbove;
    }

    if (!RasterAddress::isZeroRow(absPartIdx, numPartInCUSize))
    {
        alPartUnitIdx = g_rasterToZscan[absPartIdx - 1];
        return m_cuLeft;
    }

    alPartUnitIdx = g_rasterToZscan[m_pic->getNumPartInCU() - 1];
    return m_cuAboveLeft;
}

TComDataCU* TComDataCU::getPUAboveRight(uint32_t& arPartUnitIdx, uint32_t curPartUnitIdx)
{
    uint32_t absPartIdxRT    = g_zscanToRaster[curPartUnitIdx];
    uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();

    if ((m_pic->getCU(m_cuAddr)->getCUPelX() + g_rasterToPelX[absPartIdxRT] + UNIT_SIZE) >= m_slice->m_sps->picWidthInLumaSamples)
        return NULL;

    if (RasterAddress::lessThanCol(absPartIdxRT, numPartInCUSize - 1, numPartInCUSize))
    {
        if (!RasterAddress::isZeroRow(absPartIdxRT, numPartInCUSize))
        {
            if (curPartUnitIdx > g_rasterToZscan[absPartIdxRT - numPartInCUSize + 1])
            {
                uint32_t absZorderCUIdx  = g_zscanToRaster[m_absIdxInLCU] + (1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE)) - 1;
                arPartUnitIdx = g_rasterToZscan[absPartIdxRT - numPartInCUSize + 1];
                if (RasterAddress::isEqualRowOrCol(absPartIdxRT, absZorderCUIdx, numPartInCUSize))
                {
                    return m_pic->getCU(getAddr());
                }
                else
                {
                    arPartUnitIdx -= m_absIdxInLCU;
                    return this;
                }
            }
            return NULL;
        }
        arPartUnitIdx = g_rasterToZscan[absPartIdxRT + m_pic->getNumPartInCU() - numPartInCUSize + 1];
        return m_cuAbove;
    }

    if (!RasterAddress::isZeroRow(absPartIdxRT, numPartInCUSize))
    {
        return NULL;
    }

    arPartUnitIdx = g_rasterToZscan[m_pic->getNumPartInCU() - numPartInCUSize];
    return m_cuAboveRight;
}

TComDataCU* TComDataCU::getPUBelowLeft(uint32_t& blPartUnitIdx, uint32_t curPartUnitIdx)
{
    uint32_t absPartIdxLB     = g_zscanToRaster[curPartUnitIdx];

    if ((m_pic->getCU(m_cuAddr)->getCUPelY() + g_rasterToPelY[absPartIdxLB] + UNIT_SIZE) >= m_slice->m_sps->picHeightInLumaSamples)
    {
        return NULL;
    }

    uint32_t numPartInCUSize  = m_pic->getNumPartInCUSize();

    if (RasterAddress::lessThanRow(absPartIdxLB, numPartInCUSize - 1, numPartInCUSize))
    {
        if (!RasterAddress::isZeroCol(absPartIdxLB, numPartInCUSize))
        {
            if (curPartUnitIdx > g_rasterToZscan[absPartIdxLB + numPartInCUSize - 1])
            {
                uint32_t absZorderCUIdxLB = g_zscanToRaster[m_absIdxInLCU] + ((1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE)) - 1) * m_pic->getNumPartInCUSize();
                blPartUnitIdx = g_rasterToZscan[absPartIdxLB + numPartInCUSize - 1];
                if (RasterAddress::isEqualRowOrCol(absPartIdxLB, absZorderCUIdxLB, numPartInCUSize))
                {
                    return m_pic->getCU(getAddr());
                }
                else
                {
                    blPartUnitIdx -= m_absIdxInLCU;
                    return this;
                }
            }
            return NULL;
        }
        blPartUnitIdx = g_rasterToZscan[absPartIdxLB + numPartInCUSize * 2 - 1];
        return m_cuLeft;
    }

    return NULL;
}

TComDataCU* TComDataCU::getPUBelowLeftAdi(uint32_t& blPartUnitIdx,  uint32_t curPartUnitIdx, uint32_t partUnitOffset)
{
    uint32_t absPartIdxLB     = g_zscanToRaster[curPartUnitIdx];

    if ((m_pic->getCU(m_cuAddr)->getCUPelY() + g_rasterToPelY[absPartIdxLB] + (partUnitOffset << LOG2_UNIT_SIZE)) >=
        m_slice->m_sps->picHeightInLumaSamples)
    {
        return NULL;
    }

    uint32_t numPartInCUSize  = m_pic->getNumPartInCUSize();

    if (RasterAddress::lessThanRow(absPartIdxLB, numPartInCUSize - partUnitOffset, numPartInCUSize))
    {
        if (!RasterAddress::isZeroCol(absPartIdxLB, numPartInCUSize))
        {
            if (curPartUnitIdx > g_rasterToZscan[absPartIdxLB + partUnitOffset * numPartInCUSize - 1])
            {
                uint32_t absZorderCUIdxLB = g_zscanToRaster[m_absIdxInLCU] + ((1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE)) - 1) * m_pic->getNumPartInCUSize();
                blPartUnitIdx = g_rasterToZscan[absPartIdxLB + partUnitOffset * numPartInCUSize - 1];
                if (RasterAddress::isEqualRowOrCol(absPartIdxLB, absZorderCUIdxLB, numPartInCUSize))
                {
                    return m_pic->getCU(getAddr());
                }
                else
                {
                    blPartUnitIdx -= m_absIdxInLCU;
                    return this;
                }
            }
            return NULL;
        }
        blPartUnitIdx = g_rasterToZscan[absPartIdxLB + (1 + partUnitOffset) * numPartInCUSize - 1];
        if (m_cuLeft == NULL || m_cuLeft->m_slice == NULL)
        {
            return NULL;
        }
        return m_cuLeft;
    }

    return NULL;
}

TComDataCU* TComDataCU::getPUAboveRightAdi(uint32_t& arPartUnitIdx, uint32_t curPartUnitIdx, uint32_t partUnitOffset)
{
    uint32_t absPartIdxRT    = g_zscanToRaster[curPartUnitIdx];

    if ((m_pic->getCU(m_cuAddr)->getCUPelX() + g_rasterToPelX[absPartIdxRT] + (partUnitOffset << LOG2_UNIT_SIZE)) >=
        m_slice->m_sps->picWidthInLumaSamples)
    {
        return NULL;
    }

    uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();

    if (RasterAddress::lessThanCol(absPartIdxRT, numPartInCUSize - partUnitOffset, numPartInCUSize))
    {
        if (!RasterAddress::isZeroRow(absPartIdxRT, numPartInCUSize))
        {
            if (curPartUnitIdx > g_rasterToZscan[absPartIdxRT - numPartInCUSize + partUnitOffset])
            {
                uint32_t absZorderCUIdx  = g_zscanToRaster[m_absIdxInLCU] + (1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE)) - 1;
                arPartUnitIdx = g_rasterToZscan[absPartIdxRT - numPartInCUSize + partUnitOffset];
                if (RasterAddress::isEqualRowOrCol(absPartIdxRT, absZorderCUIdx, numPartInCUSize))
                {
                    return m_pic->getCU(getAddr());
                }
                else
                {
                    arPartUnitIdx -= m_absIdxInLCU;
                    return this;
                }
            }
            return NULL;
        }
        arPartUnitIdx = g_rasterToZscan[absPartIdxRT + m_pic->getNumPartInCU() - numPartInCUSize + partUnitOffset];
        if (m_cuAbove == NULL || m_cuAbove->m_slice == NULL)
        {
            return NULL;
        }
        return m_cuAbove;
    }

    if (!RasterAddress::isZeroRow(absPartIdxRT, numPartInCUSize))
    {
        return NULL;
    }

    arPartUnitIdx = g_rasterToZscan[m_pic->getNumPartInCU() - numPartInCUSize + partUnitOffset - 1];
    if ((m_cuAboveRight == NULL || m_cuAboveRight->m_slice == NULL ||
         (m_cuAboveRight->getAddr()) > getAddr()))
    {
        return NULL;
    }
    return m_cuAboveRight;
}

/** Get left QpMinCu
*\param   lPartUnitIdx
*\param   curAbsIdxInLCU
*\returns TComDataCU*   point of TComDataCU of left QpMinCu
*/
TComDataCU* TComDataCU::getQpMinCuLeft(uint32_t& lPartUnitIdx, uint32_t curAbsIdxInLCU)
{
    uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();
    uint32_t absZorderQpMinCUIdx = curAbsIdxInLCU & (0xFF << (g_maxFullDepth - m_slice->m_pps->maxCuDQPDepth) * 2);
    uint32_t absRorderQpMinCUIdx = g_zscanToRaster[absZorderQpMinCUIdx];

    // check for left LCU boundary
    if (RasterAddress::isZeroCol(absRorderQpMinCUIdx, numPartInCUSize))
    {
        return NULL;
    }

    // get index of left-CU relative to top-left corner of current quantization group
    lPartUnitIdx = g_rasterToZscan[absRorderQpMinCUIdx - 1];

    // return pointer to current LCU
    return m_pic->getCU(getAddr());
}

/** Get Above QpMinCu
*\param   aPartUnitIdx
*\param   curAbsIdxInLCU
*\returns TComDataCU*   point of TComDataCU of above QpMinCu
*/
TComDataCU* TComDataCU::getQpMinCuAbove(uint32_t& aPartUnitIdx, uint32_t curAbsIdxInLCU)
{
    uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();
    uint32_t absZorderQpMinCUIdx = curAbsIdxInLCU & (0xFF << (g_maxFullDepth - m_slice->m_pps->maxCuDQPDepth) * 2);
    uint32_t absRorderQpMinCUIdx = g_zscanToRaster[absZorderQpMinCUIdx];

    // check for top LCU boundary
    if (RasterAddress::isZeroRow(absRorderQpMinCUIdx, numPartInCUSize))
    {
        return NULL;
    }

    // get index of top-CU relative to top-left corner of current quantization group
    aPartUnitIdx = g_rasterToZscan[absRorderQpMinCUIdx - numPartInCUSize];

    // return pointer to current LCU
    return m_pic->getCU(getAddr());
}

/** Get reference QP from left QpMinCu or latest coded QP
*\param   currAbsIdxInLCU
*\returns char reference QP value
*/
char TComDataCU::getRefQP(uint32_t curAbsIdxInLCU)
{
    uint32_t lPartIdx = 0, aPartIdx = 0;
    TComDataCU* cULeft  = getQpMinCuLeft(lPartIdx, m_absIdxInLCU + curAbsIdxInLCU);
    TComDataCU* cUAbove = getQpMinCuAbove(aPartIdx, m_absIdxInLCU + curAbsIdxInLCU);

    return ((cULeft ? cULeft->getQP(lPartIdx) : getLastCodedQP(curAbsIdxInLCU)) + (cUAbove ? cUAbove->getQP(aPartIdx) : getLastCodedQP(curAbsIdxInLCU)) + 1) >> 1;
}

int TComDataCU::getLastValidPartIdx(int absPartIdx)
{
    int lastValidPartIdx = absPartIdx - 1;

    while (lastValidPartIdx >= 0 && getPredictionMode(lastValidPartIdx) == MODE_NONE)
    {
        uint32_t depth = getDepth(lastValidPartIdx);
        lastValidPartIdx -= m_numPartitions >> (depth << 1);
    }

    return lastValidPartIdx;
}

char TComDataCU::getLastCodedQP(uint32_t absPartIdx)
{
    uint32_t quPartIdxMask = 0xFF << (g_maxFullDepth - m_slice->m_pps->maxCuDQPDepth) * 2;
    int lastValidPartIdx = getLastValidPartIdx(absPartIdx & quPartIdxMask);

    if (lastValidPartIdx >= 0)
    {
        return getQP(lastValidPartIdx);
    }
    else
    {
        if (getZorderIdxInCU() > 0)
        {
            return m_pic->getCU(getAddr())->getLastCodedQP(getZorderIdxInCU());
        }
        else if (getAddr() > 0 && !(m_slice->m_pps->bEntropyCodingSyncEnabled &&
                                    getAddr() % m_pic->getFrameWidthInCU() == 0))
        {
            return m_pic->getCU(getAddr() - 1)->getLastCodedQP(m_pic->getNumPartInCU());
        }
        else
        {
            return m_slice->m_sliceQp;
        }
    }
}

/** Check whether the CU is coded in lossless coding mode
 * \param   absPartIdx
 * \returns true if the CU is coded in lossless coding mode; false if otherwise
 */
bool TComDataCU::isLosslessCoded(uint32_t absPartIdx)
{
    return m_slice->m_pps->bTransquantBypassEnabled && getCUTransquantBypass(absPartIdx);
}

/** Get allowed chroma intra modes
*\param   absPartIdx
*\param   uiModeList  pointer to chroma intra modes array
*\returns
*- fill uiModeList with chroma intra modes
*/
void TComDataCU::getAllowedChromaDir(uint32_t absPartIdx, uint32_t* modeList)
{
    modeList[0] = PLANAR_IDX;
    modeList[1] = VER_IDX;
    modeList[2] = HOR_IDX;
    modeList[3] = DC_IDX;
    modeList[4] = DM_CHROMA_IDX;

    uint32_t lumaMode = getLumaIntraDir(absPartIdx);

    for (int i = 0; i < NUM_CHROMA_MODE - 1; i++)
    {
        if (lumaMode == modeList[i])
        {
            modeList[i] = 34; // VER+8 mode
            break;
        }
    }
}

/** Get most probable intra modes
*\param   absPartIdx
*\param   intraDirPred  pointer to the array for MPM storage
*\param   mode          it is set with MPM mode in case both MPM are equal. It is used to restrict RD search at encode side.
*\returns Number of MPM
*/
int TComDataCU::getIntraDirLumaPredictor(uint32_t absPartIdx, uint32_t* intraDirPred)
{
    TComDataCU* tempCU;
    uint32_t    tempPartIdx;
    uint32_t    leftIntraDir, aboveIntraDir;

    // Get intra direction of left PU
    tempCU = getPULeft(tempPartIdx, m_absIdxInLCU + absPartIdx);

    leftIntraDir  = (tempCU && tempCU->isIntra(tempPartIdx)) ? tempCU->getLumaIntraDir(tempPartIdx) : DC_IDX;

    // Get intra direction of above PU
    tempCU = getPUAbove(tempPartIdx, m_absIdxInLCU + absPartIdx, true);

    aboveIntraDir = (tempCU && tempCU->isIntra(tempPartIdx)) ? tempCU->getLumaIntraDir(tempPartIdx) : DC_IDX;

    if (leftIntraDir == aboveIntraDir)
    {
        if (leftIntraDir >= 2) // angular modes
        {
            intraDirPred[0] = leftIntraDir;
            intraDirPred[1] = ((leftIntraDir - 2 + 31) & 31) + 2;
            intraDirPred[2] = ((leftIntraDir - 2 +  1) & 31) + 2;
        }
        else //non-angular
        {
            intraDirPred[0] = PLANAR_IDX;
            intraDirPred[1] = DC_IDX;
            intraDirPred[2] = VER_IDX;
        }
        return 1;
    }
    else
    {
        intraDirPred[0] = leftIntraDir;
        intraDirPred[1] = aboveIntraDir;

        if (leftIntraDir && aboveIntraDir) //both modes are non-planar
        {
            intraDirPred[2] = PLANAR_IDX;
        }
        else
        {
            intraDirPred[2] =  (leftIntraDir + aboveIntraDir) < 2 ? VER_IDX : DC_IDX;
        }
        return 2;
    }
}

uint32_t TComDataCU::getCtxSplitFlag(uint32_t absPartIdx, uint32_t depth)
{
    TComDataCU* tempCU;
    uint32_t    tempPartIdx;
    uint32_t    ctx;

    // Get left split flag
    tempCU = getPULeft(tempPartIdx, m_absIdxInLCU + absPartIdx);
    ctx  = (tempCU) ? ((tempCU->getDepth(tempPartIdx) > depth) ? 1 : 0) : 0;

    // Get above split flag
    tempCU = getPUAbove(tempPartIdx, m_absIdxInLCU + absPartIdx);
    ctx += (tempCU) ? ((tempCU->getDepth(tempPartIdx) > depth) ? 1 : 0) : 0;

    return ctx;
}

uint32_t TComDataCU::getQuadtreeTULog2MinSizeInCU(uint32_t absPartIdx)
{
    uint32_t log2CUSize = getLog2CUSize(absPartIdx);
    PartSize partSize   = getPartitionSize(absPartIdx);
    uint32_t quadtreeTUMaxDepth = getPredictionMode(absPartIdx) == MODE_INTRA ? m_slice->m_sps->quadtreeTUMaxDepthIntra : m_slice->m_sps->quadtreeTUMaxDepthInter;
    int intraSplitFlag = (getPredictionMode(absPartIdx) == MODE_INTRA && partSize == SIZE_NxN) ? 1 : 0;
    int interSplitFlag = ((quadtreeTUMaxDepth == 1) && (getPredictionMode(absPartIdx) == MODE_INTER) && (partSize != SIZE_2Nx2N));

    uint32_t log2MinTUSizeInCU = 0;

    if (log2CUSize < (m_slice->m_sps->quadtreeTULog2MinSize + quadtreeTUMaxDepth - 1 + interSplitFlag + intraSplitFlag))
    {
        // when fully making use of signaled TUMaxDepth + inter/intraSplitFlag, resulting luma TB size is < QuadtreeTULog2MinSize
        log2MinTUSizeInCU = m_slice->m_sps->quadtreeTULog2MinSize;
    }
    else
    {
        // when fully making use of signaled TUMaxDepth + inter/intraSplitFlag, resulting luma TB size is still >= QuadtreeTULog2MinSize
        log2MinTUSizeInCU = log2CUSize - (quadtreeTUMaxDepth - 1 + interSplitFlag + intraSplitFlag); // stop when trafoDepth == hierarchy_depth = splitFlag
        if (log2MinTUSizeInCU > m_slice->m_sps->quadtreeTULog2MaxSize)
        {
            // when fully making use of signaled TUMaxDepth + inter/intraSplitFlag, resulting luma TB size is still > QuadtreeTULog2MaxSize
            log2MinTUSizeInCU = m_slice->m_sps->quadtreeTULog2MaxSize;
        }
    }
    return log2MinTUSizeInCU;
}

uint32_t TComDataCU::getCtxSkipFlag(uint32_t absPartIdx)
{
    TComDataCU* tempCU;
    uint32_t    tempPartIdx;
    uint32_t    ctx = 0;

    // Get BCBP of left PU
    tempCU = getPULeft(tempPartIdx, m_absIdxInLCU + absPartIdx);
    ctx    = (tempCU) ? tempCU->isSkipped(tempPartIdx) : 0;

    // Get BCBP of above PU
    tempCU = getPUAbove(tempPartIdx, m_absIdxInLCU + absPartIdx);
    ctx   += (tempCU) ? tempCU->isSkipped(tempPartIdx) : 0;

    return ctx;
}

uint32_t TComDataCU::getCtxInterDir(uint32_t absPartIdx)
{
    return getDepth(absPartIdx);
}

void TComDataCU::clearCbf(uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_cbf[0] + absPartIdx, 0, sizeof(uint8_t) * curPartNum);
    memset(m_cbf[1] + absPartIdx, 0, sizeof(uint8_t) * curPartNum);
    memset(m_cbf[2] + absPartIdx, 0, sizeof(uint8_t) * curPartNum);
}

void TComDataCU::setCbfSubParts(uint32_t cbf, TextType ttype, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_cbf[ttype] + absPartIdx, cbf, sizeof(uint8_t) * curPartNum);
}

void TComDataCU::setCbfPartRange(uint32_t cbf, TextType ttype, uint32_t absPartIdx, uint32_t coveredPartIdxes)
{
    memset(m_cbf[ttype] + absPartIdx, cbf, sizeof(uint8_t) * coveredPartIdxes);
}

void TComDataCU::setDepthSubParts(uint32_t depth)
{
    /*All 4x4 partitions in current CU have the CU depth saved*/
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_depth, depth, sizeof(uint8_t) * curPartNum);
}

bool TComDataCU::isFirstAbsZorderIdxInDepth(uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    return ((m_absIdxInLCU + absPartIdx) & (curPartNum - 1)) == 0;
}

void TComDataCU::setPartSizeSubParts(PartSize mode, uint32_t absPartIdx, uint32_t depth)
{
    X265_CHECK(sizeof(*m_partSizes) == 1, "size check failure\n");
    memset(m_partSizes + absPartIdx, mode, m_pic->getNumPartInCU() >> (depth << 1));
}

void TComDataCU::setCUTransquantBypassSubParts(bool flag, uint32_t absPartIdx, uint32_t depth)
{
    memset(m_cuTransquantBypass + absPartIdx, flag, m_pic->getNumPartInCU() >> (depth << 1));
}

void TComDataCU::setSkipFlagSubParts(bool skip, uint32_t absPartIdx, uint32_t depth)
{
    X265_CHECK(sizeof(*m_skipFlag) == 1, "size check failure\n");
    memset(m_skipFlag + absPartIdx, skip, m_pic->getNumPartInCU() >> (depth << 1));
}

void TComDataCU::setPredModeSubParts(PredMode eMode, uint32_t absPartIdx, uint32_t depth)
{
    X265_CHECK(sizeof(*m_predModes) == 1, "size check failure\n");
    memset(m_predModes + absPartIdx, eMode, m_pic->getNumPartInCU() >> (depth << 1));
}

void TComDataCU::setQPSubCUs(int qp, TComDataCU* cu, uint32_t absPartIdx, uint32_t depth, bool &foundNonZeroCbf)
{
    uint32_t curPartNumb = m_pic->getNumPartInCU() >> (depth << 1);
    uint32_t curPartNumQ = curPartNumb >> 2;

    if (!foundNonZeroCbf)
    {
        if (cu->getDepth(absPartIdx) > depth)
        {
            for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++)
            {
                cu->setQPSubCUs(qp, cu, absPartIdx + partUnitIdx * curPartNumQ, depth + 1, foundNonZeroCbf);
            }
        }
        else
        {
            if (cu->getCbf(absPartIdx, TEXT_LUMA) || cu->getCbf(absPartIdx, TEXT_CHROMA_U) || cu->getCbf(absPartIdx, TEXT_CHROMA_V))
            {
                foundNonZeroCbf = true;
            }
            else
            {
                setQPSubParts(qp, absPartIdx, depth);
            }
        }
    }
}

void TComDataCU::setQPSubParts(int qp, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    for (uint32_t scuIdx = absPartIdx; scuIdx < absPartIdx + curPartNum; scuIdx++)
    {
        m_qp[scuIdx] = qp;
    }
}

void TComDataCU::setLumaIntraDirSubParts(uint32_t dir, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_lumaIntraDir + absPartIdx, dir, sizeof(uint8_t) * curPartNum);
}

template<typename T>
void TComDataCU::setSubPart(T param, T* baseLCU, uint32_t cuAddr, uint32_t cuDepth, uint32_t puIdx)
{
    X265_CHECK(sizeof(T) == 1, "size check failure\n"); // Using memset() works only for types of size 1

    uint32_t curPartNumQ = (m_pic->getNumPartInCU() >> (2 * cuDepth)) >> 2;
    switch (m_partSizes[cuAddr])
    {
    case SIZE_2Nx2N:
        memset(baseLCU + cuAddr, param, 4 * curPartNumQ);
        break;
    case SIZE_2NxN:
        memset(baseLCU + cuAddr, param, 2 * curPartNumQ);
        break;
    case SIZE_Nx2N:
        memset(baseLCU + cuAddr, param, curPartNumQ);
        memset(baseLCU + cuAddr + 2 * curPartNumQ, param, curPartNumQ);
        break;
    case SIZE_NxN:
        memset(baseLCU + cuAddr, param, curPartNumQ);
        break;
    case SIZE_2NxnU:
        if (puIdx == 0)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ >> 1));
            memset(baseLCU + cuAddr + curPartNumQ, param, (curPartNumQ >> 1));
        }
        else if (puIdx == 1)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ >> 1));
            memset(baseLCU + cuAddr + curPartNumQ, param, ((curPartNumQ >> 1) + (curPartNumQ << 1)));
        }
        else
        {
            X265_CHECK(0, "unexpected part unit index\n");
        }
        break;
    case SIZE_2NxnD:
        if (puIdx == 0)
        {
            memset(baseLCU + cuAddr, param, ((curPartNumQ << 1) + (curPartNumQ >> 1)));
            memset(baseLCU + cuAddr + (curPartNumQ << 1) + curPartNumQ, param, (curPartNumQ >> 1));
        }
        else if (puIdx == 1)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ >> 1));
            memset(baseLCU + cuAddr + curPartNumQ, param, (curPartNumQ >> 1));
        }
        else
        {
            X265_CHECK(0, "unexpected part unit index\n");
        }
        break;
    case SIZE_nLx2N:
        if (puIdx == 0)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ >> 1), param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ << 1), param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ << 1) + (curPartNumQ >> 1), param, (curPartNumQ >> 2));
        }
        else if (puIdx == 1)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ >> 1), param, (curPartNumQ + (curPartNumQ >> 2)));
            memset(baseLCU + cuAddr + (curPartNumQ << 1), param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ << 1) + (curPartNumQ >> 1), param, (curPartNumQ + (curPartNumQ >> 2)));
        }
        else
        {
            X265_CHECK(0, "unexpected part unit index\n");
        }
        break;
    case SIZE_nRx2N:
        if (puIdx == 0)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ + (curPartNumQ >> 2)));
            memset(baseLCU + cuAddr + curPartNumQ + (curPartNumQ >> 1), param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ << 1), param, (curPartNumQ + (curPartNumQ >> 2)));
            memset(baseLCU + cuAddr + (curPartNumQ << 1) + curPartNumQ + (curPartNumQ >> 1), param, (curPartNumQ >> 2));
        }
        else if (puIdx == 1)
        {
            memset(baseLCU + cuAddr, param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ >> 1), param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ << 1), param, (curPartNumQ >> 2));
            memset(baseLCU + cuAddr + (curPartNumQ << 1) + (curPartNumQ >> 1), param, (curPartNumQ >> 2));
        }
        else
        {
            X265_CHECK(0, "unexpected part unit index\n");
        }
        break;
    default:
        X265_CHECK(0, "unexpected part type\n");
    }
}

void TComDataCU::setChromIntraDirSubParts(uint32_t dir, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_chromaIntraDir + absPartIdx, dir, sizeof(uint8_t) * curPartNum);
}

void TComDataCU::setInterDirSubParts(uint32_t dir, uint32_t absPartIdx, uint32_t partIdx, uint32_t depth)
{
    setSubPart<uint8_t>(dir, m_interDir, absPartIdx, depth, partIdx);
}

void TComDataCU::setTrIdxSubParts(uint32_t trIdx, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_trIdx + absPartIdx, trIdx, sizeof(uint8_t) * curPartNum);
}

void TComDataCU::setTransformSkipSubParts(uint32_t useTransformSkipY, uint32_t useTransformSkipU, uint32_t useTransformSkipV, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_transformSkip[0] + absPartIdx, useTransformSkipY, sizeof(uint8_t) * curPartNum);
    memset(m_transformSkip[1] + absPartIdx, useTransformSkipU, sizeof(uint8_t) * curPartNum);
    memset(m_transformSkip[2] + absPartIdx, useTransformSkipV, sizeof(uint8_t) * curPartNum);
}

void TComDataCU::setTransformSkipSubParts(uint32_t useTransformSkip, TextType ttype, uint32_t absPartIdx, uint32_t depth)
{
    uint32_t curPartNum = m_pic->getNumPartInCU() >> (depth << 1);

    memset(m_transformSkip[ttype] + absPartIdx, useTransformSkip, sizeof(uint8_t) * curPartNum);
}

void TComDataCU::setTransformSkipPartRange(uint32_t useTransformSkip, TextType ttype, uint32_t absPartIdx, uint32_t coveredPartIdxes)
{
    memset(m_transformSkip[ttype] + absPartIdx, useTransformSkip, sizeof(uint8_t) * coveredPartIdxes);
}

void TComDataCU::getPartIndexAndSize(uint32_t partIdx, uint32_t& outPartAddr, int& outWidth, int& outHeight)
{
    int cuSize = 1 << getLog2CUSize(0);
    int part_mode = m_partSizes[0];
    int part_idx  = partIdx;

    int tmp = partTable[part_mode][part_idx][0];
    outWidth = ((tmp >> 4) * cuSize) >> 2;
    outHeight = ((tmp & 0xF) * cuSize) >> 2;
    outPartAddr = (partAddrTable[part_mode][part_idx] * m_numPartitions) >> 4;
}

void TComDataCU::getMvField(TComDataCU* cu, uint32_t absPartIdx, int picList, TComMvField& outMvField)
{
    if (cu == NULL)  // OUT OF BOUNDARY
    {
        MV mv(0, 0);
        outMvField.setMvField(mv, NOT_VALID);
        return;
    }

    TComCUMvField* cuMvField = cu->getCUMvField(picList);
    outMvField.setMvField(cuMvField->getMv(absPartIdx), cuMvField->getRefIdx(absPartIdx));
}

void TComDataCU::deriveLeftRightTopIdx(uint32_t partIdx, uint32_t& ruiPartIdxLT, uint32_t& ruiPartIdxRT)
{
    ruiPartIdxLT = m_absIdxInLCU;
    ruiPartIdxRT = g_rasterToZscan[g_zscanToRaster[ruiPartIdxLT] + (1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE)) - 1];

    switch (m_partSizes[0])
    {
    case SIZE_2Nx2N: break;
    case SIZE_2NxN:
        ruiPartIdxLT += (partIdx == 0) ? 0 : m_numPartitions >> 1;
        ruiPartIdxRT += (partIdx == 0) ? 0 : m_numPartitions >> 1;
        break;
    case SIZE_Nx2N:
        ruiPartIdxLT += (partIdx == 0) ? 0 : m_numPartitions >> 2;
        ruiPartIdxRT -= (partIdx == 1) ? 0 : m_numPartitions >> 2;
        break;
    case SIZE_NxN:
        ruiPartIdxLT += (m_numPartitions >> 2) * partIdx;
        ruiPartIdxRT +=  (m_numPartitions >> 2) * (partIdx - 1);
        break;
    case SIZE_2NxnU:
        ruiPartIdxLT += (partIdx == 0) ? 0 : m_numPartitions >> 3;
        ruiPartIdxRT += (partIdx == 0) ? 0 : m_numPartitions >> 3;
        break;
    case SIZE_2NxnD:
        ruiPartIdxLT += (partIdx == 0) ? 0 : (m_numPartitions >> 1) + (m_numPartitions >> 3);
        ruiPartIdxRT += (partIdx == 0) ? 0 : (m_numPartitions >> 1) + (m_numPartitions >> 3);
        break;
    case SIZE_nLx2N:
        ruiPartIdxLT += (partIdx == 0) ? 0 : m_numPartitions >> 4;
        ruiPartIdxRT -= (partIdx == 1) ? 0 : (m_numPartitions >> 2) + (m_numPartitions >> 4);
        break;
    case SIZE_nRx2N:
        ruiPartIdxLT += (partIdx == 0) ? 0 : (m_numPartitions >> 2) + (m_numPartitions >> 4);
        ruiPartIdxRT -= (partIdx == 1) ? 0 : m_numPartitions >> 4;
        break;
    default:
        X265_CHECK(0, "unexpected part index\n");
        break;
    }
}

void TComDataCU::deriveLeftBottomIdx(uint32_t partIdx, uint32_t& outPartIdxLB)
{
    outPartIdxLB = g_rasterToZscan[g_zscanToRaster[m_absIdxInLCU] + ((1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE - 1)) - 1) * m_pic->getNumPartInCUSize()];

    switch (m_partSizes[0])
    {
    case SIZE_2Nx2N:
        outPartIdxLB += m_numPartitions >> 1;
        break;
    case SIZE_2NxN:
        outPartIdxLB += (partIdx == 0) ? 0 : m_numPartitions >> 1;
        break;
    case SIZE_Nx2N:
        outPartIdxLB += (partIdx == 0) ? m_numPartitions >> 1 : (m_numPartitions >> 2) * 3;
        break;
    case SIZE_NxN:
        outPartIdxLB += (m_numPartitions >> 2) * partIdx;
        break;
    case SIZE_2NxnU:
        outPartIdxLB += (partIdx == 0) ? -((int)m_numPartitions >> 3) : m_numPartitions >> 1;
        break;
    case SIZE_2NxnD:
        outPartIdxLB += (partIdx == 0) ? (m_numPartitions >> 2) + (m_numPartitions >> 3) : m_numPartitions >> 1;
        break;
    case SIZE_nLx2N:
        outPartIdxLB += (partIdx == 0) ? m_numPartitions >> 1 : (m_numPartitions >> 1) + (m_numPartitions >> 4);
        break;
    case SIZE_nRx2N:
        outPartIdxLB += (partIdx == 0) ? m_numPartitions >> 1 : (m_numPartitions >> 1) + (m_numPartitions >> 2) + (m_numPartitions >> 4);
        break;
    default:
        X265_CHECK(0, "unexpected part index\n");
        break;
    }
}

/** Derives the partition index of neighboring bottom right block
 * \param [in]  cuMode
 * \param [in]  partIdx
 * \param [out] outPartIdxRB
 */
void TComDataCU::deriveRightBottomIdx(uint32_t partIdx, uint32_t& outPartIdxRB)
{
    outPartIdxRB = g_rasterToZscan[g_zscanToRaster[m_absIdxInLCU] +
                                   ((1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE - 1)) - 1) * m_pic->getNumPartInCUSize() +
                                   (1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE)) - 1];

    switch (m_partSizes[0])
    {
    case SIZE_2Nx2N:
        outPartIdxRB += m_numPartitions >> 1;
        break;
    case SIZE_2NxN:
        outPartIdxRB += (partIdx == 0) ? 0 : m_numPartitions >> 1;
        break;
    case SIZE_Nx2N:
        outPartIdxRB += (partIdx == 0) ? m_numPartitions >> 2 : (m_numPartitions >> 1);
        break;
    case SIZE_NxN:
        outPartIdxRB += (m_numPartitions >> 2) * (partIdx - 1);
        break;
    case SIZE_2NxnU:
        outPartIdxRB += (partIdx == 0) ? -((int)m_numPartitions >> 3) : m_numPartitions >> 1;
        break;
    case SIZE_2NxnD:
        outPartIdxRB += (partIdx == 0) ? (m_numPartitions >> 2) + (m_numPartitions >> 3) : m_numPartitions >> 1;
        break;
    case SIZE_nLx2N:
        outPartIdxRB += (partIdx == 0) ? (m_numPartitions >> 3) + (m_numPartitions >> 4) : m_numPartitions >> 1;
        break;
    case SIZE_nRx2N:
        outPartIdxRB += (partIdx == 0) ? (m_numPartitions >> 2) + (m_numPartitions >> 3) + (m_numPartitions >> 4) : m_numPartitions >> 1;
        break;
    default:
        X265_CHECK(0, "unexpected part index\n");
        break;
    }
}

void TComDataCU::deriveLeftRightTopIdxAdi(uint32_t& outPartIdxLT, uint32_t& outPartIdxRT, uint32_t partOffset, uint32_t partDepth)
{
    uint32_t numPartInWidth = 1 << (m_log2CUSize[0] - LOG2_UNIT_SIZE - partDepth);

    outPartIdxLT = m_absIdxInLCU + partOffset;
    outPartIdxRT = g_rasterToZscan[g_zscanToRaster[outPartIdxLT] + numPartInWidth - 1];
}

bool TComDataCU::hasEqualMotion(uint32_t absPartIdx, TComDataCU* candCU, uint32_t candAbsPartIdx)
{
    if (getInterDir(absPartIdx) != candCU->getInterDir(candAbsPartIdx))
    {
        return false;
    }

    for (uint32_t refListIdx = 0; refListIdx < 2; refListIdx++)
    {
        if (getInterDir(absPartIdx) & (1 << refListIdx))
        {
            if (getCUMvField(refListIdx)->getMv(absPartIdx)     != candCU->getCUMvField(refListIdx)->getMv(candAbsPartIdx) ||
                getCUMvField(refListIdx)->getRefIdx(absPartIdx) != candCU->getCUMvField(refListIdx)->getRefIdx(candAbsPartIdx))
            {
                return false;
            }
        }
    }

    return true;
}

/** Constructs a list of merging candidates
 * \param absPartIdx
 * \param puIdx
 * \param depth
 * \param mvFieldNeighbours
 * \param interDirNeighbours
 * \param maxNumMergeCand
 */
void TComDataCU::getInterMergeCandidates(uint32_t absPartIdx, uint32_t puIdx, TComMvField (*mvFieldNeighbours)[2], uint8_t* interDirNeighbours,
                                         uint32_t& maxNumMergeCand)
{
    uint32_t absPartAddr = m_absIdxInLCU + absPartIdx;
    const bool isInterB = m_slice->isInterB();

    maxNumMergeCand = m_slice->m_maxNumMergeCand;

    for (uint32_t i = 0; i < maxNumMergeCand; ++i)
    {
        mvFieldNeighbours[i][0].refIdx = NOT_VALID;
        mvFieldNeighbours[i][1].refIdx = NOT_VALID;
    }

    // compute the location of the current PU
    int xP, yP, nPSW, nPSH;
    this->getPartPosition(puIdx, xP, yP, nPSW, nPSH);

    uint32_t count = 0;

    uint32_t partIdxLT, partIdxRT, partIdxLB;
    PartSize curPS = getPartitionSize(absPartIdx);
    deriveLeftBottomIdx(puIdx, partIdxLB);

    //left
    uint32_t leftPartIdx = 0;
    TComDataCU* cuLeft = 0;
    cuLeft = getPULeft(leftPartIdx, partIdxLB);
    bool isAvailableA1 = cuLeft &&
        cuLeft->isDiffMER(xP - 1, yP + nPSH - 1, xP, yP) &&
        !(puIdx == 1 && (curPS == SIZE_Nx2N || curPS == SIZE_nLx2N || curPS == SIZE_nRx2N)) &&
        !cuLeft->isIntra(leftPartIdx);
    if (isAvailableA1)
    {
        // get Inter Dir
        interDirNeighbours[count] = cuLeft->getInterDir(leftPartIdx);
        // get Mv from Left
        cuLeft->getMvField(cuLeft, leftPartIdx, REF_PIC_LIST_0, mvFieldNeighbours[count][0]);
        if (isInterB)
            cuLeft->getMvField(cuLeft, leftPartIdx, REF_PIC_LIST_1, mvFieldNeighbours[count][1]);

        count++;
    
        if (count == maxNumMergeCand)
            return;
    }

    deriveLeftRightTopIdx(puIdx, partIdxLT, partIdxRT);

    // above
    uint32_t abovePartIdx = 0;
    TComDataCU* cuAbove = 0;
    cuAbove = getPUAbove(abovePartIdx, partIdxRT);
    bool isAvailableB1 = cuAbove &&
        cuAbove->isDiffMER(xP + nPSW - 1, yP - 1, xP, yP) &&
        !(puIdx == 1 && (curPS == SIZE_2NxN || curPS == SIZE_2NxnU || curPS == SIZE_2NxnD)) &&
        !cuAbove->isIntra(abovePartIdx);
    if (isAvailableB1 && (!isAvailableA1 || !cuLeft->hasEqualMotion(leftPartIdx, cuAbove, abovePartIdx)))
    {
        // get Inter Dir
        interDirNeighbours[count] = cuAbove->getInterDir(abovePartIdx);
        // get Mv from Left
        cuAbove->getMvField(cuAbove, abovePartIdx, REF_PIC_LIST_0, mvFieldNeighbours[count][0]);
        if (isInterB)
            cuAbove->getMvField(cuAbove, abovePartIdx, REF_PIC_LIST_1, mvFieldNeighbours[count][1]);

        count++;
   
        if (count == maxNumMergeCand)
            return;
    }

    // above right
    uint32_t aboveRightPartIdx = 0;
    TComDataCU* cuAboveRight = 0;
    cuAboveRight = getPUAboveRight(aboveRightPartIdx, partIdxRT);
    bool isAvailableB0 = cuAboveRight &&
        cuAboveRight->isDiffMER(xP + nPSW, yP - 1, xP, yP) &&
        !cuAboveRight->isIntra(aboveRightPartIdx);
    if (isAvailableB0 && (!isAvailableB1 || !cuAbove->hasEqualMotion(abovePartIdx, cuAboveRight, aboveRightPartIdx)))
    {
        // get Inter Dir
        interDirNeighbours[count] = cuAboveRight->getInterDir(aboveRightPartIdx);
        // get Mv from Left
        cuAboveRight->getMvField(cuAboveRight, aboveRightPartIdx, REF_PIC_LIST_0, mvFieldNeighbours[count][0]);
        if (isInterB)
            cuAboveRight->getMvField(cuAboveRight, aboveRightPartIdx, REF_PIC_LIST_1, mvFieldNeighbours[count][1]);

        count++;

        if (count == maxNumMergeCand)
            return;
    }

    // left bottom
    uint32_t leftBottomPartIdx = 0;
    TComDataCU* cuLeftBottom = 0;
    cuLeftBottom = this->getPUBelowLeft(leftBottomPartIdx, partIdxLB);
    bool isAvailableA0 = cuLeftBottom &&
        cuLeftBottom->isDiffMER(xP - 1, yP + nPSH, xP, yP) &&
        !cuLeftBottom->isIntra(leftBottomPartIdx);
    if (isAvailableA0 && (!isAvailableA1 || !cuLeft->hasEqualMotion(leftPartIdx, cuLeftBottom, leftBottomPartIdx)))
    {
        // get Inter Dir
        interDirNeighbours[count] = cuLeftBottom->getInterDir(leftBottomPartIdx);
        // get Mv from Left
        cuLeftBottom->getMvField(cuLeftBottom, leftBottomPartIdx, REF_PIC_LIST_0, mvFieldNeighbours[count][0]);
        if (isInterB)
            cuLeftBottom->getMvField(cuLeftBottom, leftBottomPartIdx, REF_PIC_LIST_1, mvFieldNeighbours[count][1]);

        count++;

        if (count == maxNumMergeCand)
            return;
    }

    // above left
    if (count < 4)
    {
        uint32_t aboveLeftPartIdx = 0;
        TComDataCU* cuAboveLeft = 0;
        cuAboveLeft = getPUAboveLeft(aboveLeftPartIdx, absPartAddr);
        bool isAvailableB2 = cuAboveLeft &&
            cuAboveLeft->isDiffMER(xP - 1, yP - 1, xP, yP) &&
            !cuAboveLeft->isIntra(aboveLeftPartIdx);
        if (isAvailableB2 && (!isAvailableA1 || !cuLeft->hasEqualMotion(leftPartIdx, cuAboveLeft, aboveLeftPartIdx))
            && (!isAvailableB1 || !cuAbove->hasEqualMotion(abovePartIdx, cuAboveLeft, aboveLeftPartIdx)))
        {
            // get Inter Dir
            interDirNeighbours[count] = cuAboveLeft->getInterDir(aboveLeftPartIdx);
            // get Mv from Left
            cuAboveLeft->getMvField(cuAboveLeft, aboveLeftPartIdx, REF_PIC_LIST_0, mvFieldNeighbours[count][0]);
            if (isInterB)
                cuAboveLeft->getMvField(cuAboveLeft, aboveLeftPartIdx, REF_PIC_LIST_1, mvFieldNeighbours[count][1]);

            count++;

            if (count == maxNumMergeCand)
                return;
        }
    }
    // TMVP always enabled
    {
        //>> MTK colocated-RightBottom
        uint32_t partIdxRB;

        deriveRightBottomIdx(puIdx, partIdxRB);

        uint32_t absPartIdxTmp = g_zscanToRaster[partIdxRB];
        uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();

        MV colmv;
        int refIdx;
        int lcuIdx = -1;

        if ((m_pic->getCU(m_cuAddr)->getCUPelX() + g_rasterToPelX[absPartIdxTmp] + UNIT_SIZE) >= m_slice->m_sps->picWidthInLumaSamples)  // image boundary check
        {
        }
        else if ((m_pic->getCU(m_cuAddr)->getCUPelY() + g_rasterToPelY[absPartIdxTmp] + UNIT_SIZE) >= m_slice->m_sps->picHeightInLumaSamples)
        {
        }
        else
        {
            if ((absPartIdxTmp % numPartInCUSize < numPartInCUSize - 1) &&        // is not at the last column of LCU
                (absPartIdxTmp / numPartInCUSize < numPartInCUSize - 1)) // is not at the last row    of LCU
            {
                absPartAddr = g_rasterToZscan[absPartIdxTmp + numPartInCUSize + 1];
                lcuIdx = getAddr();
            }
            else if (absPartIdxTmp % numPartInCUSize < numPartInCUSize - 1)       // is not at the last column of LCU But is last row of LCU
                absPartAddr = g_rasterToZscan[(absPartIdxTmp + numPartInCUSize + 1) % m_pic->getNumPartInCU()];
            else if (absPartIdxTmp / numPartInCUSize < numPartInCUSize - 1) // is not at the last row of LCU But is last column of LCU
            {
                absPartAddr = g_rasterToZscan[absPartIdxTmp + 1];
                lcuIdx = getAddr() + 1;
            }
            else //is the right bottom corner of LCU
                absPartAddr = 0;
        }

        refIdx = 0;
        uint32_t partIdxCenter;
        uint32_t curLCUIdx = getAddr();
        int dir = 0;
        uint32_t arrayAddr = count;
        xDeriveCenterIdx(puIdx, partIdxCenter);
        bool bExistMV = lcuIdx >= 0 && xGetColMVP(REF_PIC_LIST_0, lcuIdx, absPartAddr, colmv, refIdx);
        if (!bExistMV)
            bExistMV = xGetColMVP(REF_PIC_LIST_0, curLCUIdx, partIdxCenter, colmv, refIdx);
        if (bExistMV)
        {
            dir |= 1;
            mvFieldNeighbours[arrayAddr][0].setMvField(colmv, refIdx);
        }

        if (isInterB)
        {
            bExistMV = lcuIdx >= 0 && xGetColMVP(REF_PIC_LIST_1, lcuIdx, absPartAddr, colmv, refIdx);
            if (!bExistMV)
                bExistMV = xGetColMVP(REF_PIC_LIST_1, curLCUIdx, partIdxCenter, colmv, refIdx);

            if (bExistMV)
            {
                dir |= 2;
                mvFieldNeighbours[arrayAddr][1].setMvField(colmv, refIdx);
            }
        }

        if (dir != 0)
        {
            interDirNeighbours[arrayAddr] = dir;

            count++;
        
            if (count == maxNumMergeCand)
                return;
        }
    }

    uint32_t arrayAddr = count;

    if (isInterB)
    {
        const uint32_t cutoff = count * (count - 1);
        uint32_t priorityList0 = 0xEDC984; // { 0, 1, 0, 2, 1, 2, 0, 3, 1, 3, 2, 3 }
        uint32_t priorityList1 = 0xB73621; // { 1, 0, 2, 0, 2, 1, 3, 0, 3, 1, 3, 2 }

        for (uint32_t idx = 0; idx < cutoff; idx++)
        {
            int i = priorityList0 & 3;
            int j = priorityList1 & 3;
            priorityList0 >>= 2;
            priorityList1 >>= 2;

            if ((interDirNeighbours[i] & 0x1) && (interDirNeighbours[j] & 0x2))
            {
                // get Mv from cand[i] and cand[j]
                int refIdxL0 = mvFieldNeighbours[i][0].refIdx;
                int refIdxL1 = mvFieldNeighbours[j][1].refIdx;
                int refPOCL0 = m_slice->m_refPOCList[0][refIdxL0];
                int refPOCL1 = m_slice->m_refPOCList[1][refIdxL1];
                if (!(refPOCL0 == refPOCL1 && mvFieldNeighbours[i][0].mv == mvFieldNeighbours[j][1].mv))
                {
                    mvFieldNeighbours[arrayAddr][0].setMvField(mvFieldNeighbours[i][0].mv, refIdxL0);
                    mvFieldNeighbours[arrayAddr][1].setMvField(mvFieldNeighbours[j][1].mv, refIdxL1);
                    interDirNeighbours[arrayAddr] = 3;

                    arrayAddr++;

                    if (arrayAddr == maxNumMergeCand)
                        return;
                }
            }
        }
    }
    int numRefIdx = (isInterB) ? X265_MIN(m_slice->m_numRefIdx[0], m_slice->m_numRefIdx[1]) : m_slice->m_numRefIdx[0];
    int r = 0;
    int refcnt = 0;
    while (arrayAddr < maxNumMergeCand)
    {
        interDirNeighbours[arrayAddr] = 1;
        mvFieldNeighbours[arrayAddr][0].setMvField(MV(0, 0), r);

        if (isInterB)
        {
            interDirNeighbours[arrayAddr] = 3;
            mvFieldNeighbours[arrayAddr][1].setMvField(MV(0, 0), r);
        }

        arrayAddr++;

        if (refcnt == numRefIdx - 1)
            r = 0;
        else
        {
            ++r;
            ++refcnt;
        }
    }
}

/** Check whether the current PU and a spatial neighboring PU are in a same ME region.
 * \param xN, xN   location of the upper-left corner pixel of a neighboring PU
 * \param xP, yP   location of the upper-left corner pixel of the current PU
 * \returns bool
 */
bool TComDataCU::isDiffMER(int xN, int yN, int xP, int yP)
{
    uint32_t plevel = 2;

    if ((xN >> plevel) != (xP >> plevel))
        return true;
    if ((yN >> plevel) != (yP >> plevel))
        return true;
    return false;
}

/** calculate the location of upper-left corner pixel and size of the current PU.
 * \param partIdx  PU index within a CU
 * \param xP, yP   location of the upper-left corner pixel of the current PU
 * \param PSW, nPSH    size of the curren PU
 * \returns void
 */
void TComDataCU::getPartPosition(uint32_t partIdx, int& xP, int& yP, int& nPSW, int& nPSH)
{
    int cuSize = 1 << getLog2CUSize(0);
    int part_mode = m_partSizes[0];
    int part_idx  = partIdx;

    int tmp = partTable[part_mode][part_idx][0];
    nPSW = ((tmp >> 4) * cuSize) >> 2;
    nPSH = ((tmp & 0xF) * cuSize) >> 2;

    tmp = partTable[part_mode][part_idx][1];
    xP = ((tmp >> 4) * cuSize) >> 2;
    yP = ((tmp & 0xF) * cuSize) >> 2;
}

/** Constructs a list of candidates for AMVP
 * \param partIdx
 * \param partAddr
 * \param picList
 * \param refIdx
 * \param info
 * \param mvc
 */
int TComDataCU::fillMvpCand(uint32_t partIdx, uint32_t partAddr, int picList, int refIdx, MV* amvpCand, MV* mvc)
{
    int num = 0;

    //-- Get Spatial MV
    uint32_t partIdxLT, partIdxRT, partIdxLB;

    deriveLeftRightTopIdx(partIdx, partIdxLT, partIdxRT);
    deriveLeftBottomIdx(partIdx, partIdxLB);

    MV mv[MD_ABOVE_LEFT + 1];
    MV mvOrder[MD_ABOVE_LEFT + 1];
    bool valid[MD_ABOVE_LEFT + 1];
    bool validOrder[MD_ABOVE_LEFT + 1];

    valid[MD_BELOW_LEFT]  = xAddMVPCand(mv[MD_BELOW_LEFT], picList, refIdx, partIdxLB, MD_BELOW_LEFT);
    valid[MD_LEFT]        = xAddMVPCand(mv[MD_LEFT], picList, refIdx, partIdxLB, MD_LEFT);
    valid[MD_ABOVE_RIGHT] = xAddMVPCand(mv[MD_ABOVE_RIGHT], picList, refIdx, partIdxRT, MD_ABOVE_RIGHT);
    valid[MD_ABOVE]       = xAddMVPCand(mv[MD_ABOVE], picList, refIdx, partIdxRT, MD_ABOVE);
    valid[MD_ABOVE_LEFT]  = xAddMVPCand(mv[MD_ABOVE_LEFT], picList, refIdx, partIdxLT, MD_ABOVE_LEFT);

    validOrder[MD_BELOW_LEFT]  = xAddMVPCandOrder(mvOrder[MD_BELOW_LEFT], picList, refIdx, partIdxLB, MD_BELOW_LEFT);
    validOrder[MD_LEFT]        = xAddMVPCandOrder(mvOrder[MD_LEFT], picList, refIdx, partIdxLB, MD_LEFT);
    validOrder[MD_ABOVE_RIGHT] = xAddMVPCandOrder(mvOrder[MD_ABOVE_RIGHT], picList, refIdx, partIdxRT, MD_ABOVE_RIGHT);
    validOrder[MD_ABOVE]       = xAddMVPCandOrder(mvOrder[MD_ABOVE], picList, refIdx, partIdxRT, MD_ABOVE);
    validOrder[MD_ABOVE_LEFT]  = xAddMVPCandOrder(mvOrder[MD_ABOVE_LEFT], picList, refIdx, partIdxLT, MD_ABOVE_LEFT);

    // Left predictor search
    if (valid[MD_BELOW_LEFT])
        amvpCand[num++] = mv[MD_BELOW_LEFT];
    else if (valid[MD_LEFT])
        amvpCand[num++] = mv[MD_LEFT];
    else if (validOrder[MD_BELOW_LEFT])
        amvpCand[num++] = mvOrder[MD_BELOW_LEFT];
    else if (validOrder[MD_LEFT])
        amvpCand[num++] = mvOrder[MD_LEFT];

    bool bAddedSmvp = num > 0;

    // Above predictor search
    if (valid[MD_ABOVE_RIGHT])
        amvpCand[num++] = mv[MD_ABOVE_RIGHT];
    else if (valid[MD_ABOVE])
        amvpCand[num++] = mv[MD_ABOVE];
    else if (valid[MD_ABOVE_LEFT])
        amvpCand[num++] = mv[MD_ABOVE_LEFT];

    if (!bAddedSmvp)
    {
        if (validOrder[MD_ABOVE_RIGHT])
            amvpCand[num++] = mvOrder[MD_ABOVE_RIGHT];
        else if (validOrder[MD_ABOVE])
            amvpCand[num++] = mvOrder[MD_ABOVE];
        else if (validOrder[MD_ABOVE_LEFT])
            amvpCand[num++] = mvOrder[MD_ABOVE_LEFT];
    }

    int numMvc = 0;
    for (int dir = MD_LEFT; dir <= MD_ABOVE_LEFT; dir++)
    {
        if (valid[dir] && mv[dir].notZero())
            mvc[numMvc++] = mv[dir];

        if (validOrder[dir] && mvOrder[dir].notZero())
            mvc[numMvc++] = mvOrder[dir];
    }

    if (num == 2)
    {
        if (amvpCand[0] == amvpCand[1])
            num = 1;
        else
            /* AMVP_NUM_CANDS = 2 */
            return numMvc;
    }

    // TMVP always enabled
    {
        // Get Temporal Motion Predictor
        int refIdxCol = refIdx;
        MV  colmv;
        uint32_t partIdxRB;
        uint32_t absPartIdx;
        uint32_t absPartAddr;

        deriveRightBottomIdx(partIdx, partIdxRB);
        absPartAddr = m_absIdxInLCU + partAddr;

        //----  co-located RightBottom Temporal Predictor (H) ---//
        absPartIdx = g_zscanToRaster[partIdxRB];
        int lcuIdx = -1;
        if ((m_pic->getCU(m_cuAddr)->getCUPelX() + g_rasterToPelX[absPartIdx] + UNIT_SIZE) >= m_slice->m_sps->picWidthInLumaSamples)  // image boundary check
        {
        }
        else if ((m_pic->getCU(m_cuAddr)->getCUPelY() + g_rasterToPelY[absPartIdx] + UNIT_SIZE) >= m_slice->m_sps->picHeightInLumaSamples)
        {
        }
        else
        {
            uint32_t numPartInCUSize = m_pic->getNumPartInCUSize();
            if ((absPartIdx % numPartInCUSize < numPartInCUSize - 1) && // is not at the last column of LCU
                (absPartIdx / numPartInCUSize < numPartInCUSize - 1))   // is not at the last row    of LCU
            {
                absPartAddr = g_rasterToZscan[absPartIdx + numPartInCUSize + 1];
                lcuIdx = getAddr();
            }
            else if (absPartIdx % numPartInCUSize < numPartInCUSize - 1) // is not at the last column of LCU But is last row of LCU
                absPartAddr = g_rasterToZscan[(absPartIdx + numPartInCUSize + 1) % m_pic->getNumPartInCU()];
            else if (absPartIdx / numPartInCUSize < numPartInCUSize - 1) // is not at the last row of LCU But is last column of LCU
            {
                absPartAddr = g_rasterToZscan[absPartIdx + 1];
                lcuIdx = getAddr() + 1;
            }
            else // is the right bottom corner of LCU
                absPartAddr = 0;
        }
        if (lcuIdx >= 0 && xGetColMVP(picList, lcuIdx, absPartAddr, colmv, refIdxCol))
        {
            amvpCand[num++] = colmv;
            mvc[numMvc++] = colmv;
        }
        else
        {
            uint32_t partIdxCenter;
            uint32_t curLCUIdx = getAddr();
            xDeriveCenterIdx(partIdx, partIdxCenter);
            if (xGetColMVP(picList, curLCUIdx, partIdxCenter, colmv, refIdxCol))
            {
                amvpCand[num++] = colmv;
                mvc[numMvc++] = colmv;
            }
        }
        //----  co-located RightBottom Temporal Predictor  ---//
    }

    while (num < AMVP_NUM_CANDS)
    {
        amvpCand[num++] = 0;
    }

    return numMvc;
}

bool TComDataCU::isBipredRestriction()
{
    return getLog2CUSize(0) == 3 && getPartitionSize(0) != SIZE_2Nx2N;
}

void TComDataCU::clipMv(MV& outMV)
{
    int mvshift = 2;
    int offset = 8;
    int xmax = (m_slice->m_sps->picWidthInLumaSamples + offset - m_cuPelX - 1) << mvshift;
    int xmin = (-(int)g_maxCUSize - offset - (int)m_cuPelX + 1) << mvshift;

    int ymax = (m_slice->m_sps->picHeightInLumaSamples + offset - m_cuPelY - 1) << mvshift;
    int ymin = (-(int)g_maxCUSize - offset - (int)m_cuPelY + 1) << mvshift;

    outMV.x = X265_MIN(xmax, X265_MAX(xmin, (int)outMV.x));
    outMV.y = X265_MIN(ymax, X265_MAX(ymin, (int)outMV.y));
}

/** Test whether the current block is skipped
 * \param partIdx Block index
 * \returns Flag indicating whether the block is skipped
 */
bool TComDataCU::isSkipped(uint32_t partIdx)
{
    return getSkipFlag(partIdx);
}

// ====================================================================================================================
// Protected member functions
// ====================================================================================================================

bool TComDataCU::xAddMVPCand(MV& mvp, int picList, int refIdx, uint32_t partUnitIdx, MVP_DIR dir)
{
    TComDataCU* tmpCU = NULL;
    uint32_t idx = 0;

    switch (dir)
    {
    case MD_LEFT:
        tmpCU = getPULeft(idx, partUnitIdx);
        break;
    case MD_ABOVE:
        tmpCU = getPUAbove(idx, partUnitIdx);
        break;
    case MD_ABOVE_RIGHT:
        tmpCU = getPUAboveRight(idx, partUnitIdx);
        break;
    case MD_BELOW_LEFT:
        tmpCU = getPUBelowLeft(idx, partUnitIdx);
        break;
    case MD_ABOVE_LEFT:
        tmpCU = getPUAboveLeft(idx, partUnitIdx);
        break;
    default:
        return false;
    }

    if (!tmpCU)
        return false;

    int refPOC = m_slice->m_refPOCList[picList][refIdx];
    int otherPOC = tmpCU->m_slice->m_refPOCList[picList][tmpCU->getCUMvField(picList)->getRefIdx(idx)];
    if (tmpCU->getCUMvField(picList)->getRefIdx(idx) >= 0 && refPOC == otherPOC)
    {
        mvp = tmpCU->getCUMvField(picList)->getMv(idx);
        return true;
    }

    int refPicList2nd = 0;
    if (picList == 0)
        refPicList2nd = 1;
    else if (picList == 1)
        refPicList2nd = 0;

    int curRefPOC = m_slice->m_refPOCList[picList][refIdx];
    int neibRefPOC;

    if (tmpCU->getCUMvField(refPicList2nd)->getRefIdx(idx) >= 0)
    {
        neibRefPOC = tmpCU->m_slice->m_refPOCList[refPicList2nd][tmpCU->getCUMvField(refPicList2nd)->getRefIdx(idx)];
        if (neibRefPOC == curRefPOC)
        {
            // Same reference frame but different list
            mvp = tmpCU->getCUMvField(refPicList2nd)->getMv(idx);
            return true;
        }
    }
    return false;
}

/**
 * \param info
 * \param picList
 * \param refIdx
 * \param partUnitIdx
 * \param dir
 * \returns bool
 */
bool TComDataCU::xAddMVPCandOrder(MV& outMV, int picList, int refIdx, uint32_t partUnitIdx, MVP_DIR dir)
{
    TComDataCU* tmpCU = NULL;
    uint32_t idx = 0;

    switch (dir)
    {
    case MD_LEFT:
        tmpCU = getPULeft(idx, partUnitIdx);
        break;
    case MD_ABOVE:
        tmpCU = getPUAbove(idx, partUnitIdx);
        break;
    case MD_ABOVE_RIGHT:
        tmpCU = getPUAboveRight(idx, partUnitIdx);
        break;
    case MD_BELOW_LEFT:
        tmpCU = getPUBelowLeft(idx, partUnitIdx);
        break;
    case MD_ABOVE_LEFT:
        tmpCU = getPUAboveLeft(idx, partUnitIdx);
        break;
    default:
        return false;
    }

    if (!tmpCU)
        return false;

    int refPicList2nd = REF_PIC_LIST_0;
    if (picList == REF_PIC_LIST_0)
        refPicList2nd = REF_PIC_LIST_1;
    else if (picList == REF_PIC_LIST_1)
        refPicList2nd = REF_PIC_LIST_0;

    int curPOC = m_slice->m_poc;
    int curRefPOC = m_slice->m_refPOCList[picList][refIdx];
    int neibPOC = curPOC;
    int neibRefPOC;

    //---------------  V1 (END) ------------------//
    if (tmpCU->getCUMvField(picList)->getRefIdx(idx) >= 0)
    {
        neibRefPOC = tmpCU->m_slice->m_refPOCList[picList][tmpCU->getCUMvField(picList)->getRefIdx(idx)];
        MV mvp = tmpCU->getCUMvField(picList)->getMv(idx);

        int scale = xGetDistScaleFactor(curPOC, curRefPOC, neibPOC, neibRefPOC);
        if (scale == 4096)
            outMV = mvp;
        else
            outMV = scaleMv(mvp, scale);

        return true;
    }

    //---------------------- V2(END) --------------------//
    if (tmpCU->getCUMvField(refPicList2nd)->getRefIdx(idx) >= 0)
    {
        neibRefPOC = tmpCU->m_slice->m_refPOCList[refPicList2nd][tmpCU->getCUMvField(refPicList2nd)->getRefIdx(idx)];
        MV mvp = tmpCU->getCUMvField(refPicList2nd)->getMv(idx);

        int scale = xGetDistScaleFactor(curPOC, curRefPOC, neibPOC, neibRefPOC);
        if (scale == 4096)
            outMV = mvp;
        else
            outMV = scaleMv(mvp, scale);

        return true;
    }

    //---------------------- V3(END) --------------------//
    return false;
}

/**
 * \param picList
 * \param cuAddr
 * \param partUnitIdx
 * \param outRefIdx
 * \returns bool
 */
bool TComDataCU::xGetColMVP(int picList, int cuAddr, int partUnitIdx, MV& outMV, int& outRefIdx)
{
    uint32_t absPartAddr = partUnitIdx & TMVP_UNIT_MASK;

    int colRefPicList;
    int colPOC, colRefPOC, curPOC, curRefPOC, scale;
    MV colmv;

    // use coldir.
    Frame *colPic = m_slice->m_refPicList[m_slice->isInterB() ? 1 - m_slice->m_colFromL0Flag : 0][m_slice->m_colRefIdx];
    TComDataCU *colCU = colPic->getCU(cuAddr);

    if (colCU->m_pic == 0 || colCU->getPartitionSize(partUnitIdx) == SIZE_NONE)
        return false;

    curPOC = m_slice->m_poc;
    colPOC = colCU->m_slice->m_poc;

    if (colCU->isIntra(absPartAddr))
        return false;

    colRefPicList = m_slice->m_bCheckLDC ? picList : m_slice->m_colFromL0Flag;

    int colRefIdx = colCU->getCUMvField(colRefPicList)->getRefIdx(absPartAddr);

    if (colRefIdx < 0)
    {
        colRefPicList = 1 - colRefPicList;
        colRefIdx = colCU->getCUMvField(colRefPicList)->getRefIdx(absPartAddr);

        if (colRefIdx < 0)
            return false;
    }

    // Scale the vector.
    colRefPOC = colCU->m_slice->m_refPOCList[colRefPicList][colRefIdx];
    colmv = colCU->getCUMvField(colRefPicList)->getMv(absPartAddr);

    curRefPOC = m_slice->m_refPOCList[picList][outRefIdx];

    scale = xGetDistScaleFactor(curPOC, curRefPOC, colPOC, colRefPOC);
    if (scale == 4096)
        outMV = colmv;
    else
        outMV = scaleMv(colmv, scale);

    return true;
}

int TComDataCU::xGetDistScaleFactor(int curPOC, int curRefPOC, int colPOC, int colRefPOC)
{
    int diffPocD = colPOC - colRefPOC;
    int diffPocB = curPOC - curRefPOC;

    if (diffPocD == diffPocB)
    {
        return 4096;
    }
    else
    {
        int tdb      = Clip3(-128, 127, diffPocB);
        int tdd      = Clip3(-128, 127, diffPocD);
        int x        = (0x4000 + abs(tdd / 2)) / tdd;
        int scale    = Clip3(-4096, 4095, (tdb * x + 32) >> 6);
        return scale;
    }
}

/**
 * \param cuMode
 * \param partIdx
 * \param outPartIdxCenter
 * \returns void
 */
void TComDataCU::xDeriveCenterIdx(uint32_t partIdx, uint32_t& outPartIdxCenter)
{
    uint32_t partAddr;
    int partWidth;
    int partHeight;

    getPartIndexAndSize(partIdx, partAddr, partWidth, partHeight);

    outPartIdxCenter = m_absIdxInLCU + partAddr; // partition origin.
    outPartIdxCenter = g_rasterToZscan[g_zscanToRaster[outPartIdxCenter]
                                       + (partHeight >> (LOG2_UNIT_SIZE + 1)) * m_pic->getNumPartInCUSize()
                                       + (partWidth  >> (LOG2_UNIT_SIZE + 1))];
}

ScanType TComDataCU::getCoefScanIdx(uint32_t absPartIdx, uint32_t log2TrSize, bool bIsLuma, bool bIsIntra)
{
    uint32_t dirMode;

    if (!bIsIntra)
        return SCAN_DIAG;

    //check that MDCS can be used for this TU

    if (bIsLuma)
    {
        if (log2TrSize > MDCS_LOG2_MAX_SIZE)
            return SCAN_DIAG;

        dirMode = getLumaIntraDir(absPartIdx);
    }
    else
    {
        if (log2TrSize > (MDCS_LOG2_MAX_SIZE - m_hChromaShift))
            return SCAN_DIAG;

        dirMode = getChromaIntraDir(absPartIdx);
        if (dirMode == DM_CHROMA_IDX)
        {
            dirMode = getLumaIntraDir((m_chromaFormat == X265_CSP_I444) ? absPartIdx : absPartIdx & 0xFC);
            dirMode = (m_chromaFormat == X265_CSP_I422) ? g_chroma422IntraAngleMappingTable[dirMode] : dirMode;
        }
    }

    if (abs((int)dirMode - VER_IDX) <= MDCS_ANGLE_LIMIT)
        return SCAN_HOR;
    else if (abs((int)dirMode - HOR_IDX) <= MDCS_ANGLE_LIMIT)
        return SCAN_VER;
    else
        return SCAN_DIAG;
}

void TComDataCU::getTUEntropyCodingParameters(TUEntropyCodingParameters &result, uint32_t absPartIdx, uint32_t log2TrSize, bool bIsLuma)
{
    // set the group layout
    result.log2TrSizeCG = log2TrSize - 2;

    // set the scan orders
    result.scanType = getCoefScanIdx(absPartIdx, log2TrSize, bIsLuma, isIntra(absPartIdx));
    result.scan     = g_scanOrder[result.scanType][log2TrSize - 2];
    result.scanCG   = g_scanOrderCG[result.scanType][result.log2TrSizeCG];

    if (log2TrSize == 2)
        result.firstSignificanceMapContext = 0;
    else if (log2TrSize == 3)
    {
        result.firstSignificanceMapContext = 9;
        if (result.scanType != SCAN_DIAG && bIsLuma)
            result.firstSignificanceMapContext += 6;
    }
    else
        result.firstSignificanceMapContext = bIsLuma ? 21 : 12;
}

uint32_t TComDataCU::getSCUAddr()
{
    return (m_cuAddr << g_maxFullDepth * 2) + m_absIdxInLCU;
}

//! \}
