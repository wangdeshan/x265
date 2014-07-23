/*****************************************************************************
* Copyright (C) 2013 x265 project
*
* Authors: Deepthi Nandakumar <deepthi@multicorewareinc.com>
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

#include "analysis.h"
#include "primitives.h"
#include "common.h"
#include "rdcost.h"
#include "encoder.h"
#include "PPA/ppa.h"

using namespace x265;

Analysis::Analysis()
{
    m_bestPredYuv     = NULL;
    m_bestResiYuv     = NULL;
    m_bestRecoYuv     = NULL;

    m_tmpPredYuv      = NULL;
    m_tmpResiYuv      = NULL;
    m_tmpRecoYuv      = NULL;
    m_bestMergeRecoYuv = NULL;
    m_origYuv         = NULL;
    for (int i = 0; i < MAX_PRED_TYPES; i++)
        m_modePredYuv[i] = NULL;
}

bool Analysis::init(Encoder* top)
{
    m_param = top->m_param;
    m_trQuant.init(top->m_bEnableRDOQ);

    if (!top->m_scalingList.m_bEnabled)
    {
        m_trQuant.setFlatScalingList();
        m_trQuant.setUseScalingList(false);
    }
    else
    {
        m_trQuant.setScalingList(&top->m_scalingList);
        m_trQuant.setUseScalingList(true);
    }

    m_rdCost.setPsyRdScale(m_param->psyRd);
    m_bEnableRDOQ = top->m_bEnableRDOQ;
    m_bFrameParallel = m_param->frameNumThreads > 1;
    m_numLayers = top->m_quadtreeTULog2MaxSize - top->m_quadtreeTULog2MinSize + 1;

    return initSearch();
}

bool Analysis::create(uint8_t totalDepth, uint32_t maxWidth)
{
    X265_CHECK(totalDepth <= MAX_CU_DEPTH, "invalid totalDepth\n");

    m_bestPredYuv = new TComYuv*[totalDepth];
    m_bestResiYuv = new ShortYuv*[totalDepth];
    m_bestRecoYuv = new TComYuv*[totalDepth];

    m_tmpPredYuv     = new TComYuv*[totalDepth];
    m_modePredYuv[0] = new TComYuv*[totalDepth];
    m_modePredYuv[1] = new TComYuv*[totalDepth];
    m_modePredYuv[2] = new TComYuv*[totalDepth];
    m_modePredYuv[3] = new TComYuv*[totalDepth];
    m_modePredYuv[4] = new TComYuv*[totalDepth];
    m_modePredYuv[5] = new TComYuv*[totalDepth];

    m_tmpResiYuv = new ShortYuv*[totalDepth];
    m_tmpRecoYuv = new TComYuv*[totalDepth];

    m_bestMergeRecoYuv = new TComYuv*[totalDepth];

    m_origYuv = new TComYuv*[totalDepth];

    int unitSize  = maxWidth >> totalDepth;
    int csp       = m_param->internalCsp;
    bool tqBypass = m_param->bCULossless || m_param->bLossless;

    m_memPool = new TComDataCU[totalDepth];

    bool ok = true;
    for (int i = 0; i < totalDepth; i++)
    {
        uint32_t numPartitions = 1 << ((totalDepth - i) << 1);
        uint32_t cuSize = maxWidth >> i;

        uint32_t sizeL = cuSize * cuSize;
        uint32_t sizeC = sizeL >> (CHROMA_H_SHIFT(csp) + CHROMA_V_SHIFT(csp));

        ok &= m_memPool[i].initialize(numPartitions, sizeL, sizeC, 8, tqBypass);

        m_interCU_2Nx2N[i]  = new TComDataCU;
        m_interCU_2Nx2N[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 0, tqBypass);

        m_interCU_2NxN[i]   = new TComDataCU;
        m_interCU_2NxN[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 1, tqBypass);

        m_interCU_Nx2N[i]   = new TComDataCU;
        m_interCU_Nx2N[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 2, tqBypass);

        m_intraInInterCU[i] = new TComDataCU;
        m_intraInInterCU[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 3, tqBypass);

        m_mergeCU[i]        = new TComDataCU;
        m_mergeCU[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 4, tqBypass);

        m_bestMergeCU[i]    = new TComDataCU;
        m_bestMergeCU[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 5, tqBypass);

        m_bestCU[i]         = new TComDataCU;
        m_bestCU[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 6, tqBypass);

        m_tempCU[i]         = new TComDataCU;
        m_tempCU[i]->create(&m_memPool[i], numPartitions, cuSize, unitSize, csp, 7, tqBypass);

        m_bestPredYuv[i] = new TComYuv;
        ok &= m_bestPredYuv[i]->create(cuSize, cuSize, csp);

        m_bestResiYuv[i] = new ShortYuv;
        ok &= m_bestResiYuv[i]->create(cuSize, cuSize, csp);

        m_bestRecoYuv[i] = new TComYuv;
        ok &= m_bestRecoYuv[i]->create(cuSize, cuSize, csp);

        m_tmpPredYuv[i] = new TComYuv;
        ok &= m_tmpPredYuv[i]->create(cuSize, cuSize, csp);

        for (int j = 0; j < MAX_PRED_TYPES; j++)
        {
            m_modePredYuv[j][i] = new TComYuv;
            ok &= m_modePredYuv[j][i]->create(cuSize, cuSize, csp);
        }

        m_tmpResiYuv[i] = new ShortYuv;
        ok &= m_tmpResiYuv[i]->create(cuSize, cuSize, csp);

        m_tmpRecoYuv[i] = new TComYuv;
        ok &= m_tmpRecoYuv[i]->create(cuSize, cuSize, csp);

        m_bestMergeRecoYuv[i] = new TComYuv;
        ok &= m_bestMergeRecoYuv[i]->create(cuSize, cuSize, csp);

        m_origYuv[i] = new TComYuv;
        ok &= m_origYuv[i]->create(cuSize, cuSize, csp);
    }

    m_bEncodeDQP = false;
    return ok;
}

void Analysis::destroy()
{
    for (unsigned int i = 0; i < g_maxCUDepth; i++)
    {
        m_memPool[i].destroy();

        delete m_interCU_2Nx2N[i];
        delete m_interCU_2NxN[i];
        delete m_interCU_Nx2N[i];
        delete m_intraInInterCU[i];
        delete m_mergeCU[i];
        delete m_bestMergeCU[i];
        delete m_bestCU[i];
        delete m_tempCU[i];

        if (m_bestPredYuv && m_bestPredYuv[i])
        {
            m_bestPredYuv[i]->destroy();
            delete m_bestPredYuv[i];
        }
        if (m_bestResiYuv && m_bestResiYuv[i])
        {
            m_bestResiYuv[i]->destroy();
            delete m_bestResiYuv[i];
        }
        if (m_bestRecoYuv && m_bestRecoYuv[i])
        {
            m_bestRecoYuv[i]->destroy();
            delete m_bestRecoYuv[i];
        }

        if (m_tmpPredYuv && m_tmpPredYuv[i])
        {
            m_tmpPredYuv[i]->destroy();
            delete m_tmpPredYuv[i];
        }
        for (int j = 0; j < MAX_PRED_TYPES; j++)
        {
            if (m_modePredYuv[j] && m_modePredYuv[j][i])
            {
                m_modePredYuv[j][i]->destroy();
                delete m_modePredYuv[j][i];
            }
        }

        if (m_tmpResiYuv && m_tmpResiYuv[i])
        {
            m_tmpResiYuv[i]->destroy();
            delete m_tmpResiYuv[i];
        }
        if (m_tmpRecoYuv && m_tmpRecoYuv[i])
        {
            m_tmpRecoYuv[i]->destroy();
            delete m_tmpRecoYuv[i];
        }
        if (m_bestMergeRecoYuv && m_bestMergeRecoYuv[i])
        {
            m_bestMergeRecoYuv[i]->destroy();
            delete m_bestMergeRecoYuv[i];
        }

        if (m_origYuv && m_origYuv[i])
        {
            m_origYuv[i]->destroy();
            delete m_origYuv[i];
        }
    }

    delete [] m_memPool;
    delete [] m_bestPredYuv;
    delete [] m_bestResiYuv;
    delete [] m_bestRecoYuv;
    delete [] m_bestMergeRecoYuv;
    delete [] m_tmpPredYuv;

    for (int i = 0; i < MAX_PRED_TYPES; i++)
        delete [] m_modePredYuv[i];

    delete [] m_tmpResiYuv;
    delete [] m_tmpRecoYuv;
    delete [] m_origYuv;
}

/* Lambda Partition Select adjusts the threshold value for Early Exit in No-RDO flow */
#define LAMBDA_PARTITION_SELECT     0.9
#define EARLY_EXIT                  1
#define TOPSKIP                     1

void Analysis::compressCU(TComDataCU* cu)
{
    if (cu->m_slice->m_pps->bUseDQP)
        m_bEncodeDQP = true;

    // initialize CU data
    m_bestCU[0]->initCU(cu->m_pic, cu->getAddr());
    m_tempCU[0]->initCU(cu->m_pic, cu->getAddr());

    // analysis of CU
    uint32_t numPartition = cu->getTotalNumPart();

    if (m_bestCU[0]->m_slice->m_sliceType == I_SLICE)
    {
        compressIntraCU(m_bestCU[0], m_tempCU[0], 0, false);
        if (m_param->bLogCuStats || m_param->rc.bStatWrite)
        {
            uint32_t i = 0, part;
            do
            {
                m_log->totalCu++;
                part = cu->getDepth(i);
                int next = numPartition >> (part * 2);
                m_log->qTreeIntraCnt[part]++;
                if (part == g_maxCUDepth - 1 && cu->getPartitionSize(i) != SIZE_2Nx2N)
                    m_log->cntIntraNxN++;
                else
                {
                    m_log->cntIntra[part]++;
                    if (cu->getLumaIntraDir(i) > 1)
                        m_log->cuIntraDistribution[part][ANGULAR_MODE_ID]++;
                    else
                        m_log->cuIntraDistribution[part][cu->getLumaIntraDir(i)]++;
                }
                i += next;
            }
            while (i < numPartition);
        }
    }
    else
    {
        if (m_param->rdLevel < 5)
        {
            TComDataCU* outBestCU = NULL;

            /* At the start of analysis, the best CU is a null pointer
            On return, it points to the CU encode with best chosen mode*/
            compressInterCU_rd0_4(outBestCU, m_tempCU[0], cu, 0, false, 0, 4);
        }
        else
            compressInterCU_rd5_6(m_bestCU[0], m_tempCU[0], 0, false);
        if (m_param->bLogCuStats || m_param->rc.bStatWrite)
        {
            uint32_t i = 0, part;
            do
            {
                part = cu->getDepth(i);
                m_log->cntTotalCu[part]++;
                int next = numPartition >> (part * 2);
                if (cu->isSkipped(i))
                {
                    m_log->cntSkipCu[part]++;
                    m_log->qTreeSkipCnt[part]++;
                }
                else
                {
                    m_log->totalCu++;
                    if (cu->getPredictionMode(0) == MODE_INTER)
                    {
                        m_log->cntInter[part]++;
                        m_log->qTreeInterCnt[part]++;
                        if (cu->getPartitionSize(0) < AMP_ID)
                            m_log->cuInterDistribution[part][cu->getPartitionSize(0)]++;
                        else
                            m_log->cuInterDistribution[part][AMP_ID]++;
                    }
                    else if (cu->getPredictionMode(0) == MODE_INTRA)
                    {
                        m_log->qTreeIntraCnt[part]++;
                        if (part == g_maxCUDepth - 1 && cu->getPartitionSize(0) == SIZE_NxN)
                        {
                            m_log->cntIntraNxN++;
                        }
                        else
                        {
                            m_log->cntIntra[part]++;
                            if (cu->getLumaIntraDir(0) > 1)
                                m_log->cuIntraDistribution[part][ANGULAR_MODE_ID]++;
                            else
                                m_log->cuIntraDistribution[part][cu->getLumaIntraDir(0)]++;
                        }
                    }
                }
                i = i + next;
            }
            while (i < numPartition);
        }
    }
}

void Analysis::compressIntraCU(TComDataCU*& outBestCU, TComDataCU*& outTempCU, uint8_t depth, bool bInsidePicture)
{
    //PPAScopeEvent(CompressIntraCU + depth);

    Frame* pic = outBestCU->m_pic;

    if (depth == 0)
        // get original YUV data from picture
        m_origYuv[depth]->copyFromPicYuv(pic->getPicYuvOrg(), outBestCU->getAddr(), outBestCU->getZorderIdxInCU());
    else
        // copy partition YUV from depth 0 CTU cache
        m_origYuv[0]->copyPartToYuv(m_origYuv[depth], outBestCU->getZorderIdxInCU());

    uint32_t log2CUSize = outTempCU->getLog2CUSize(0);
    Slice* slice = outTempCU->m_slice;
    if (!bInsidePicture)
    {
        uint32_t cuSize = 1 << log2CUSize;
        uint32_t lpelx = outBestCU->getCUPelX();
        uint32_t tpely = outBestCU->getCUPelY();
        uint32_t rpelx = lpelx + cuSize;
        uint32_t bpely = tpely + cuSize;
        bInsidePicture = (rpelx <= slice->m_sps->picWidthInLumaSamples &&
                          bpely <= slice->m_sps->picHeightInLumaSamples);
    }

    // We need to split, so don't try these modes.
    if (bInsidePicture)
    {
        m_trQuant.setQPforQuant(outTempCU);

        checkIntra(outBestCU, outTempCU, SIZE_2Nx2N);

        if (depth == g_maxCUDepth - g_addCUDepth)
        {
            if (log2CUSize > slice->m_sps->quadtreeTULog2MinSize)
                checkIntra(outBestCU, outTempCU, SIZE_NxN);
        }

        m_entropyCoder->resetBits();
        m_entropyCoder->codeSplitFlag(outBestCU, 0, depth);
        outBestCU->m_totalBits += m_entropyCoder->getNumberOfWrittenBits(); // split bits
        if (m_rdCost.psyRdEnabled())
            outBestCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outBestCU->m_totalDistortion, outBestCU->m_totalBits, outBestCU->m_psyEnergy);
        else
            outBestCU->m_totalRDCost  = m_rdCost.calcRdCost(outBestCU->m_totalDistortion, outBestCU->m_totalBits);
    }

    // copy original YUV samples in lossless mode
    if (outBestCU->isLosslessCoded(0))
        fillOrigYUVBuffer(outBestCU, m_origYuv[depth]);

    // further split
    if (depth < g_maxCUDepth - g_addCUDepth)
    {
        uint8_t     nextDepth     = (uint8_t)(depth + 1);
        TComDataCU* subBestPartCU = m_bestCU[nextDepth];
        TComDataCU* subTempPartCU = m_tempCU[nextDepth];
        uint32_t partUnitIdx = 0;
        for (; partUnitIdx < 4; partUnitIdx++)
        {
            int qp = outTempCU->getQP(0);
            subBestPartCU->initSubCU(outTempCU, partUnitIdx, nextDepth, qp); // clear sub partition datas or init.

            if (bInsidePicture ||
                ((subBestPartCU->getCUPelX() < slice->m_sps->picWidthInLumaSamples) &&
                 (subBestPartCU->getCUPelY() < slice->m_sps->picHeightInLumaSamples)))
            {
                subTempPartCU->initSubCU(outTempCU, partUnitIdx, nextDepth, qp); // clear sub partition datas or init.
                if (0 == partUnitIdx) //initialize RD with previous depth buffer
                {
                    m_rdEntropyCoders[nextDepth][CI_CURR_BEST].load(m_rdEntropyCoders[depth][CI_CURR_BEST]);
                }
                else
                {
                    m_rdEntropyCoders[nextDepth][CI_CURR_BEST].load(m_rdEntropyCoders[nextDepth][CI_NEXT_BEST]);
                }

                compressIntraCU(subBestPartCU, subTempPartCU, nextDepth, bInsidePicture);
                outTempCU->copyPartFrom(subBestPartCU, partUnitIdx, nextDepth); // Keep best part data to current temporary data.
                copyYuv2Tmp(subBestPartCU->getTotalNumPart() * partUnitIdx, nextDepth);
            }
            else
            {
                subBestPartCU->copyToPic(nextDepth);
                outTempCU->copyPartFrom(subBestPartCU, partUnitIdx, nextDepth);
            }
        }

        if (bInsidePicture)
        {
            m_entropyCoder->resetBits();
            m_entropyCoder->codeSplitFlag(outTempCU, 0, depth);
            outTempCU->m_totalBits += m_entropyCoder->getNumberOfWrittenBits(); // split bits
        }

        if (m_rdCost.psyRdEnabled())
            outTempCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits, outTempCU->m_psyEnergy);
        else
            outTempCU->m_totalRDCost = m_rdCost.calcRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);

        if ((g_maxCUSize >> depth) == slice->m_pps->minCuDQPSize && slice->m_pps->bUseDQP)
        {
            bool hasResidual = false;
            for (uint32_t blkIdx = 0; blkIdx < outTempCU->getTotalNumPart(); blkIdx++)
            {
                if (outTempCU->getCbf(blkIdx, TEXT_LUMA) || outTempCU->getCbf(blkIdx, TEXT_CHROMA_U) ||
                    outTempCU->getCbf(blkIdx, TEXT_CHROMA_V))
                {
                    hasResidual = true;
                    break;
                }
            }

            uint32_t targetPartIdx = 0;
            if (hasResidual)
            {
                bool foundNonZeroCbf = false;
                outTempCU->setQPSubCUs(outTempCU->getRefQP(targetPartIdx), outTempCU, 0, depth, foundNonZeroCbf);
                X265_CHECK(foundNonZeroCbf, "expected to find non-zero CBF\n");
            }
            else
                outTempCU->setQPSubParts(outTempCU->getRefQP(targetPartIdx), 0, depth); // set QP to default QP
        }

        m_rdEntropyCoders[nextDepth][CI_NEXT_BEST].store(m_rdEntropyCoders[depth][CI_TEMP_BEST]);
        checkBestMode(outBestCU, outTempCU, depth); // RD compare current CU against split
    }
    outBestCU->copyToPic(depth); // Copy Best data to Picture for next partition prediction.

    if (!bInsidePicture) return;

    // Copy Yuv data to picture Yuv
    copyYuv2Pic(pic, outBestCU->getAddr(), outBestCU->getZorderIdxInCU(), depth);

    X265_CHECK(outBestCU->getPartitionSize(0) != SIZE_NONE, "no best partition size\n");
    X265_CHECK(outBestCU->getPredictionMode(0) != MODE_NONE, "no best partition mode\n");
    if (m_rdCost.psyRdEnabled())
    {
        X265_CHECK(outBestCU->m_totalPsyCost != MAX_INT64, "no best partition cost\n");
    }
    else
    {
        X265_CHECK(outBestCU->m_totalRDCost != MAX_INT64, "no best partition cost\n");
    }
}

void Analysis::checkIntra(TComDataCU*& outBestCU, TComDataCU*& outTempCU, PartSize partSize)
{
    //PPAScopeEvent(CheckRDCostIntra + depth);
    uint32_t depth = outTempCU->getDepth(0);

    outTempCU->setSkipFlagSubParts(false, 0, depth);
    outTempCU->setPartSizeSubParts(partSize, 0, depth);
    outTempCU->setPredModeSubParts(MODE_INTRA, 0, depth);
    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    estIntraPredQT(outTempCU, m_origYuv[depth], m_tmpPredYuv[depth], m_tmpResiYuv[depth], m_tmpRecoYuv[depth]);

    estIntraPredChromaQT(outTempCU, m_origYuv[depth], m_tmpPredYuv[depth], m_tmpResiYuv[depth], m_tmpRecoYuv[depth]);

    m_entropyCoder->resetBits();
    if (outTempCU->m_slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder->codeCUTransquantBypassFlag(outTempCU, 0);

    if (!outTempCU->m_slice->isIntra())
    {
        m_entropyCoder->codeSkipFlag(outTempCU, 0);
        m_entropyCoder->codePredMode(outTempCU, 0);
    }
    m_entropyCoder->codePartSize(outTempCU, 0, depth);
    m_entropyCoder->codePredInfo(outTempCU, 0);
    outTempCU->m_mvBits = m_entropyCoder->getNumberOfWrittenBits();

    // Encode Coefficients
    bool bCodeDQP = m_bEncodeDQP;
    m_entropyCoder->codeCoeff(outTempCU, 0, depth, bCodeDQP);
    m_entropyCoder->store(m_rdEntropyCoders[depth][CI_TEMP_BEST]);
    outTempCU->m_totalBits = m_entropyCoder->getNumberOfWrittenBits();
    outTempCU->m_coeffBits = outTempCU->m_totalBits - outTempCU->m_mvBits;

    if (m_rdCost.psyRdEnabled())
    {
        int part = outTempCU->getLog2CUSize(0) - 2;
        outTempCU->m_psyEnergy = m_rdCost.psyCost(part, m_origYuv[depth]->getLumaAddr(), m_origYuv[depth]->getStride(),
                                                  m_tmpRecoYuv[depth]->getLumaAddr(), m_tmpRecoYuv[depth]->getStride());
        outTempCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits, outTempCU->m_psyEnergy);
    }
    else
        outTempCU->m_totalRDCost = m_rdCost.calcRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);

    checkDQP(outTempCU);
    checkBestMode(outBestCU, outTempCU, depth);
}

void Analysis::compressInterCU_rd0_4(TComDataCU*& outBestCU, TComDataCU*& outTempCU, TComDataCU* cu, uint8_t depth, bool bInsidePicture, uint32_t PartitionIndex, uint8_t minDepth)
{
    Frame* pic = outTempCU->m_pic;
    uint32_t absPartIdx = outTempCU->getZorderIdxInCU();

    if (depth == 0)
        // get original YUV data from picture
        m_origYuv[depth]->copyFromPicYuv(pic->getPicYuvOrg(), outTempCU->getAddr(), absPartIdx);
    else
        // copy partition YUV from depth 0 CTU cache
        m_origYuv[0]->copyPartToYuv(m_origYuv[depth], absPartIdx);

    // variables for fast encoder decision
    bool bSubBranch = true;
    int qp = outTempCU->getQP(0);

#if TOPSKIP
    bool bInsidePictureParent = bInsidePicture;
#endif

    Slice* slice = outTempCU->m_slice;
    if (!bInsidePicture)
    {
        int cuSize = 1 << outTempCU->getLog2CUSize(0);
        uint32_t lpelx = outTempCU->getCUPelX();
        uint32_t tpely = outTempCU->getCUPelY();
        uint32_t rpelx = lpelx + cuSize;
        uint32_t bpely = tpely + cuSize;
        bInsidePicture = (rpelx <= slice->m_sps->picWidthInLumaSamples &&
                          bpely <= slice->m_sps->picHeightInLumaSamples);
    }

    if (depth == 0 && m_param->rdLevel == 0)
    {
        m_origYuv[depth]->copyToPicYuv(pic->getPicYuvRec(), cu->getAddr(), 0);
    }
    // We need to split, so don't try these modes.
#if TOPSKIP
    if (bInsidePicture && !bInsidePictureParent)
    {
        TComDataCU* colocated0 = slice->m_numRefIdx[0] > 0 ? slice->m_refPicList[0][0]->getCU(outTempCU->getAddr()) : NULL;
        TComDataCU* colocated1 = slice->m_numRefIdx[1] > 0 ? slice->m_refPicList[1][0]->getCU(outTempCU->getAddr()) : NULL;
        char currentQP = outTempCU->getQP(0);
        char previousQP = colocated0->getQP(0);
        uint8_t delta = 0, minDepth0 = 4, minDepth1 = 4;
        uint32_t sum0 = 0, sum1 = 0;
        uint32_t numPartitions = outTempCU->getTotalNumPart();
        for (uint32_t i = 0; i < numPartitions; i = i + 4)
        {
            uint32_t j = absPartIdx + i;
            if (colocated0 && colocated0->getDepth(j) < minDepth0)
                minDepth0 = colocated0->getDepth(j);
            if (colocated1 && colocated1->getDepth(j) < minDepth1)
                minDepth1 = colocated1->getDepth(j);
            if (colocated0)
                sum0 += (colocated0->getDepth(j) * 4);
            if (colocated1)
                sum1 += (colocated1->getDepth(j) * 4);
        }

        uint32_t avgDepth2 = (sum0 + sum1) / numPartitions;
        minDepth = X265_MIN(minDepth0, minDepth1);
        if (((currentQP - previousQP) < 0) || (((currentQP - previousQP) >= 0) && ((avgDepth2 - 2 * minDepth) > 1)))
            delta = 0;
        else
            delta = 1;
        if (minDepth > 0)
            minDepth = minDepth - delta;
    }
    if (!(depth < minDepth)) //topskip
#endif // if TOPSKIP
    {
        if (bInsidePicture)
        {
            /* Initialise all Mode-CUs based on parentCU */
            if (depth == 0)
            {
                m_interCU_2Nx2N[depth]->initCU(pic, cu->getAddr());
                m_interCU_Nx2N[depth]->initCU(pic, cu->getAddr());
                m_interCU_2NxN[depth]->initCU(pic, cu->getAddr());
                m_intraInInterCU[depth]->initCU(pic, cu->getAddr());
                m_mergeCU[depth]->initCU(pic, cu->getAddr());
                m_bestMergeCU[depth]->initCU(pic, cu->getAddr());
            }
            else
            {
                m_interCU_2Nx2N[depth]->initSubCU(cu, PartitionIndex, depth, qp);
                m_interCU_2NxN[depth]->initSubCU(cu, PartitionIndex, depth, qp);
                m_interCU_Nx2N[depth]->initSubCU(cu, PartitionIndex, depth, qp);
                m_intraInInterCU[depth]->initSubCU(cu, PartitionIndex, depth, qp);
                m_mergeCU[depth]->initSubCU(cu, PartitionIndex, depth, qp);
                m_bestMergeCU[depth]->initSubCU(cu, PartitionIndex, depth, qp);
            }

            /* Compute  Merge Cost */
            checkMerge2Nx2N_rd0_4(m_bestMergeCU[depth], m_mergeCU[depth], m_modePredYuv[3][depth], m_bestMergeRecoYuv[depth]);
            bool earlyskip = false;
            if (m_param->rdLevel >= 1)
                earlyskip = (m_param->bEnableEarlySkip && m_bestMergeCU[depth]->isSkipped(0));

            if (!earlyskip)
            {
                /* Compute 2Nx2N mode costs */
                {
                    checkInter_rd0_4(m_interCU_2Nx2N[depth], m_modePredYuv[0][depth], SIZE_2Nx2N);
                    /* Choose best mode; initialise outBestCU to 2Nx2N */
                    outBestCU = m_interCU_2Nx2N[depth];
                    std::swap(m_bestPredYuv[depth], m_modePredYuv[0][depth]);
                }

                /* Compute Rect costs */
                if (m_param->bEnableRectInter)
                {
                    checkInter_rd0_4(m_interCU_Nx2N[depth], m_modePredYuv[1][depth], SIZE_Nx2N);
                    checkInter_rd0_4(m_interCU_2NxN[depth], m_modePredYuv[2][depth], SIZE_2NxN);
                    if (m_interCU_Nx2N[depth]->m_sa8dCost < outBestCU->m_sa8dCost)
                    {
                        outBestCU = m_interCU_Nx2N[depth];
                        std::swap(m_bestPredYuv[depth], m_modePredYuv[1][depth]);
                    }
                    if (m_interCU_2NxN[depth]->m_sa8dCost < outBestCU->m_sa8dCost)
                    {
                        outBestCU = m_interCU_2NxN[depth];
                        std::swap(m_bestPredYuv[depth], m_modePredYuv[2][depth]);
                    }
                }

                if (m_param->rdLevel > 2)
                {
                    // calculate the motion compensation for chroma for the best mode selected
                    int numPart = outBestCU->getNumPartInter();
                    for (int partIdx = 0; partIdx < numPart; partIdx++)
                    {
                        outBestCU->getPartIndexAndSize(partIdx, m_partAddr, m_width, m_height);
                        motionCompensation(outBestCU, m_bestPredYuv[depth], REF_PIC_LIST_X, false, true);
                    }

                    encodeResAndCalcRdInterCU(outBestCU, m_origYuv[depth], m_bestPredYuv[depth], m_tmpResiYuv[depth],
                                              m_bestResiYuv[depth], m_bestRecoYuv[depth], false, true);
                    uint64_t bestMergeCost = m_rdCost.psyRdEnabled() ? m_bestMergeCU[depth]->m_totalPsyCost : m_bestMergeCU[depth]->m_totalRDCost;
                    uint64_t bestCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;
                    if (bestMergeCost < bestCost)
                    {
                        outBestCU = m_bestMergeCU[depth];
                        std::swap(m_bestPredYuv[depth], m_modePredYuv[3][depth]);
                        std::swap(m_bestRecoYuv[depth], m_bestMergeRecoYuv[depth]);
                    }
                    else
                    {
                        m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
                    }
                }

                /* Check for Intra in inter frames only if its a P-slice*/
                if (slice->m_sliceType == P_SLICE)
                {
                    /* compute intra cost */
                    bool bdoIntra = true;

                    if (m_param->rdLevel > 2)
                    {
                        bdoIntra = (outBestCU->getCbf(0, TEXT_LUMA) ||  outBestCU->getCbf(0, TEXT_CHROMA_U) ||
                                    outBestCU->getCbf(0, TEXT_CHROMA_V));
                    }
                    if (bdoIntra)
                    {
                        checkIntraInInter_rd0_4(m_intraInInterCU[depth], SIZE_2Nx2N);
                        uint64_t intraInInterCost, bestCost;
                        if (m_param->rdLevel > 2)
                        {
                            encodeIntraInInter(m_intraInInterCU[depth], m_origYuv[depth], m_modePredYuv[5][depth],
                                                m_tmpResiYuv[depth],  m_tmpRecoYuv[depth]);
                            intraInInterCost = m_rdCost.psyRdEnabled() ? m_intraInInterCU[depth]->m_totalPsyCost : m_intraInInterCU[depth]->m_totalRDCost;
                            bestCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;
                        }
                        else
                        {
                            intraInInterCost = m_intraInInterCU[depth]->m_sa8dCost;
                            bestCost = outBestCU->m_sa8dCost;

                        }
                        if (intraInInterCost < bestCost)
                        {
                            outBestCU = m_intraInInterCU[depth];
                            std::swap(m_bestPredYuv[depth], m_modePredYuv[5][depth]);
                            std::swap(m_bestRecoYuv[depth], m_tmpRecoYuv[depth]);
                            if (m_param->rdLevel > 2)
                            {
                                m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
                            }
                        }
                    }
                }
                if (m_param->rdLevel == 2)
                {
                    if (m_bestMergeCU[depth]->m_sa8dCost < outBestCU->m_sa8dCost)
                    {
                        outBestCU = m_bestMergeCU[depth];
                        std::swap(m_bestPredYuv[depth], m_modePredYuv[3][depth]);
                        std::swap(m_bestRecoYuv[depth], m_bestMergeRecoYuv[depth]);
                    }
                    else if (outBestCU->getPredictionMode(0) == MODE_INTER)
                    {
                        int numPart = outBestCU->getNumPartInter();
                        for (int partIdx = 0; partIdx < numPart; partIdx++)
                        {
                            outBestCU->getPartIndexAndSize(partIdx, m_partAddr, m_width, m_height);
                            motionCompensation(outBestCU, m_bestPredYuv[depth], REF_PIC_LIST_X, false, true);
                        }

                        encodeResAndCalcRdInterCU(outBestCU, m_origYuv[depth], m_bestPredYuv[depth], m_tmpResiYuv[depth],
                                                  m_bestResiYuv[depth], m_bestRecoYuv[depth], false, true);
                        m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
                    }
                    else if (outBestCU->getPredictionMode(0) == MODE_INTRA)
                        encodeIntraInInter(outBestCU, m_origYuv[depth], m_bestPredYuv[depth], m_tmpResiYuv[depth],  m_bestRecoYuv[depth]);
                        m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
                }
                else if (m_param->rdLevel == 1)
                {
                    if (m_bestMergeCU[depth]->m_sa8dCost < outBestCU->m_sa8dCost)
                    {
                        outBestCU = m_bestMergeCU[depth];
                        std::swap(m_bestPredYuv[depth], m_modePredYuv[3][depth]);
                        std::swap(m_bestRecoYuv[depth], m_bestMergeRecoYuv[depth]);
                    }
                    else if (outBestCU->getPredictionMode(0) == MODE_INTER)
                    {
                        int numPart = outBestCU->getNumPartInter();
                        for (int partIdx = 0; partIdx < numPart; partIdx++)
                        {
                            outBestCU->getPartIndexAndSize(partIdx, m_partAddr, m_width, m_height);
                            motionCompensation(outBestCU, m_bestPredYuv[depth], REF_PIC_LIST_X, false, true);
                        }

                        m_tmpResiYuv[depth]->subtract(m_origYuv[depth], m_bestPredYuv[depth], outBestCU->getLog2CUSize(0));
                        generateCoeffRecon(outBestCU, m_origYuv[depth], m_bestPredYuv[depth], m_tmpResiYuv[depth], m_bestRecoYuv[depth], false);
                    }
                    else
                        generateCoeffRecon(outBestCU, m_origYuv[depth], m_bestPredYuv[depth], m_tmpResiYuv[depth], m_bestRecoYuv[depth], false);
                }
                else if (m_param->rdLevel == 0)
                {
                    if (outBestCU->getPredictionMode(0) == MODE_INTER)
                    {
                        int numPart = outBestCU->getNumPartInter();
                        for (int partIdx = 0; partIdx < numPart; partIdx++)
                        {
                            outBestCU->getPartIndexAndSize(partIdx, m_partAddr, m_width, m_height);
                            motionCompensation(outBestCU, m_bestPredYuv[depth], REF_PIC_LIST_X, false, true);
                        }
                    }
                }
            }
            else
            {
                outBestCU = m_bestMergeCU[depth];
                std::swap(m_bestPredYuv[depth], m_modePredYuv[3][depth]);
                std::swap(m_bestRecoYuv[depth], m_bestMergeRecoYuv[depth]);
            }

            if (m_param->rdLevel > 0) // checkDQP can be done only after residual encoding is done
                checkDQP(outBestCU);
            /* Disable recursive analysis for whole CUs temporarily */
            if ((outBestCU != 0) && (outBestCU->isSkipped(0)))
                bSubBranch = false;
            else
                bSubBranch = true;

            if (m_param->rdLevel > 1)
            {
                m_entropyCoder->resetBits();
                m_entropyCoder->codeSplitFlag(outBestCU, 0, depth);
                outBestCU->m_totalBits += m_entropyCoder->getNumberOfWrittenBits(); // split bits
                if (m_rdCost.psyRdEnabled())
                    outBestCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outBestCU->m_totalDistortion, outBestCU->m_totalBits, outBestCU->m_psyEnergy);
                else
                    outBestCU->m_totalRDCost = m_rdCost.calcRdCost(outBestCU->m_totalDistortion, outBestCU->m_totalBits);
            }

            // copy original YUV samples in lossless mode
            if (outBestCU->isLosslessCoded(0))
                fillOrigYUVBuffer(outBestCU, m_origYuv[depth]);
            }
        }

    // further split
    if (bSubBranch && depth < g_maxCUDepth - g_addCUDepth)
    {
#if EARLY_EXIT // turn ON this to enable early exit
        // early exit when the RD cost of best mode at depth n is less than the sum of avgerage of RD cost of the neighbour
        // CU's(above, aboveleft, aboveright, left, colocated) and avg cost of that CU at depth "n"  with weightage for each quantity
#if TOPSKIP
        if (outBestCU != 0 && !(depth < minDepth)) //topskip
#else
        if (outBestCU != 0)
#endif
        {
            uint64_t totalCostNeigh = 0, totalCostCU = 0, totalCountNeigh = 0, totalCountCU = 0;
            TComDataCU* above = outTempCU->getCUAbove();
            TComDataCU* aboveLeft = outTempCU->getCUAboveLeft();
            TComDataCU* aboveRight = outTempCU->getCUAboveRight();
            TComDataCU* left = outTempCU->getCULeft();
            TComDataCU* rootCU = pic->getPicSym()->getCU(outTempCU->getAddr());

            totalCostCU += rootCU->m_avgCost[depth] * rootCU->m_count[depth];
            totalCountCU += rootCU->m_count[depth];
            if (above)
            {
                totalCostNeigh += above->m_avgCost[depth] * above->m_count[depth];
                totalCountNeigh += above->m_count[depth];
            }
            if (aboveLeft)
            {
                totalCostNeigh += aboveLeft->m_avgCost[depth] * aboveLeft->m_count[depth];
                totalCountNeigh += aboveLeft->m_count[depth];
            }
            if (aboveRight)
            {
                totalCostNeigh += aboveRight->m_avgCost[depth] * aboveRight->m_count[depth];
                totalCountNeigh += aboveRight->m_count[depth];
            }
            if (left)
            {
                totalCostNeigh += left->m_avgCost[depth] * left->m_count[depth];
                totalCountNeigh += left->m_count[depth];
            }

            // give 60% weight to all CU's and 40% weight to neighbour CU's
            uint64_t avgCost = 0;
            if (totalCountNeigh + totalCountCU)
                avgCost = ((3 * totalCostCU) + (2 * totalCostNeigh)) / ((3 * totalCountCU) + (2 * totalCountNeigh));
            uint64_t bestavgCost = 0;
            if (m_param->rdLevel > 1)
                bestavgCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;
            else
                bestavgCost = outBestCU->m_totalRDCost;

            if (bestavgCost < avgCost && avgCost != 0 && depth != 0)
            {
                /* Copy Best data to Picture for next partition prediction. */
                outBestCU->copyToPic((uint8_t)depth);

                /* Copy Yuv data to picture Yuv */
                if (m_param->rdLevel != 0)
                    copyYuv2Pic(pic, outBestCU->getAddr(), absPartIdx, depth);
                return;
            }
        }
#endif // if EARLY_EXIT
        outTempCU->setQPSubParts(qp, 0, depth);
        uint8_t     nextDepth = (uint8_t)(depth + 1);
        TComDataCU* subBestPartCU;
        TComDataCU* subTempPartCU = m_tempCU[nextDepth];
        for (uint32_t nextDepth_partIndex = 0; nextDepth_partIndex < 4; nextDepth_partIndex++)
        {
            subBestPartCU = NULL;
            subTempPartCU->initSubCU(outTempCU, nextDepth_partIndex, nextDepth, qp); // clear sub partition datas or init.

            if (bInsidePicture ||
                ((subTempPartCU->getCUPelX() < slice->m_sps->picWidthInLumaSamples) &&
                 (subTempPartCU->getCUPelY() < slice->m_sps->picHeightInLumaSamples)))
            {
                if (0 == nextDepth_partIndex) // initialize RD with previous depth buffer
                    m_rdEntropyCoders[nextDepth][CI_CURR_BEST].load(m_rdEntropyCoders[depth][CI_CURR_BEST]);
                else
                    m_rdEntropyCoders[nextDepth][CI_CURR_BEST].load(m_rdEntropyCoders[nextDepth][CI_NEXT_BEST]);

                compressInterCU_rd0_4(subBestPartCU, subTempPartCU, outTempCU, nextDepth, bInsidePicture, nextDepth_partIndex, minDepth);
#if EARLY_EXIT
                if (subBestPartCU->getPredictionMode(0) != MODE_INTRA)
                {
                    uint64_t tempavgCost = 0;
                    if (m_param->rdLevel > 1)
                        tempavgCost = m_rdCost.psyRdEnabled() ? subBestPartCU->m_totalPsyCost : subBestPartCU->m_totalRDCost;
                    else
                        tempavgCost = subBestPartCU->m_totalRDCost;
                    TComDataCU* rootCU = pic->getPicSym()->getCU(outTempCU->getAddr());
                    uint64_t temp = rootCU->m_avgCost[depth + 1] * rootCU->m_count[depth + 1];
                    rootCU->m_count[depth + 1] += 1;
                    rootCU->m_avgCost[depth + 1] = (temp + tempavgCost) / rootCU->m_count[depth + 1];
                }
#endif // if EARLY_EXIT
                /* Adding costs from best SUbCUs */
                outTempCU->copyPartFrom(subBestPartCU, nextDepth_partIndex, nextDepth, true); // Keep best part data to current temporary data.
                if (m_param->rdLevel != 0)
                    m_bestRecoYuv[nextDepth]->copyToPartYuv(m_tmpRecoYuv[depth], subBestPartCU->getTotalNumPart() * nextDepth_partIndex);
                else
                    m_bestPredYuv[nextDepth]->copyToPartYuv(m_tmpPredYuv[depth], subBestPartCU->getTotalNumPart() * nextDepth_partIndex);
            }
            else
            {
                subTempPartCU->copyToPic((uint8_t)nextDepth);
                outTempCU->copyPartFrom(subTempPartCU, nextDepth_partIndex, nextDepth, false);
            }
        }

        if (bInsidePicture)
        {
            if (m_param->rdLevel > 1)
            {
                m_entropyCoder->resetBits();
                m_entropyCoder->codeSplitFlag(outTempCU, 0, depth);
                outTempCU->m_totalBits += m_entropyCoder->getNumberOfWrittenBits(); // split bits
            }
        }
        if (m_param->rdLevel > 1)
        {
            if (m_rdCost.psyRdEnabled())
                outTempCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits, outTempCU->m_psyEnergy);
            else
                outTempCU->m_totalRDCost = m_rdCost.calcRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);
        }
        else
            outTempCU->m_sa8dCost = m_rdCost.calcRdSADCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);

        if ((g_maxCUSize >> depth) == slice->m_pps->minCuDQPSize && slice->m_pps->bUseDQP)
        {
            bool hasResidual = false;
            for (uint32_t blkIdx = 0; blkIdx < outTempCU->getTotalNumPart(); blkIdx++)
            {
                if (outTempCU->getCbf(blkIdx, TEXT_LUMA) || outTempCU->getCbf(blkIdx, TEXT_CHROMA_U) ||
                    outTempCU->getCbf(blkIdx, TEXT_CHROMA_V))
                {
                    hasResidual = true;
                    break;
                }
            }

            uint32_t targetPartIdx = 0;
            if (hasResidual)
            {
                bool foundNonZeroCbf = false;
                outTempCU->setQPSubCUs(outTempCU->getRefQP(targetPartIdx), outTempCU, 0, depth, foundNonZeroCbf);
                X265_CHECK(foundNonZeroCbf, "setQPSubCUs did not find non-zero Cbf\n");
            }
            else
            {
                outTempCU->setQPSubParts(outTempCU->getRefQP(targetPartIdx), 0, depth); // set QP to default QP
            }
        }

        m_rdEntropyCoders[nextDepth][CI_NEXT_BEST].store(m_rdEntropyCoders[depth][CI_TEMP_BEST]);

        /* If Best Mode is not NULL; then compare costs. Else assign best mode to Sub-CU costs
         * Copy recon data from Temp structure to Best structure */
        if (outBestCU)
        {
#if EARLY_EXIT
            if (depth == 0)
            {
                uint64_t tempavgCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;
                TComDataCU* rootCU = pic->getPicSym()->getCU(outTempCU->getAddr());
                uint64_t temp = rootCU->m_avgCost[depth] * rootCU->m_count[depth];
                rootCU->m_count[depth] += 1;
                rootCU->m_avgCost[depth] = (temp + tempavgCost) / rootCU->m_count[depth];
            }
#endif
            uint64_t tempCost = m_rdCost.psyRdEnabled() ? outTempCU->m_totalPsyCost : outTempCU->m_totalRDCost;
            uint64_t bestCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost; 
            if (tempCost < bestCost)
            {
                outBestCU = outTempCU;
                std::swap(m_bestRecoYuv[depth], m_tmpRecoYuv[depth]);
                std::swap(m_bestPredYuv[depth], m_tmpPredYuv[depth]);
            }
        }
        else
        {
            outBestCU = outTempCU;
            std::swap(m_bestRecoYuv[depth], m_tmpRecoYuv[depth]);
            std::swap(m_bestPredYuv[depth], m_tmpPredYuv[depth]);
        }
    }

    /* Copy Best data to Picture for next partition prediction. */
    outBestCU->copyToPic((uint8_t)depth);

    if (m_param->rdLevel == 0 && depth == 0)
        encodeResidue(outBestCU, outBestCU, 0, 0);
    else if (m_param->rdLevel != 0)
    {
        /* Copy Yuv data to picture Yuv */
        if (bInsidePicture)
            copyYuv2Pic(pic, outBestCU->getAddr(), absPartIdx, depth);
    }

    /* Assert if Best prediction mode is NONE
     * Selected mode's RD-cost must be not MAX_INT64 */
    if (bInsidePicture)
    {
        X265_CHECK(outBestCU->getPartitionSize(0) != SIZE_NONE, "no best prediction size\n");
        X265_CHECK(outBestCU->getPredictionMode(0) != MODE_NONE, "no best prediction mode\n");
        X265_CHECK(outBestCU->m_totalRDCost != MAX_INT64, "no best prediction cost\n");
    }

    x265_emms();
}

void Analysis::compressInterCU_rd5_6(TComDataCU*& outBestCU, TComDataCU*& outTempCU, uint8_t depth, bool bInsidePicture, PartSize parentSize)
{
    //PPAScopeEvent(CompressCU + depth);

    Frame* pic = outBestCU->m_pic;

    if (depth == 0)
        // get original YUV data from picture
        m_origYuv[depth]->copyFromPicYuv(pic->getPicYuvOrg(), outBestCU->getAddr(), outBestCU->getZorderIdxInCU());
    else
        // copy partition YUV from depth 0 CTU cache
        m_origYuv[0]->copyPartToYuv(m_origYuv[depth], outBestCU->getZorderIdxInCU());

    // variable for Early CU determination
    bool bSubBranch = true;

    // variable for Cbf fast mode PU decision
    bool doNotBlockPu = true;
    bool earlyDetectionSkipMode = false;

    uint32_t log2CUSize = outTempCU->getLog2CUSize(0);
    Slice* slice = outTempCU->m_slice;
    if (!bInsidePicture)
    {
        uint32_t cuSize = 1 << log2CUSize;
        uint32_t lpelx = outBestCU->getCUPelX();
        uint32_t tpely = outBestCU->getCUPelY();
        uint32_t rpelx = lpelx + cuSize;
        uint32_t bpely = tpely + cuSize;
        bInsidePicture = (rpelx <= slice->m_sps->picWidthInLumaSamples &&
                          bpely <= slice->m_sps->picHeightInLumaSamples);
    }

    // We need to split, so don't try these modes.
    if (bInsidePicture)
    {
        m_trQuant.setQPforQuant(outTempCU);

        // do inter modes, SKIP and 2Nx2N
        if (slice->m_sliceType != I_SLICE)
        {
            // 2Nx2N
            if (m_param->bEnableEarlySkip)
            {
                checkInter_rd5_6(outBestCU, outTempCU, SIZE_2Nx2N);
                outTempCU->initEstData(); // by competition for inter_2Nx2N
            }
            // by Merge for inter_2Nx2N
            checkMerge2Nx2N_rd5_6(outBestCU, outTempCU, &earlyDetectionSkipMode, m_bestPredYuv[depth], m_bestRecoYuv[depth]);

            outTempCU->initEstData();

            if (!m_param->bEnableEarlySkip)
            {
                // 2Nx2N, NxN
                checkInter_rd5_6(outBestCU, outTempCU, SIZE_2Nx2N);
                outTempCU->initEstData();
                if (m_param->bEnableCbfFastMode)
                    doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
            }
        }

        if (!earlyDetectionSkipMode)
        {
            outTempCU->initEstData();

            // do inter modes, NxN, 2NxN, and Nx2N
            if (slice->m_sliceType != I_SLICE)
            {
                // 2Nx2N, NxN
                if (!(log2CUSize == 3))
                {
                    if (depth == g_maxCUDepth - g_addCUDepth && doNotBlockPu)
                    {
                        checkInter_rd5_6(outBestCU, outTempCU, SIZE_NxN);
                        outTempCU->initEstData();
                    }
                }

                if (m_param->bEnableRectInter)
                {
                    // 2NxN, Nx2N
                    if (doNotBlockPu)
                    {
                        checkInter_rd5_6(outBestCU, outTempCU, SIZE_Nx2N);
                        outTempCU->initEstData();
                        if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_Nx2N)
                            doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                    }
                    if (doNotBlockPu)
                    {
                        checkInter_rd5_6(outBestCU, outTempCU, SIZE_2NxN);
                        outTempCU->initEstData();
                        if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_2NxN)
                            doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                    }
                }

                // Try AMP (SIZE_2NxnU, SIZE_2NxnD, SIZE_nLx2N, SIZE_nRx2N)
                if (slice->m_sps->maxAMPDepth > depth)
                {
                    bool bTestAMP_Hor = false, bTestAMP_Ver = false;
                    bool bTestMergeAMP_Hor = false, bTestMergeAMP_Ver = false;

                    deriveTestModeAMP(outBestCU, parentSize, bTestAMP_Hor, bTestAMP_Ver, bTestMergeAMP_Hor, bTestMergeAMP_Ver);

                    // Do horizontal AMP
                    if (bTestAMP_Hor)
                    {
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_2NxnU);
                            outTempCU->initEstData();
                            if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_2NxnU)
                                doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                        }
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_2NxnD);
                            outTempCU->initEstData();
                            if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_2NxnD)
                                doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                        }
                    }
                    else if (bTestMergeAMP_Hor)
                    {
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_2NxnU, true);
                            outTempCU->initEstData();
                            if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_2NxnU)
                                doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                        }
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_2NxnD, true);
                            outTempCU->initEstData();
                            if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_2NxnD)
                                doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                        }
                    }

                    // Do horizontal AMP
                    if (bTestAMP_Ver)
                    {
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_nLx2N);
                            outTempCU->initEstData();
                            if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_nLx2N)
                                doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                        }
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_nRx2N);
                            outTempCU->initEstData();
                        }
                    }
                    else if (bTestMergeAMP_Ver)
                    {
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_nLx2N, true);
                            outTempCU->initEstData();
                            if (m_param->bEnableCbfFastMode && outBestCU->getPartitionSize(0) == SIZE_nLx2N)
                                doNotBlockPu = outBestCU->getQtRootCbf(0) != 0;
                        }
                        if (doNotBlockPu)
                        {
                            checkInter_rd5_6(outBestCU, outTempCU, SIZE_nRx2N, true);
                            outTempCU->initEstData();
                        }
                    }
                }
            }

            // speedup for inter frames, avoid intra in special cases
            bool doIntra = slice->m_sliceType == B_SLICE ? !!m_param->bIntraInBFrames : true;
            if ((outBestCU->getCbf(0, TEXT_LUMA)     != 0   ||
                 outBestCU->getCbf(0, TEXT_CHROMA_U) != 0   ||
                 outBestCU->getCbf(0, TEXT_CHROMA_V) != 0)  && doIntra)
            {
                checkIntraInInter_rd5_6(outBestCU, outTempCU, SIZE_2Nx2N);
                outTempCU->initEstData();

                if (depth == g_maxCUDepth - g_addCUDepth)
                {
                    if (log2CUSize > slice->m_sps->quadtreeTULog2MinSize)
                    {
                        checkIntraInInter_rd5_6(outBestCU, outTempCU, SIZE_NxN);
                        outTempCU->initEstData();
                    }
                }
            }
        }

        m_entropyCoder->resetBits();
        m_entropyCoder->codeSplitFlag(outBestCU, 0, depth);
        outBestCU->m_totalBits += m_entropyCoder->getNumberOfWrittenBits(); // split bits
        if (m_rdCost.psyRdEnabled())
            outBestCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outBestCU->m_totalDistortion, outBestCU->m_totalBits, outBestCU->m_psyEnergy);
        else
            outBestCU->m_totalRDCost = m_rdCost.calcRdCost(outBestCU->m_totalDistortion, outBestCU->m_totalBits);

        // Early CU determination
        if (outBestCU->isSkipped(0))
            bSubBranch = false;
        else
            bSubBranch = true;
    }

    // copy original YUV samples in lossless mode
    if (outBestCU->isLosslessCoded(0))
    {
        fillOrigYUVBuffer(outBestCU, m_origYuv[depth]);
    }

    // further split
    if (bSubBranch && depth < g_maxCUDepth - g_addCUDepth)
    {
        uint8_t     nextDepth     = depth + 1;
        TComDataCU* subBestPartCU = m_bestCU[nextDepth];
        TComDataCU* subTempPartCU = m_tempCU[nextDepth];
        uint32_t partUnitIdx = 0;
        for (; partUnitIdx < 4; partUnitIdx++)
        {
            int qp = outTempCU->getQP(0);
            subBestPartCU->initSubCU(outTempCU, partUnitIdx, nextDepth, qp); // clear sub partition datas or init.

            if (bInsidePicture ||
                ((subBestPartCU->getCUPelX() < slice->m_sps->picWidthInLumaSamples) &&
                 (subBestPartCU->getCUPelY() < slice->m_sps->picHeightInLumaSamples)))
            {
                subTempPartCU->initSubCU(outTempCU, partUnitIdx, nextDepth, qp); // clear sub partition datas or init.

                if (0 == partUnitIdx) //initialize RD with previous depth buffer
                    m_rdEntropyCoders[nextDepth][CI_CURR_BEST].load(m_rdEntropyCoders[depth][CI_CURR_BEST]);
                else
                    m_rdEntropyCoders[nextDepth][CI_CURR_BEST].load(m_rdEntropyCoders[nextDepth][CI_NEXT_BEST]);

                compressInterCU_rd5_6(subBestPartCU, subTempPartCU, nextDepth, bInsidePicture);
                outTempCU->copyPartFrom(subBestPartCU, partUnitIdx, nextDepth); // Keep best part data to current temporary data.
                copyYuv2Tmp(subBestPartCU->getTotalNumPart() * partUnitIdx, nextDepth);
            }
            else
            {
                subBestPartCU->copyToPic(nextDepth);
                outTempCU->copyPartFrom(subBestPartCU, partUnitIdx, nextDepth);
            }
        }

        if (bInsidePicture)
        {
            m_entropyCoder->resetBits();
            m_entropyCoder->codeSplitFlag(outTempCU, 0, depth);
            outTempCU->m_totalBits += m_entropyCoder->getNumberOfWrittenBits(); // split bits
        }

        if (m_rdCost.psyRdEnabled())
            outTempCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits, outTempCU->m_psyEnergy);
        else
            outTempCU->m_totalRDCost = m_rdCost.calcRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);

        if ((g_maxCUSize >> depth) == slice->m_pps->minCuDQPSize && slice->m_pps->bUseDQP)
        {
            bool hasResidual = false;
            for (uint32_t blkIdx = 0; blkIdx < outTempCU->getTotalNumPart(); blkIdx++)
            {
                if (outTempCU->getCbf(blkIdx, TEXT_LUMA) || outTempCU->getCbf(blkIdx, TEXT_CHROMA_U) ||
                    outTempCU->getCbf(blkIdx, TEXT_CHROMA_V))
                {
                    hasResidual = true;
                    break;
                }
            }

            uint32_t targetPartIdx = 0;
            if (hasResidual)
            {
                bool foundNonZeroCbf = false;
                outTempCU->setQPSubCUs(outTempCU->getRefQP(targetPartIdx), outTempCU, 0, depth, foundNonZeroCbf);
                X265_CHECK(foundNonZeroCbf, "expected to find non-zero CBF\n");
            }
            else
                outTempCU->setQPSubParts(outTempCU->getRefQP(targetPartIdx), 0, depth); // set QP to default QP
        }

        m_rdEntropyCoders[nextDepth][CI_NEXT_BEST].store(m_rdEntropyCoders[depth][CI_TEMP_BEST]);
        checkBestMode(outBestCU, outTempCU, depth); // RD compare current CU against split
    }
    outBestCU->copyToPic(depth); // Copy Best data to Picture for next partition prediction.

    if (!bInsidePicture) return;

    // Copy Yuv data to picture Yuv
    copyYuv2Pic(pic, outBestCU->getAddr(), outBestCU->getZorderIdxInCU(), depth);

    X265_CHECK(outBestCU->getPartitionSize(0) != SIZE_NONE, "no best partition size\n");
    X265_CHECK(outBestCU->getPredictionMode(0) != MODE_NONE, "no best partition mode\n");
    if (m_rdCost.psyRdEnabled())
    {
        X265_CHECK(outBestCU->m_totalPsyCost != MAX_INT64, "no best partition cost\n");
    }
    else
    {
        X265_CHECK(outBestCU->m_totalRDCost != MAX_INT64, "no best partition cost\n");
    }
}

void Analysis::checkMerge2Nx2N_rd0_4(TComDataCU*& outBestCU, TComDataCU*& outTempCU, TComYuv*& bestPredYuv, TComYuv*& yuvReconBest)
{
    X265_CHECK(outTempCU->m_slice->m_sliceType != I_SLICE, "Evaluating merge in I slice\n");
    TComMvField mvFieldNeighbours[MRG_MAX_NUM_CANDS][2]; // double length for mv of both lists
    uint8_t interDirNeighbours[MRG_MAX_NUM_CANDS];
    uint32_t maxNumMergeCand = outTempCU->m_slice->m_maxNumMergeCand;

    uint8_t depth = outTempCU->getDepth(0);
    outTempCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to LCU level
    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    outTempCU->getInterMergeCandidates(0, 0, mvFieldNeighbours, interDirNeighbours, maxNumMergeCand);
    outTempCU->setPredModeSubParts(MODE_INTER, 0, depth);
    outTempCU->setMergeFlag(0, true);

    outBestCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to LCU level
    outBestCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    outBestCU->setPredModeSubParts(MODE_INTER, 0, depth);
    outBestCU->setMergeFlag(0, true);

    int sizeIdx = outTempCU->getLog2CUSize(0) - 2;
    int bestMergeCand = -1;

    for (uint32_t mergeCand = 0; mergeCand < maxNumMergeCand; ++mergeCand)
    {
        if (!m_bFrameParallel ||
            (mvFieldNeighbours[mergeCand][0].mv.y < (m_param->searchRange + 1) * 4 &&
             mvFieldNeighbours[mergeCand][1].mv.y < (m_param->searchRange + 1) * 4))
        {
            // set MC parameters, interprets depth relative to LCU level
            outTempCU->setMergeIndex(0, mergeCand);
            outTempCU->setInterDirSubParts(interDirNeighbours[mergeCand], 0, 0, depth);
            outTempCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(mvFieldNeighbours[mergeCand][0], SIZE_2Nx2N, 0, 0); // interprets depth relative to rpcTempCU level
            outTempCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(mvFieldNeighbours[mergeCand][1], SIZE_2Nx2N, 0, 0); // interprets depth relative to rpcTempCU level

            // do MC only for Luma part
            /* Set CU parameters for motion compensation */
            outTempCU->getPartIndexAndSize(0, m_partAddr, m_width, m_height);
            motionCompensation(outTempCU, m_tmpPredYuv[depth], REF_PIC_LIST_X, true, false);
            uint32_t bitsCand = getTUBits(mergeCand, maxNumMergeCand);
            outTempCU->m_totalBits = bitsCand;
            outTempCU->m_totalDistortion = primitives.sa8d[sizeIdx](m_origYuv[depth]->getLumaAddr(), m_origYuv[depth]->getStride(),
                                                                    m_tmpPredYuv[depth]->getLumaAddr(), m_tmpPredYuv[depth]->getStride());
            outTempCU->m_sa8dCost = m_rdCost.calcRdSADCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);

            if (outTempCU->m_sa8dCost < outBestCU->m_sa8dCost)
            {
                bestMergeCand = mergeCand;
                std::swap(outBestCU, outTempCU);
                // Change Prediction data
                std::swap(bestPredYuv, m_tmpPredYuv[depth]);
            }
        }
    }

    if (bestMergeCand < 0)
    {
        outBestCU->setMergeFlag(0, false);
        outBestCU->setQPSubParts(outBestCU->getQP(0), 0, depth);
    }
    else
    {
        outTempCU->setMergeIndex(0, bestMergeCand);
        outTempCU->setInterDirSubParts(interDirNeighbours[bestMergeCand], 0, 0, depth);
        outTempCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(mvFieldNeighbours[bestMergeCand][0], SIZE_2Nx2N, 0, 0);
        outTempCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(mvFieldNeighbours[bestMergeCand][1], SIZE_2Nx2N, 0, 0);
        outTempCU->m_totalBits = outBestCU->m_totalBits;
        outTempCU->m_totalDistortion = outBestCU->m_totalDistortion;
        outTempCU->m_sa8dCost = outBestCU->m_sa8dCost;

        if (m_param->rdLevel >= 1)
        {
            //calculate the motion compensation for chroma for the best mode selected
            int numPart = outBestCU->getNumPartInter();
            for (int partIdx = 0; partIdx < numPart; partIdx++)
            {
                outBestCU->getPartIndexAndSize(partIdx, m_partAddr, m_width, m_height);
                motionCompensation(outBestCU, bestPredYuv, REF_PIC_LIST_X, false, true);
            }

            if (outTempCU->isLosslessCoded(0))
                outBestCU->m_totalRDCost = MAX_INT64;
            else
            {
                //No-residue mode
                encodeResAndCalcRdInterCU(outBestCU, m_origYuv[depth], bestPredYuv, m_tmpResiYuv[depth], m_bestResiYuv[depth], m_tmpRecoYuv[depth], true, true);
                std::swap(yuvReconBest, m_tmpRecoYuv[depth]);
                m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
            }

            //Encode with residue
            encodeResAndCalcRdInterCU(outTempCU, m_origYuv[depth], bestPredYuv, m_tmpResiYuv[depth], m_bestResiYuv[depth], m_tmpRecoYuv[depth], false, true);

            uint64_t tempCost = m_rdCost.psyRdEnabled() ? outTempCU->m_totalPsyCost : outTempCU->m_totalRDCost;
            uint64_t bestCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;
            if (tempCost < bestCost) //Choose best from no-residue mode and residue mode
            {
                std::swap(outBestCU, outTempCU);
                std::swap(yuvReconBest, m_tmpRecoYuv[depth]);
                m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
            }
        }
    }
}

void Analysis::checkMerge2Nx2N_rd5_6(TComDataCU*& outBestCU, TComDataCU*& outTempCU, bool *earlyDetectionSkipMode, TComYuv*& outBestPredYuv, TComYuv*& rpcYuvReconBest)
{
    X265_CHECK(outTempCU->m_slice->m_sliceType != I_SLICE, "I slice not expected\n");
    TComMvField mvFieldNeighbours[MRG_MAX_NUM_CANDS][2]; // double length for mv of both lists
    uint8_t interDirNeighbours[MRG_MAX_NUM_CANDS];
    uint32_t maxNumMergeCand = outTempCU->m_slice->m_maxNumMergeCand;

    uint8_t depth = outTempCU->getDepth(0);
    outTempCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to LCU level
    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    outTempCU->getInterMergeCandidates(0, 0, mvFieldNeighbours, interDirNeighbours, maxNumMergeCand);

    int mergeCandBuffer[MRG_MAX_NUM_CANDS];
    for (uint32_t i = 0; i < maxNumMergeCand; ++i)
        mergeCandBuffer[i] = 0;

    bool bestIsSkip = false;

    uint32_t iteration = outTempCU->isLosslessCoded(0) ? 1 : 2;

    for (uint32_t noResidual = 0; noResidual < iteration; ++noResidual)
    {
        for (uint32_t mergeCand = 0; mergeCand < maxNumMergeCand; ++mergeCand)
        {
            if (m_bFrameParallel &&
                (mvFieldNeighbours[mergeCand][0].mv.y >= (m_param->searchRange + 1) * 4 ||
                 mvFieldNeighbours[mergeCand][1].mv.y >= (m_param->searchRange + 1) * 4))
            {
                continue;
            }
            if (!(noResidual && mergeCandBuffer[mergeCand] == 1))
            {
                if (!(bestIsSkip && !noResidual))
                {
                    // set MC parameters
                    outTempCU->setPredModeSubParts(MODE_INTER, 0, depth); // interprets depth relative to LCU level
                    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
                    outTempCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to LCU level
                    outTempCU->setMergeFlag(0, true);
                    outTempCU->setMergeIndex(0, mergeCand);
                    outTempCU->setInterDirSubParts(interDirNeighbours[mergeCand], 0, 0, depth); // interprets depth relative to LCU level
                    outTempCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(mvFieldNeighbours[mergeCand][0], SIZE_2Nx2N, 0, 0); // interprets depth relative to outTempCU level
                    outTempCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(mvFieldNeighbours[mergeCand][1], SIZE_2Nx2N, 0, 0); // interprets depth relative to outTempCU level

                    // do MC
                    outTempCU->getPartIndexAndSize(0, m_partAddr, m_width, m_height);
                    motionCompensation(outTempCU, m_tmpPredYuv[depth], REF_PIC_LIST_X);
                    // estimate residual and encode everything
                    encodeResAndCalcRdInterCU(outTempCU,
                                              m_origYuv[depth],
                                              m_tmpPredYuv[depth],
                                              m_tmpResiYuv[depth],
                                              m_bestResiYuv[depth],
                                              m_tmpRecoYuv[depth],
                                              !!noResidual,
                                              true);

                    /* Todo: Fix the satd cost estimates. Why is merge being chosen in high motion areas: estimated distortion is too low? */
                    if (!noResidual && !outTempCU->getQtRootCbf(0))
                        mergeCandBuffer[mergeCand] = 1;

                    outTempCU->setSkipFlagSubParts(!outTempCU->getQtRootCbf(0), 0, depth);
                    int origQP = outTempCU->getQP(0);
                    checkDQP(outTempCU);
                    uint64_t tempCost = m_rdCost.psyRdEnabled() ? outTempCU->m_totalPsyCost : outTempCU->m_totalRDCost;
                    uint64_t bestCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;
                    if (tempCost < bestCost)
                    {
                        std::swap(outBestCU, outTempCU);
                        std::swap(outBestPredYuv, m_tmpPredYuv[depth]);
                        std::swap(rpcYuvReconBest, m_tmpRecoYuv[depth]);

                        m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
                    }
                    outTempCU->setQPSubParts(origQP, 0, depth);
                    outTempCU->setSkipFlagSubParts(false, 0, depth);
                    if (!bestIsSkip)
                        bestIsSkip = !outBestCU->getQtRootCbf(0);
                }
            }
        }

        if (noResidual == 0 && m_param->bEnableEarlySkip)
        {
            if (outBestCU->getQtRootCbf(0) == 0)
            {
                if (outBestCU->getMergeFlag(0))
                    *earlyDetectionSkipMode = true;
                else
                {
                    int mvsum = 0;
                    for (uint32_t refListIdx = 0; refListIdx < 2; refListIdx++)
                    {
                        if (outBestCU->m_slice->m_numRefIdx[refListIdx] > 0)
                        {
                            TComCUMvField* pcCUMvField = outBestCU->getCUMvField(refListIdx);
                            int hor = abs(pcCUMvField->getMvd(0).x);
                            int ver = abs(pcCUMvField->getMvd(0).y);
                            mvsum += hor + ver;
                        }
                    }

                    if (mvsum == 0)
                        *earlyDetectionSkipMode = true;
                }
            }
        }
    }
}

void Analysis::checkInter_rd0_4(TComDataCU* outTempCU, TComYuv* outPredYuv, PartSize partSize, bool bUseMRG)
{
    uint8_t depth = outTempCU->getDepth(0);

    outTempCU->setPartSizeSubParts(partSize, 0, depth);
    outTempCU->setPredModeSubParts(MODE_INTER, 0, depth);
    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    // do motion compensation only for Luma since luma cost alone is calculated
    outTempCU->m_totalBits = 0;
    if (predInterSearch(outTempCU, outPredYuv, bUseMRG, false))
    {
        int sizeIdx = outTempCU->getLog2CUSize(0) - 2;
        uint32_t distortion = primitives.sa8d[sizeIdx](m_origYuv[depth]->getLumaAddr(), m_origYuv[depth]->getStride(),
                                                       outPredYuv->getLumaAddr(), outPredYuv->getStride());
        outTempCU->m_totalDistortion = distortion;
        outTempCU->m_sa8dCost = m_rdCost.calcRdSADCost(distortion, outTempCU->m_totalBits);
    }
    else
    {
        outTempCU->m_totalDistortion = MAX_UINT;
        outTempCU->m_totalRDCost = MAX_INT64;
    }
}

void Analysis::checkInter_rd5_6(TComDataCU*& outBestCU, TComDataCU*& outTempCU, PartSize partSize, bool bUseMRG)
{
    uint8_t depth = outTempCU->getDepth(0);

    outTempCU->setSkipFlagSubParts(false, 0, depth);
    outTempCU->setPartSizeSubParts(partSize, 0, depth);
    outTempCU->setPredModeSubParts(MODE_INTER, 0, depth);
    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    if (predInterSearch(outTempCU, m_tmpPredYuv[depth], bUseMRG, true))
    {
        encodeResAndCalcRdInterCU(outTempCU, m_origYuv[depth], m_tmpPredYuv[depth], m_tmpResiYuv[depth], m_bestResiYuv[depth], m_tmpRecoYuv[depth], false, true);
        checkDQP(outTempCU);
        checkBestMode(outBestCU, outTempCU, depth);
    }
}

void Analysis::checkIntraInInter_rd0_4(TComDataCU* cu, PartSize partSize)
{
    uint32_t depth = cu->getDepth(0);

    cu->setPartSizeSubParts(partSize, 0, depth);
    cu->setPredModeSubParts(MODE_INTRA, 0, depth);
    cu->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    uint32_t initTrDepth = cu->getPartitionSize(0) == SIZE_2Nx2N ? 0 : 1;
    uint32_t log2TrSize  = cu->getLog2CUSize(0) - initTrDepth;
    uint32_t tuSize      = 1 << log2TrSize;
    const uint32_t partOffset  = 0;

    // Reference sample smoothing
    TComPattern::initAdiPattern(cu, partOffset, initTrDepth, m_predBuf, m_refAbove, m_refLeft, m_refAboveFlt, m_refLeftFlt, ALL_IDX);

    pixel* fenc     = m_origYuv[depth]->getLumaAddr();
    uint32_t stride = m_modePredYuv[5][depth]->getStride();

    pixel *above         = m_refAbove    + tuSize - 1;
    pixel *aboveFiltered = m_refAboveFlt + tuSize - 1;
    pixel *left          = m_refLeft     + tuSize - 1;
    pixel *leftFiltered  = m_refLeftFlt  + tuSize - 1;
    int sad, bsad;
    uint32_t bits, bbits, mode, bmode;
    uint64_t cost, bcost;

    // 33 Angle modes once
    ALIGN_VAR_32(pixel, buf_trans[32 * 32]);
    ALIGN_VAR_32(pixel, tmp[33 * 32 * 32]);
    int scaleTuSize = tuSize;
    int scaleStride = stride;
    int costShift = 0;
    int sizeIdx = log2TrSize - 2;

    if (tuSize > 32)
    {
        // origin is 64x64, we scale to 32x32 and setup required parameters
        ALIGN_VAR_32(pixel, bufScale[32 * 32]);
        primitives.scale2D_64to32(bufScale, fenc, stride);
        fenc = bufScale;

        // reserve space in case primitives need to store data in above
        // or left buffers
        pixel _above[4 * 32 + 1];
        pixel _left[4 * 32 + 1];
        pixel *aboveScale  = _above + 2 * 32;
        pixel *leftScale   = _left + 2 * 32;
        aboveScale[0] = leftScale[0] = above[0];
        primitives.scale1D_128to64(aboveScale + 1, above + 1, 0);
        primitives.scale1D_128to64(leftScale + 1, left + 1, 0);

        scaleTuSize = 32;
        scaleStride = 32;
        costShift = 2;
        sizeIdx = 5 - 2; // log2(scaleTuSize) - 2

        // Filtered and Unfiltered refAbove and refLeft pointing to above and left.
        above         = aboveScale;
        left          = leftScale;
        aboveFiltered = aboveScale;
        leftFiltered  = leftScale;
    }

    pixelcmp_t sa8d = primitives.sa8d[sizeIdx];

    uint32_t preds[3];
    cu->getIntraDirLumaPredictor(partOffset, preds);

    uint64_t mpms;
    uint32_t rbits = xModeBitsRemIntra(cu, partOffset, depth, preds, mpms);

    // DC
    primitives.intra_pred[sizeIdx][DC_IDX](tmp, scaleStride, left, above, 0, (scaleTuSize <= 16));
    bsad = sa8d(fenc, scaleStride, tmp, scaleStride) << costShift;
    bmode = mode = DC_IDX;
    bbits = !(mpms & ((uint64_t)1 << mode)) ? rbits : xModeBitsIntra(cu, mode, partOffset, depth);
    bcost = m_rdCost.calcRdSADCost(bsad, bbits);

    pixel *abovePlanar   = above;
    pixel *leftPlanar    = left;

    if (tuSize >= 8 && tuSize <= 32)
    {
        abovePlanar = aboveFiltered;
        leftPlanar  = leftFiltered;
    }

    // PLANAR
    primitives.intra_pred[sizeIdx][PLANAR_IDX](tmp, scaleStride, leftPlanar, abovePlanar, 0, 0);
    sad = sa8d(fenc, scaleStride, tmp, scaleStride) << costShift;
    mode = PLANAR_IDX;
    bits = !(mpms & ((uint64_t)1 << mode)) ? rbits : xModeBitsIntra(cu, mode, partOffset, depth);
    cost = m_rdCost.calcRdSADCost(sad, bits);
    COPY4_IF_LT(bcost, cost, bmode, mode, bsad, sad, bbits, bits);

    // Transpose NxN
    primitives.transpose[sizeIdx](buf_trans, fenc, scaleStride);

    primitives.intra_pred_allangs[sizeIdx](tmp, above, left, aboveFiltered, leftFiltered, (scaleTuSize <= 16));

    for (mode = 2; mode < 35; mode++)
    {
        bool modeHor = (mode < 18);
        pixel *cmp = (modeHor ? buf_trans : fenc);
        intptr_t srcStride = (modeHor ? scaleTuSize : scaleStride);
        sad  = sa8d(cmp, srcStride, &tmp[(mode - 2) * (scaleTuSize * scaleTuSize)], scaleTuSize) << costShift;
        bits = !(mpms & ((uint64_t)1 << mode)) ? rbits : xModeBitsIntra(cu, mode, partOffset, depth);
        cost = m_rdCost.calcRdSADCost(sad, bits);
        COPY4_IF_LT(bcost, cost, bmode, mode, bsad, sad, bbits, bits);
    }

    cu->m_totalBits = bbits;
    cu->m_totalDistortion = bsad;
    cu->m_sa8dCost = bcost;

    // generate predYuv for the best mode
    cu->setLumaIntraDirSubParts(bmode, partOffset, depth + initTrDepth);
}

void Analysis::checkIntraInInter_rd5_6(TComDataCU*& outBestCU, TComDataCU*& outTempCU, PartSize partSize)
{
    uint32_t depth = outTempCU->getDepth(0);

    PPAScopeEvent(CheckRDCostIntra + depth);

    m_trQuant.setQPforQuant(outTempCU);
    outTempCU->setSkipFlagSubParts(false, 0, depth);
    outTempCU->setPartSizeSubParts(partSize, 0, depth);
    outTempCU->setPredModeSubParts(MODE_INTRA, 0, depth);
    outTempCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    estIntraPredQT(outTempCU, m_origYuv[depth], m_tmpPredYuv[depth], m_tmpResiYuv[depth], m_tmpRecoYuv[depth]);

    estIntraPredChromaQT(outTempCU, m_origYuv[depth], m_tmpPredYuv[depth], m_tmpResiYuv[depth], m_tmpRecoYuv[depth]);

    m_entropyCoder->resetBits();
    if (outTempCU->m_slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder->codeCUTransquantBypassFlag(outTempCU, 0);

    if (!outTempCU->m_slice->isIntra())
    {
        m_entropyCoder->codeSkipFlag(outTempCU, 0);
        m_entropyCoder->codePredMode(outTempCU, 0);
    }
    m_entropyCoder->codePartSize(outTempCU, 0, depth);
    m_entropyCoder->codePredInfo(outTempCU, 0);
    outTempCU->m_mvBits = m_entropyCoder->getNumberOfWrittenBits();

    // Encode Coefficients
    bool bCodeDQP = m_bEncodeDQP;
    m_entropyCoder->codeCoeff(outTempCU, 0, depth, bCodeDQP);
    m_entropyCoder->store(m_rdEntropyCoders[depth][CI_TEMP_BEST]);
    outTempCU->m_totalBits = m_entropyCoder->getNumberOfWrittenBits();
    outTempCU->m_coeffBits = outTempCU->m_totalBits - outTempCU->m_mvBits;

    if (m_rdCost.psyRdEnabled())
    {
        int part = outTempCU->getLog2CUSize(0) - 2;
        outTempCU->m_psyEnergy = m_rdCost.psyCost(part, m_origYuv[depth]->getLumaAddr(), m_origYuv[depth]->getStride(),
                                                  m_tmpRecoYuv[depth]->getLumaAddr(), m_tmpRecoYuv[depth]->getStride());
        outTempCU->m_totalPsyCost = m_rdCost.calcPsyRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits, outTempCU->m_psyEnergy);
    }
    else
        outTempCU->m_totalRDCost = m_rdCost.calcRdCost(outTempCU->m_totalDistortion, outTempCU->m_totalBits);

    checkDQP(outTempCU);
    checkBestMode(outBestCU, outTempCU, depth);
}

void Analysis::encodeIntraInInter(TComDataCU* cu, TComYuv* fencYuv, TComYuv* predYuv,  ShortYuv* outResiYuv, TComYuv* outReconYuv)
{
    uint64_t puCost = 0;
    uint32_t puDistY = 0;
    uint32_t depth = cu->getDepth(0);
    uint32_t initTrDepth = cu->getPartitionSize(0) == SIZE_2Nx2N ? 0 : 1;

    // set context models
    m_entropyCoder->load(m_rdEntropyCoders[depth][CI_CURR_BEST]);

    m_trQuant.setQPforQuant(cu);

    xRecurIntraCodingQT(cu, initTrDepth, 0, fencYuv, predYuv, outResiYuv, puDistY, false, puCost);
    xSetIntraResultQT(cu, initTrDepth, 0, outReconYuv);

    //=== update PU data ====
    cu->copyToPic(cu->getDepth(0), 0, initTrDepth);

    //===== set distortion (rate and r-d costs are determined later) =====
    cu->m_totalDistortion = puDistY;

    estIntraPredChromaQT(cu, fencYuv, predYuv, outResiYuv, outReconYuv);
    m_entropyCoder->resetBits();
    if (cu->m_slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder->codeCUTransquantBypassFlag(cu, 0);

    if (!cu->m_slice->isIntra())
    {
        m_entropyCoder->codeSkipFlag(cu, 0);
        m_entropyCoder->codePredMode(cu, 0);
    }
    m_entropyCoder->codePartSize(cu, 0, depth);
    m_entropyCoder->codePredInfo(cu, 0);
    cu->m_mvBits += m_entropyCoder->getNumberOfWrittenBits();

    // Encode Coefficients
    bool bCodeDQP = m_bEncodeDQP;
    m_entropyCoder->codeCoeff(cu, 0, depth, bCodeDQP);
    m_entropyCoder->store(m_rdEntropyCoders[depth][CI_TEMP_BEST]);

    cu->m_totalBits = m_entropyCoder->getNumberOfWrittenBits();
    cu->m_coeffBits = cu->m_totalBits - cu->m_mvBits;
    if (m_rdCost.psyRdEnabled())
    {
        int part = cu->getLog2CUSize(0) - 2;
        cu->m_psyEnergy = m_rdCost.psyCost(part, m_origYuv[depth]->getLumaAddr(), m_origYuv[depth]->getStride(),
            m_tmpRecoYuv[depth]->getLumaAddr(), m_tmpRecoYuv[depth]->getStride());
        cu->m_totalPsyCost = m_rdCost.calcPsyRdCost(cu->m_totalDistortion, cu->m_totalBits, cu->m_psyEnergy);
    }
    else
        cu->m_totalRDCost = m_rdCost.calcRdCost(cu->m_totalDistortion, cu->m_totalBits);
}

void Analysis::encodeResidue(TComDataCU* lcu, TComDataCU* cu, uint32_t absPartIdx, uint8_t depth)
{
    uint8_t nextDepth = (uint8_t)(depth + 1);
    TComDataCU* subTempPartCU = m_tempCU[nextDepth];
    Frame* pic = cu->m_pic;
    Slice* slice = cu->m_slice;

    if (((depth < lcu->getDepth(absPartIdx)) && (depth < (g_maxCUDepth - g_addCUDepth))))
    {
        uint32_t qNumParts = (pic->getNumPartInCU() >> (depth << 1)) >> 2;
        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++, absPartIdx += qNumParts)
        {
            uint32_t lpelx = lcu->getCUPelX() + g_rasterToPelX[g_zscanToRaster[absPartIdx]];
            uint32_t tpely = lcu->getCUPelY() + g_rasterToPelY[g_zscanToRaster[absPartIdx]];
            if ((lpelx < slice->m_sps->picWidthInLumaSamples) &&
                (tpely < slice->m_sps->picHeightInLumaSamples))
            {
                subTempPartCU->copyToSubCU(cu, partUnitIdx, depth + 1);
                encodeResidue(lcu, subTempPartCU, absPartIdx, depth + 1);
            }
        }

        return;
    }

    m_trQuant.setQPforQuant(cu);

    if (lcu->getPredictionMode(absPartIdx) == MODE_INTER)
    {
        if (!lcu->getSkipFlag(absPartIdx))
        {
            // Calculate Residue
            pixel* src2 = m_bestPredYuv[0]->getLumaAddr(absPartIdx);
            pixel* src1 = m_origYuv[0]->getLumaAddr(absPartIdx);
            int16_t* dst = m_tmpResiYuv[depth]->getLumaAddr();
            uint32_t src2stride = m_bestPredYuv[0]->getStride();
            uint32_t src1stride = m_origYuv[0]->getStride();
            uint32_t dststride = m_tmpResiYuv[depth]->m_width;
            int part = partitionFromLog2Size(cu->getLog2CUSize(0));
            primitives.luma_sub_ps[part](dst, dststride, src1, src2, src1stride, src2stride);

            src2 = m_bestPredYuv[0]->getCbAddr(absPartIdx);
            src1 = m_origYuv[0]->getCbAddr(absPartIdx);
            dst = m_tmpResiYuv[depth]->getCbAddr();
            src2stride = m_bestPredYuv[0]->getCStride();
            src1stride = m_origYuv[0]->getCStride();
            dststride = m_tmpResiYuv[depth]->m_cwidth;
            primitives.chroma[m_param->internalCsp].sub_ps[part](dst, dststride, src1, src2, src1stride, src2stride);

            src2 = m_bestPredYuv[0]->getCrAddr(absPartIdx);
            src1 = m_origYuv[0]->getCrAddr(absPartIdx);
            dst = m_tmpResiYuv[depth]->getCrAddr();
            dststride = m_tmpResiYuv[depth]->m_cwidth;
            primitives.chroma[m_param->internalCsp].sub_ps[part](dst, dststride, src1, src2, src1stride, src2stride);

            // Residual encoding
            residualTransformQuantInter(cu, 0, m_tmpResiYuv[depth], cu->getDepth(0), true);
            checkDQP(cu);

            if (lcu->getMergeFlag(absPartIdx) && cu->getPartitionSize(0) == SIZE_2Nx2N && !cu->getQtRootCbf(0))
            {
                cu->setSkipFlagSubParts(true, 0, depth);
                cu->copyCodedToPic(depth);
            }
            else
            {
                cu->copyCodedToPic(depth);

                // Generate Recon
                pixel* pred = m_bestPredYuv[0]->getLumaAddr(absPartIdx);
                int16_t* res = m_tmpResiYuv[depth]->getLumaAddr();
                pixel* reco = m_bestRecoYuv[depth]->getLumaAddr();
                dststride = m_bestRecoYuv[depth]->getStride();
                src1stride = m_bestPredYuv[0]->getStride();
                src2stride = m_tmpResiYuv[depth]->m_width;
                primitives.luma_add_ps[part](reco, dststride, pred, res, src1stride, src2stride);

                pred = m_bestPredYuv[0]->getCbAddr(absPartIdx);
                res = m_tmpResiYuv[depth]->getCbAddr();
                reco = m_bestRecoYuv[depth]->getCbAddr();
                dststride = m_bestRecoYuv[depth]->getCStride();
                src1stride = m_bestPredYuv[0]->getCStride();
                src2stride = m_tmpResiYuv[depth]->m_cwidth;
                primitives.chroma[m_param->internalCsp].add_ps[part](reco, dststride, pred, res, src1stride, src2stride);

                pred = m_bestPredYuv[0]->getCrAddr(absPartIdx);
                res = m_tmpResiYuv[depth]->getCrAddr();
                reco = m_bestRecoYuv[depth]->getCrAddr();
                reco = m_bestRecoYuv[depth]->getCrAddr();
                primitives.chroma[m_param->internalCsp].add_ps[part](reco, dststride, pred, res, src1stride, src2stride);
                m_bestRecoYuv[depth]->copyToPicYuv(pic->getPicYuvRec(), lcu->getAddr(), absPartIdx);
                return;
            }
        }

        // Generate Recon
        TComPicYuv* rec = pic->getPicYuvRec();
        int part = partitionFromLog2Size(cu->getLog2CUSize(0));
        pixel* src = m_bestPredYuv[0]->getLumaAddr(absPartIdx);
        pixel* dst = rec->getLumaAddr(cu->getAddr(), absPartIdx);
        uint32_t srcstride = m_bestPredYuv[0]->getStride();
        uint32_t dststride = rec->getStride();
        primitives.luma_copy_pp[part](dst, dststride, src, srcstride);

        src = m_bestPredYuv[0]->getCbAddr(absPartIdx);
        dst = rec->getCbAddr(cu->getAddr(), absPartIdx);
        srcstride = m_bestPredYuv[0]->getCStride();
        dststride = rec->getCStride();
        primitives.chroma[m_param->internalCsp].copy_pp[part](dst, dststride, src, srcstride);

        src = m_bestPredYuv[0]->getCrAddr(absPartIdx);
        dst = rec->getCrAddr(cu->getAddr(), absPartIdx);
        primitives.chroma[m_param->internalCsp].copy_pp[part](dst, dststride, src, srcstride);
    }
    else
    {
        m_origYuv[0]->copyPartToYuv(m_origYuv[depth], absPartIdx);
        generateCoeffRecon(cu, m_origYuv[depth], m_modePredYuv[5][depth], m_tmpResiYuv[depth],  m_tmpRecoYuv[depth], false);
        checkDQP(cu);
        m_tmpRecoYuv[depth]->copyToPicYuv(pic->getPicYuvRec(), lcu->getAddr(), absPartIdx);
        cu->copyCodedToPic(depth);
    }
}

/* encode a CU block recursively */
void Analysis::encodeCU(TComDataCU* cu, uint32_t absPartIdx, uint32_t depth, bool bInsidePicture)
{
    Frame* pic = cu->m_pic;

    Slice* slice = cu->m_slice;
    if (!bInsidePicture)
    {
        uint32_t lpelx = cu->getCUPelX() + g_rasterToPelX[g_zscanToRaster[absPartIdx]];
        uint32_t tpely = cu->getCUPelY() + g_rasterToPelY[g_zscanToRaster[absPartIdx]];
        uint32_t rpelx = lpelx + (g_maxCUSize >> depth);
        uint32_t bpely = tpely + (g_maxCUSize >> depth);
        bInsidePicture = (rpelx <= slice->m_sps->picWidthInLumaSamples &&
                          bpely <= slice->m_sps->picHeightInLumaSamples);
    }

    // We need to split, so don't try these modes.
    if (bInsidePicture)
        m_entropyCoder->codeSplitFlag(cu, absPartIdx, depth);

    if ((g_maxCUSize >> depth) >= slice->m_pps->minCuDQPSize && slice->m_pps->bUseDQP)
        m_bEncodeDQP = true;

    if (!bInsidePicture)
    {
        uint32_t qNumParts = (pic->getNumPartInCU() >> (depth << 1)) >> 2;

        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++, absPartIdx += qNumParts)
        {
            uint32_t lpelx = cu->getCUPelX() + g_rasterToPelX[g_zscanToRaster[absPartIdx]];
            uint32_t tpely = cu->getCUPelY() + g_rasterToPelY[g_zscanToRaster[absPartIdx]];
            if ((lpelx < slice->m_sps->picWidthInLumaSamples) &&
                (tpely < slice->m_sps->picHeightInLumaSamples))
            {
                encodeCU(cu, absPartIdx, depth + 1, bInsidePicture);
            }
        }

        return;
    }

    if ((depth < cu->getDepth(absPartIdx)) && (depth < (g_maxCUDepth - g_addCUDepth)))
    {
        uint32_t qNumParts = (pic->getNumPartInCU() >> (depth << 1)) >> 2;

        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++, absPartIdx += qNumParts)
            encodeCU(cu, absPartIdx, depth + 1, bInsidePicture);
        return;
    }

    if (slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder->codeCUTransquantBypassFlag(cu, absPartIdx);

    if (!slice->isIntra())
        m_entropyCoder->codeSkipFlag(cu, absPartIdx);

    if (cu->isSkipped(absPartIdx))
    {
        m_entropyCoder->codeMergeIndex(cu, absPartIdx);
        finishCU(cu, absPartIdx, depth);
        return;
    }

    if (!slice->isIntra())
        m_entropyCoder->codePredMode(cu, absPartIdx);

    m_entropyCoder->codePartSize(cu, absPartIdx, depth);

    // prediction Info ( Intra : direction mode, Inter : Mv, reference idx )
    m_entropyCoder->codePredInfo(cu, absPartIdx);

    // Encode Coefficients, allow codeCoeff() to modify m_bEncodeDQP
    m_entropyCoder->codeCoeff(cu, absPartIdx, depth, m_bEncodeDQP);

    // --- write terminating bit ---
    finishCU(cu, absPartIdx, depth);
}

void Analysis::encodeCU(TComDataCU* cu)
{
    if (cu->m_slice->m_pps->bUseDQP)
        m_bEncodeDQP = true;

    // Encode CU data
    encodeCU(cu, 0, 0, false);
}

/* check whether current try is the best with identifying the depth of current try */
void Analysis::checkBestMode(TComDataCU*& outBestCU, TComDataCU*& outTempCU, uint32_t depth)
{
    uint64_t tempCost = m_rdCost.psyRdEnabled() ? outTempCU->m_totalPsyCost : outTempCU->m_totalRDCost;
    uint64_t bestCost = m_rdCost.psyRdEnabled() ? outBestCU->m_totalPsyCost : outBestCU->m_totalRDCost;

    if (tempCost < bestCost)
    {
        // Change Information data
        std::swap(outBestCU, outTempCU);

        // Change Prediction data
        std::swap(m_bestPredYuv[depth], m_tmpPredYuv[depth]);

        // Change Reconstruction data
        std::swap(m_bestRecoYuv[depth], m_tmpRecoYuv[depth]);

        m_rdEntropyCoders[depth][CI_TEMP_BEST].store(m_rdEntropyCoders[depth][CI_NEXT_BEST]);
    }
}

void Analysis::deriveTestModeAMP(TComDataCU* outBestCU, PartSize parentSize, bool &bTestAMP_Hor, bool &bTestAMP_Ver,
                               bool &bTestMergeAMP_Hor, bool &bTestMergeAMP_Ver)
{
    if (outBestCU->getPartitionSize(0) == SIZE_2NxN)
    {
        bTestAMP_Hor = true;
    }
    else if (outBestCU->getPartitionSize(0) == SIZE_Nx2N)
    {
        bTestAMP_Ver = true;
    }
    else if (outBestCU->getPartitionSize(0) == SIZE_2Nx2N && outBestCU->getMergeFlag(0) == false &&
             outBestCU->isSkipped(0) == false)
    {
        bTestAMP_Hor = true;
        bTestAMP_Ver = true;
    }

    //! Utilizing the partition size of parent PU
    if (parentSize >= SIZE_2NxnU && parentSize <= SIZE_nRx2N)
    {
        bTestMergeAMP_Hor = true;
        bTestMergeAMP_Ver = true;
    }

    if (parentSize == SIZE_NONE) //! if parent is intra
    {
        if (outBestCU->getPartitionSize(0) == SIZE_2NxN)
        {
            bTestMergeAMP_Hor = true;
        }
        else if (outBestCU->getPartitionSize(0) == SIZE_Nx2N)
        {
            bTestMergeAMP_Ver = true;
        }
    }

    if (outBestCU->getPartitionSize(0) == SIZE_2Nx2N && outBestCU->isSkipped(0) == false)
    {
        bTestMergeAMP_Hor = true;
        bTestMergeAMP_Ver = true;
    }

    if (outBestCU->getLog2CUSize(0) == 6)
    {
        bTestAMP_Hor = false;
        bTestAMP_Ver = false;
    }
}

void Analysis::checkDQP(TComDataCU* cu)
{
    uint32_t depth = cu->getDepth(0);

    if (cu->m_slice->m_pps->bUseDQP && (g_maxCUSize >> depth) >= cu->m_slice->m_pps->minCuDQPSize)
    {
        if (!cu->getCbf(0, TEXT_LUMA, 0) && !cu->getCbf(0, TEXT_CHROMA_U, 0) && !cu->getCbf(0, TEXT_CHROMA_V, 0))
            cu->setQPSubParts(cu->getRefQP(0), 0, depth); // set QP to default QP
    }
}

void Analysis::copyYuv2Pic(Frame* outPic, uint32_t cuAddr, uint32_t absPartIdx, uint32_t depth)
{
    m_bestRecoYuv[depth]->copyToPicYuv(outPic->getPicYuvRec(), cuAddr, absPartIdx);
}

void Analysis::copyYuv2Tmp(uint32_t partUnitIdx, uint32_t nextDepth)
{
    m_bestRecoYuv[nextDepth]->copyToPartYuv(m_tmpRecoYuv[nextDepth - 1], partUnitIdx);
}

/* Function for filling original YUV samples of a CU in lossless mode */
void Analysis::fillOrigYUVBuffer(TComDataCU* cu, TComYuv* fencYuv)
{
    uint32_t width  = 1 << cu->getLog2CUSize(0);
    uint32_t height = 1 << cu->getLog2CUSize(0);

    pixel* srcY = fencYuv->getLumaAddr();
    pixel* dstY = cu->getLumaOrigYuv();
    uint32_t srcStride = fencYuv->getStride();

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            dstY[x] = srcY[x];
        }

        dstY += width;
        srcY += srcStride;
    }

    pixel* srcCb = fencYuv->getChromaAddr(1);
    pixel* srcCr = fencYuv->getChromaAddr(2);

    pixel* dstCb = cu->getChromaOrigYuv(1);
    pixel* dstCr = cu->getChromaOrigYuv(2);

    uint32_t srcStrideC = fencYuv->getCStride();
    uint32_t widthC  = width  >> cu->getHorzChromaShift();
    uint32_t heightC = height >> cu->getVertChromaShift();

    for (uint32_t y = 0; y < heightC; y++)
    {
        for (uint32_t x = 0; x < widthC; x++)
        {
            dstCb[x] = srcCb[x];
            dstCr[x] = srcCr[x];
        }

        dstCb += widthC;
        dstCr += widthC;
        srcCb += srcStrideC;
        srcCr += srcStrideC;
    }
}

/* finish encoding a cu and handle end-of-slice conditions */
void Analysis::finishCU(TComDataCU* cu, uint32_t absPartIdx, uint32_t depth)
{
    Frame* pic = cu->m_pic;
    Slice* slice = cu->m_slice;

    // Calculate end address
    uint32_t cuAddr = cu->getSCUAddr() + absPartIdx;

    uint32_t internalAddress = (slice->m_endCUAddr - 1) % pic->getNumPartInCU();
    uint32_t externalAddress = (slice->m_endCUAddr - 1) / pic->getNumPartInCU();
    uint32_t posx = (externalAddress % pic->getFrameWidthInCU()) * g_maxCUSize + g_rasterToPelX[g_zscanToRaster[internalAddress]];
    uint32_t posy = (externalAddress / pic->getFrameWidthInCU()) * g_maxCUSize + g_rasterToPelY[g_zscanToRaster[internalAddress]];
    uint32_t width = slice->m_sps->picWidthInLumaSamples;
    uint32_t height = slice->m_sps->picHeightInLumaSamples;
    uint32_t cuSize = 1 << cu->getLog2CUSize(absPartIdx);

    while (posx >= width || posy >= height)
    {
        internalAddress--;
        posx = (externalAddress % pic->getFrameWidthInCU()) * g_maxCUSize + g_rasterToPelX[g_zscanToRaster[internalAddress]];
        posy = (externalAddress / pic->getFrameWidthInCU()) * g_maxCUSize + g_rasterToPelY[g_zscanToRaster[internalAddress]];
    }

    internalAddress++;
    if (internalAddress == cu->m_pic->getNumPartInCU())
    {
        internalAddress = 0;
        externalAddress = (externalAddress + 1);
    }
    uint32_t realEndAddress = (externalAddress * pic->getNumPartInCU() + internalAddress);

    // Encode slice finish
    bool bTerminateSlice = false;
    if (cuAddr + (cu->m_pic->getNumPartInCU() >> (depth << 1)) == realEndAddress)
        bTerminateSlice = true;

    uint32_t granularityWidth = g_maxCUSize;
    posx = cu->getCUPelX() + g_rasterToPelX[g_zscanToRaster[absPartIdx]];
    posy = cu->getCUPelY() + g_rasterToPelY[g_zscanToRaster[absPartIdx]];
    bool granularityBoundary = ((posx + cuSize) % granularityWidth == 0 || (posx + cuSize == width))
                            && ((posy + cuSize) % granularityWidth == 0 || (posy + cuSize == height));

    if (granularityBoundary)
    {
        // The 1-terminating bit is added to all streams, so don't add it here when it's 1.
        if (!bTerminateSlice)
            m_entropyCoder->codeTerminatingBit(bTerminateSlice ? 1 : 0);

        if (m_entropyCoder->isBitCounter())
            m_entropyCoder->resetBits();
    }
}
