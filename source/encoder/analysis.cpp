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

#include "common.h"
#include "primitives.h"
#include "threading.h"

#include "analysis.h"
#include "rdcost.h"
#include "encoder.h"

#include "PPA/ppa.h"

using namespace x265;

Analysis::Analysis() : JobProvider(NULL)
{
    m_bJobsQueued = false;
    m_totalNumME = m_numAcquiredME = m_numCompletedME = 0;
    m_totalNumJobs = m_numAcquiredJobs = m_numCompletedJobs = 0;
}

bool Analysis::create(uint32_t numCUDepth, uint32_t maxWidth, ThreadLocalData *tld)
{
    X265_CHECK(numCUDepth <= NUM_CU_DEPTH, "invalid numCUDepth\n");

    m_tld = tld;
    m_bEncodeDQP = false;

    int csp       = m_param->internalCsp;
    bool tqBypass = m_param->bCULossless || m_param->bLossless;
    bool ok = true;
    for (uint32_t i = 0; i < numCUDepth; i++)
    {
        ModeDepth &md = m_modeDepth[i];

        uint32_t numPartitions = 1 << (g_maxFullDepth - i) * 2;
        uint32_t cuSize = maxWidth >> i;

        uint32_t sizeL = cuSize * cuSize;
        uint32_t sizeC = sizeL >> (CHROMA_H_SHIFT(csp) + CHROMA_V_SHIFT(csp));

        ok &= md.memPool.initialize(numPartitions, sizeL, sizeC, MAX_PRED_TYPES, tqBypass);
        ok &= md.origYuv.create(cuSize, cuSize, csp);
        ok &= md.tempResi.create(cuSize, cuSize, csp);

        for (int j = 0; j < MAX_PRED_TYPES; j++)
        {
            md.pred[j].cu.create(&md.memPool, numPartitions, cuSize, csp, j, tqBypass);
            ok &= md.pred[j].predYuv.create(cuSize, cuSize, csp);
            ok &= md.pred[j].reconYuv.create(cuSize, cuSize, csp);
            ok &= md.pred[j].resiYuv.create(cuSize, cuSize, csp);
        }
    }

    return ok;
}

void Analysis::destroy()
{
    uint32_t numCUDepth = g_maxCUDepth + 1;
    for (uint32_t i = 0; i < numCUDepth; i++)
    {
        m_modeDepth[i].memPool.destroy();
        m_modeDepth[i].origYuv.destroy();
        m_modeDepth[i].tempResi.destroy();

        for (int j = 0; j < MAX_PRED_TYPES; j++)
        {
            m_modeDepth[i].pred[j].cu.destroy();
            m_modeDepth[i].pred[j].predYuv.destroy();
            m_modeDepth[i].pred[j].reconYuv.destroy();
            m_modeDepth[i].pred[j].resiYuv.destroy();
        }
    }
}

bool Analysis::findJob(int threadId)
{
    /* try to acquire a CU mode to analyze */
    if (m_totalNumJobs > m_numAcquiredJobs)
    {
        /* ATOMIC_INC returns the incremented value */
        int id = ATOMIC_INC(&m_numAcquiredJobs);
        if (m_totalNumJobs >= id)
        {
            parallelAnalysisJob(threadId, id - 1);
            if (ATOMIC_INC(&m_numCompletedJobs) == m_totalNumJobs)
                m_modeCompletionEvent.trigger();
            return true;
        }
    }

    /* else try to acquire a motion estimation task */
    if (m_totalNumME > m_numAcquiredME)
    {
        int id = ATOMIC_INC(&m_numAcquiredME);
        if (m_totalNumME >= id)
        {
            parallelME(threadId, id - 1);
            if (ATOMIC_INC(&m_numCompletedME) == m_totalNumME)
                m_meCompletionEvent.trigger();
            return true;
        }
    }

    return false;
}

void Analysis::parallelAnalysisJob(int threadId, int jobId)
{
    Analysis* slave;
    int depth = m_curDepth;

    if (threadId == -1)
        slave = this;
    else
    {
        TComDataCU& cu = m_modeDepth[depth].pred[PRED_2Nx2N].cu;
        TComPicYuv* fenc = cu.m_pic->getPicYuvOrg();

        slave = &m_tld[threadId].analysis;
        slave->m_me.setSourcePlane(fenc->getLumaAddr(), fenc->getStride());
        slave->m_log = &slave->m_sliceTypeLog[cu.m_slice->m_sliceType];
        m_modeDepth[0].origYuv.copyPartToYuv(&slave->m_modeDepth[depth].origYuv, m_curCUData->encodeIdx);
        slave->setQP(cu.m_slice, m_rdCost.m_qp);
        if (!jobId || m_param->rdLevel > 4)
        {
            slave->m_quant.setQPforQuant(&cu);
            slave->m_quant.m_nr = m_quant.m_nr;
            slave->m_rdContexts[depth].cur.load(m_rdContexts[depth].cur);
        }
    }

    switch (jobId)
    {
    case 0:
        slave->checkIntraInInter_rd0_4(m_modeDepth[depth].pred[PRED_INTRA], m_curCUData);
        if (m_param->rdLevel > 2)
            slave->encodeIntraInInter(m_modeDepth[depth].pred[PRED_INTRA], m_curCUData);
        break;

    case 1:
        slave->checkInter_rd0_4(m_modeDepth[depth].pred[PRED_2Nx2N], m_curCUData, SIZE_2Nx2N);
        break;

    case 2:
        slave->checkInter_rd0_4(m_modeDepth[depth].pred[PRED_Nx2N], m_curCUData, SIZE_Nx2N);
        break;

    case 3:
        slave->checkInter_rd0_4(m_modeDepth[depth].pred[PRED_2NxN], m_curCUData, SIZE_2NxN);
        break;

    default:
        X265_CHECK(0, "invalid job ID for parallel mode analysis\n");
        break;
    }
}

void Analysis::parallelME(int threadId, int meId)
{
    Analysis* slave;
    const TComDataCU *cu = m_curMECu;
    TComPicYuv* fenc = cu->m_pic->getPicYuvOrg();
    Slice *slice = cu->m_slice;

    if (threadId == -1)
        slave = this;
    else
    {
        slave = &m_tld[threadId].analysis;
        slave->m_me.setSourcePlane(fenc->getLumaAddr(), fenc->getStride());
        slave->setQP(slice, m_rdCost.m_qp);
    }

    int ref, l;
    if (meId < slice->m_numRefIdx[0])
    {
        l = 0;
        ref = meId;
    }
    else
    {
        l = 1;
        ref = meId - slice->m_numRefIdx[0];
    }

    uint32_t partAddr;
    int      puWidth, puHeight;
    cu->getPartIndexAndSize(m_curPart, partAddr, puWidth, puHeight);
    slave->prepMotionCompensation(cu, m_curCUData, m_curPart);

    pixel* pu = fenc->getLumaAddr(cu->m_cuAddr, m_curCUData->encodeIdx + partAddr);
    slave->m_me.setSourcePU(pu - fenc->getLumaAddr(), puWidth, puHeight);

    uint32_t bits = m_listSelBits[l] + MVP_IDX_BITS;
    bits += getTUBits(ref, slice->m_numRefIdx[l]);

    MV amvpCand[AMVP_NUM_CANDS];
    MV mvc[(MD_ABOVE_LEFT + 1) * 2 + 1];
    int numMvc = cu->fillMvpCand(m_curPart, partAddr, l, ref, amvpCand, mvc);

    uint32_t bestCost = MAX_INT;
    int mvpIdx = 0;
    int merange = m_param->searchRange;
    for (int i = 0; i < AMVP_NUM_CANDS; i++)
    {
        MV mvCand = amvpCand[i];

        // NOTE: skip mvCand if Y is > merange and -FN>1
        if (m_bFrameParallel && (mvCand.y >= (merange + 1) * 4))
            continue;

        cu->clipMv(mvCand);

        slave->predInterLumaBlk(slice->m_refPicList[l][ref]->getPicYuvRec(), &slave->m_predTempYuv, &mvCand);
        uint32_t cost = m_me.bufSAD(m_predTempYuv.getLumaAddr(partAddr), slave->m_predTempYuv.getStride());

        if (bestCost > cost)
        {
            bestCost = cost;
            mvpIdx  = i;
        }
    }

    MV mvmin, mvmax, outmv, mvp = amvpCand[mvpIdx];
    setSearchRange(cu, mvp, merange, mvmin, mvmax);

    int satdCost = slave->m_me.motionEstimate(&slice->m_mref[l][ref], mvmin, mvmax, mvp, numMvc, mvc, merange, outmv);

    /* Get total cost of partition, but only include MV bit cost once */
    bits += m_me.bitcost(outmv);
    uint32_t cost = (satdCost - m_me.mvcost(outmv)) + m_rdCost.getCost(bits);

    /* Refine MVP selection, updates: mvp, mvpIdx, bits, cost */
    checkBestMVP(amvpCand, outmv, mvp, mvpIdx, bits, cost);

    ScopedLock _lock(m_outputLock);
    if (cost < m_bestME[l].cost)
    {
        m_bestME[l].mv = outmv;
        m_bestME[l].mvp = mvp;
        m_bestME[l].mvpIdx = mvpIdx;
        m_bestME[l].ref = ref;
        m_bestME[l].cost = cost;
        m_bestME[l].bits = bits;
    }
}

void Analysis::compressCTU(TComDataCU* ctu, const Entropy& initialContext)
{
    Frame* pic = ctu->m_pic;
    uint32_t cuAddr = ctu->m_cuAddr;

    invalidateContexts(0);
    m_rdContexts[0].cur.load(initialContext);

    if (ctu->m_slice->m_pps->bUseDQP)
        m_bEncodeDQP = true;

    // analysis of CU
    uint32_t numPartition = ctu->m_cuLocalData->numPartitions;
    if (ctu->m_slice->m_sliceType == I_SLICE)
    {
        if (m_param->analysisMode == X265_ANALYSIS_LOAD && pic->m_intraData)
        {
            uint32_t zOrder = 0;
            compressSharedIntraCTU(ctu, ctu->m_cuLocalData, 
                &pic->m_intraData->depth[cuAddr * ctu->m_numPartitions],
                &pic->m_intraData->partSizes[cuAddr * ctu->m_numPartitions],
                &pic->m_intraData->modes[cuAddr * ctu->m_numPartitions], zOrder);
        }
        else
        {
            compressIntraCU(ctu, ctu->m_cuLocalData);

            if (m_param->analysisMode == X265_ANALYSIS_SAVE && pic->m_intraData)
            {
                TComDataCU *bestCU = &m_modeDepth[0].bestMode->cu;
                memcpy(&pic->m_intraData->depth[cuAddr * ctu->m_numPartitions], bestCU->getDepth(), sizeof(uint8_t) * numPartition);
                memcpy(&pic->m_intraData->modes[cuAddr * ctu->m_numPartitions], bestCU->getLumaIntraDir(), sizeof(uint8_t) * numPartition);
                memcpy(&pic->m_intraData->partSizes[cuAddr * ctu->m_numPartitions], bestCU->getPartitionSize(), sizeof(char) * numPartition);
                pic->m_intraData->cuAddr[cuAddr] = cuAddr;
                pic->m_intraData->poc[cuAddr]    = ctu->m_pic->m_POC;
            }
        }
        if (m_param->bLogCuStats || m_param->rc.bStatWrite)
        {
            uint32_t i = 0;
            do
            {
                m_log->totalCu++;
                uint32_t depth = ctu->getDepth(i);
                int next = numPartition >> (depth * 2);
                m_log->qTreeIntraCnt[depth]++;
                if (depth == g_maxCUDepth && ctu->getPartitionSize(i) != SIZE_2Nx2N)
                    m_log->cntIntraNxN++;
                else
                {
                    m_log->cntIntra[depth]++;
                    if (ctu->getLumaIntraDir(i) > 1)
                        m_log->cuIntraDistribution[depth][ANGULAR_MODE_ID]++;
                    else
                        m_log->cuIntraDistribution[depth][ctu->getLumaIntraDir(i)]++;
                }
                i += next;
            }
            while (i < numPartition);
        }
    }
    else
    {
        if (m_param->rdLevel < 5)
            compressInterCU_rd0_4(ctu, ctu->m_cuLocalData, 0, 4);
        else
            compressInterCU_rd5_6(ctu, ctu->m_cuLocalData, 0);

        if (m_param->bLogCuStats || m_param->rc.bStatWrite)
        {
            uint32_t i = 0;
            do
            {
                uint32_t depth = ctu->getDepth(i);
                m_log->cntTotalCu[depth]++;
                int next = numPartition >> (depth * 2);
                if (ctu->isSkipped(i))
                {
                    m_log->cntSkipCu[depth]++;
                    m_log->qTreeSkipCnt[depth]++;
                }
                else
                {
                    m_log->totalCu++;
                    if (ctu->getPredictionMode(0) == MODE_INTER)
                    {
                        m_log->cntInter[depth]++;
                        m_log->qTreeInterCnt[depth]++;
                        if (ctu->getPartitionSize(0) < AMP_ID)
                            m_log->cuInterDistribution[depth][ctu->getPartitionSize(0)]++;
                        else
                            m_log->cuInterDistribution[depth][AMP_ID]++;
                    }
                    else if (ctu->getPredictionMode(0) == MODE_INTRA)
                    {
                        m_log->qTreeIntraCnt[depth]++;
                        if (depth == g_maxCUDepth && ctu->getPartitionSize(0) == SIZE_NxN)
                            m_log->cntIntraNxN++;
                        else
                        {
                            m_log->cntIntra[depth]++;
                            if (ctu->getLumaIntraDir(0) > 1)
                                m_log->cuIntraDistribution[depth][ANGULAR_MODE_ID]++;
                            else
                                m_log->cuIntraDistribution[depth][ctu->getLumaIntraDir(0)]++;
                        }
                    }
                }
                i = i + next;
            }
            while (i < numPartition);
        }
    }
}

void Analysis::compressIntraCU(TComDataCU* parentCU, CU *cuData)
{
    Frame* pic = parentCU->m_pic;
    uint32_t cuAddr = parentCU->m_cuAddr;
    uint32_t depth = cuData->depth;
    uint32_t absPartIdx = cuData->encodeIdx;
    ModeDepth& md = m_modeDepth[depth];
    md.bestMode = NULL;

    if (depth)
        m_modeDepth[0].origYuv.copyPartToYuv(&md.origYuv, absPartIdx);
    else
        m_modeDepth[0].origYuv.copyFromPicYuv(pic->getPicYuvOrg(), cuAddr, absPartIdx);

    bool mightSplit = !(cuData->flags & CU::LEAF);
    bool mightNotSplit = !(cuData->flags & CU::SPLIT_MANDATORY);

    if (mightNotSplit)
    {
        m_quant.setQPforQuant(parentCU);
        checkIntra(parentCU, cuData, SIZE_2Nx2N, NULL);

        if (depth == g_maxCUDepth)
            checkIntra(parentCU, cuData, SIZE_NxN, NULL);

        if (mightSplit)
        {
            /* add signal cost not no-split flag */
            TComDataCU *cu = &md.bestMode->cu;
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(cu, 0, depth);
            cu->m_totalBits += m_entropyCoder.getNumberOfWrittenBits();
            if (m_rdCost.m_psyRd)
                cu->m_totalRDCost = m_rdCost.calcPsyRdCost(cu->m_totalDistortion, cu->m_totalBits, cu->m_psyEnergy);
            else
                cu->m_totalRDCost = m_rdCost.calcRdCost(cu->m_totalDistortion, cu->m_totalBits);
        }

        // copy original YUV samples in lossless mode
        if (md.bestMode->cu.isLosslessCoded(0))
            fillOrigYUVBuffer(&md.bestMode->cu, &md.origYuv);
    }

    if (mightSplit)
    {
        Mode* splitPred = &md.pred[PRED_SPLIT];
        TComDataCU* splitCU = &splitPred->cu;
        splitCU->initSubCU(parentCU, cuData, 0); // prepare splitCU to accumulate costs
        splitCU->m_totalRDCost = 0;

        uint32_t nextDepth = cuData->depth + 1;
        ModeDepth& nd = m_modeDepth[nextDepth];
        invalidateContexts(nextDepth);

        m_rdContexts[nextDepth].cur.load(m_rdContexts[cuData->depth].cur);

        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++)
        {
            CU *childCuData = pic->getCU(cuAddr)->m_cuLocalData + cuData->childIdx + partUnitIdx;
            if (childCuData->flags & CU::PRESENT)
            {
                compressIntraCU(parentCU, childCuData);

                // Save best CU and pred data for this sub CU
                splitCU->copyPartFrom(&nd.bestMode->cu, childCuData->numPartitions, partUnitIdx, nextDepth);
                nd.bestMode->reconYuv.copyToPartYuv(&splitPred->reconYuv, childCuData->numPartitions * partUnitIdx);
                if (partUnitIdx < 3) m_rdContexts[nextDepth].cur.load(nd.bestMode->contexts);
            }
            else
                splitCU->copyToPic(nextDepth);
        }
        if (mightNotSplit)
        {
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(splitCU, 0, depth);
            splitCU->m_totalBits += m_entropyCoder.getNumberOfWrittenBits();
        }
        nd.bestMode->contexts.store(splitPred->contexts);
        if (m_rdCost.m_psyRd)
            splitCU->m_totalRDCost = m_rdCost.calcPsyRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits, splitCU->m_psyEnergy);
        else
            splitCU->m_totalRDCost = m_rdCost.calcRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits);
        checkDQP(splitCU, cuData);
        checkBestMode(*splitPred, depth);
    }

    // Copy best data to picsym
    md.bestMode->cu.copyToPic(depth);

    // TODO: can this be written as "if (md.bestMode->cu is not split)" to avoid copies?
    // if split was not required, write recon
    if (mightNotSplit)
        md.bestMode->reconYuv.copyToPicYuv(pic->getPicYuvRec(), cuAddr, absPartIdx);
}

void Analysis::compressSharedIntraCTU(TComDataCU* parentCU, CU *cuData, uint8_t* sharedDepth, char* sharedPartSizes, uint8_t* sharedModes, uint32_t &zOrder)
{
    Frame* pic = parentCU->m_pic;
    uint32_t cuAddr = parentCU->m_cuAddr;
    uint32_t depth = cuData->depth;
    uint32_t absPartIdx = cuData->encodeIdx;
    ModeDepth& md = m_modeDepth[depth];
    md.bestMode = NULL;

    if (depth)
        m_modeDepth[0].origYuv.copyPartToYuv(&md.origYuv, absPartIdx);
    else
        m_modeDepth[0].origYuv.copyFromPicYuv(pic->getPicYuvOrg(), cuAddr, absPartIdx);

    bool mightSplit = !(cuData->flags & CU::LEAF);
    bool mightNotSplit = !(cuData->flags & CU::SPLIT_MANDATORY);

    if (mightNotSplit && depth == sharedDepth[zOrder] && zOrder == cuData->encodeIdx)
    {
        m_quant.setQPforQuant(parentCU);
        checkIntra(parentCU, cuData, (PartSize)sharedPartSizes[zOrder], &sharedModes[zOrder]);

        if (mightSplit)
        {
            /* add signal cost not no-split flag */
            TComDataCU *cu = &md.bestMode->cu;
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(cu, 0, depth);
            cu->m_totalBits += m_entropyCoder.getNumberOfWrittenBits();
            if (m_rdCost.m_psyRd)
                cu->m_totalRDCost = m_rdCost.calcPsyRdCost(cu->m_totalDistortion, cu->m_totalBits, cu->m_psyEnergy);
            else
                cu->m_totalRDCost = m_rdCost.calcRdCost(cu->m_totalDistortion, cu->m_totalBits);
        }

        // copy original YUV samples in lossless mode
        if (md.bestMode->cu.isLosslessCoded(0))
            fillOrigYUVBuffer(&md.bestMode->cu, &md.origYuv);

        // increment zOrder offset to point to next best depth in sharedDepth buffer
        zOrder += g_depthInc[g_maxCUDepth - 1][depth];
        mightSplit = false;
    }

    if (mightSplit)
    {
        Mode* splitPred = &md.pred[PRED_SPLIT];
        TComDataCU* splitCU = &splitPred->cu;
        splitCU->initSubCU(parentCU, cuData, 0); // prepare splitCU to accumulate costs
        splitCU->m_totalRDCost = 0;

        uint32_t nextDepth = cuData->depth + 1;
        ModeDepth& nd = m_modeDepth[nextDepth];
        invalidateContexts(nextDepth);

        m_rdContexts[nextDepth].cur.load(m_rdContexts[cuData->depth].cur);

        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++)
        {
            CU *childCuData = pic->getCU(cuAddr)->m_cuLocalData + cuData->childIdx + partUnitIdx;
            if (childCuData->flags & CU::PRESENT)
            {
                compressSharedIntraCTU(splitCU, childCuData, sharedDepth, sharedPartSizes, sharedModes, zOrder);

                // Save best CU and pred data for this sub CU
                splitCU->copyPartFrom(&nd.bestMode->cu, childCuData->numPartitions, partUnitIdx, nextDepth);
                nd.bestMode->reconYuv.copyToPartYuv(&splitPred->reconYuv, childCuData->numPartitions * partUnitIdx);
                if (partUnitIdx < 3) m_rdContexts[nextDepth].cur.load(nd.bestMode->contexts);
            }
            else
            {
                splitCU->copyToPic(nextDepth);
                zOrder += g_depthInc[g_maxCUDepth - 1][nextDepth];
            }
        }
        if (mightNotSplit)
        {
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(splitCU, 0, depth);
            splitCU->m_totalBits += m_entropyCoder.getNumberOfWrittenBits();
        }
        nd.bestMode->contexts.store(splitPred->contexts);
        if (m_rdCost.m_psyRd)
            splitCU->m_totalRDCost = m_rdCost.calcPsyRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits, splitCU->m_psyEnergy);
        else
            splitCU->m_totalRDCost = m_rdCost.calcRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits);
        checkDQP(splitCU, cuData);
        checkBestMode(*splitPred, depth);
    }

    // Copy best data to picsym
    md.bestMode->cu.copyToPic(depth);

    // TODO: can this be written as "if (md.bestMode->cu is not split)" to avoid copies?
    // if split was not required, write recon
    if (mightNotSplit)
        md.bestMode->reconYuv.copyToPicYuv(pic->getPicYuvRec(), cuAddr, absPartIdx);
}

/* TODO: move to Search except checkDQP() and checkBestMode() */
void Analysis::checkIntra(TComDataCU* parentCU, CU *cuData, PartSize partSize, uint8_t* sharedModes)
{
    uint32_t depth = cuData->depth;
    Mode& mode = partSize == SIZE_2Nx2N ? m_modeDepth[depth].pred[PRED_INTRA] : m_modeDepth[depth].pred[PRED_INTRA_NxN];
    TComDataCU& cu = mode.cu;
    TComYuv& orig = m_modeDepth[depth].origYuv;

    cu.initSubCU(parentCU, cuData, 0);
    cu.setPartSizeSubParts(partSize, 0, depth);
    cu.setPredModeSubParts(MODE_INTRA, 0, depth);
    cu.setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    uint32_t tuDepthRange[2];
    cu.getQuadtreeTULog2MinSizeInCU(tuDepthRange, 0);

    if (sharedModes)
        sharedEstIntraPredQT(mode, cuData, &orig, tuDepthRange, sharedModes);
    else
        estIntraPredQT(mode, cuData, &orig, tuDepthRange);

    estIntraPredChromaQT(mode, cuData, &orig);

    m_entropyCoder.resetBits();
    if (cu.m_slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder.codeCUTransquantBypassFlag(cu.getCUTransquantBypass(0));

    m_entropyCoder.codePartSize(&cu, 0, depth);
    m_entropyCoder.codePredInfo(&cu, 0);
    cu.m_mvBits = m_entropyCoder.getNumberOfWrittenBits();

    bool bCodeDQP = m_bEncodeDQP;
    m_entropyCoder.codeCoeff(&cu, 0, depth, bCodeDQP, tuDepthRange);
    m_entropyCoder.store(mode.contexts);
    cu.m_totalBits = m_entropyCoder.getNumberOfWrittenBits();
    cu.m_coeffBits = cu.m_totalBits - cu.m_mvBits;

    if (m_rdCost.m_psyRd)
    {
        int part = cu.getLog2CUSize(0) - 2;
        cu.m_psyEnergy = m_rdCost.psyCost(part, orig.getLumaAddr(), orig.getStride(), mode.reconYuv.getLumaAddr(), mode.reconYuv.getStride());
        cu.m_totalRDCost = m_rdCost.calcPsyRdCost(cu.m_totalDistortion, cu.m_totalBits, cu.m_psyEnergy);
    }
    else
        cu.m_totalRDCost = m_rdCost.calcRdCost(cu.m_totalDistortion, cu.m_totalBits);

    checkDQP(&cu, cuData);
    checkBestMode(mode, depth);
}

void Analysis::compressInterCU_rd0_4(TComDataCU* parentCU, CU *cuData, uint32_t partitionIndex, uint32_t minDepth)
{
    Slice* slice = parentCU->m_slice;
    Frame* pic = parentCU->m_pic;
    uint32_t depth = cuData->depth;
    uint32_t cuAddr = parentCU->m_cuAddr;
    uint32_t absPartIdx = cuData->encodeIdx;
    ModeDepth& md = m_modeDepth[depth];
    md.bestMode = NULL;

    if (depth)
        m_modeDepth[0].origYuv.copyPartToYuv(&md.origYuv, absPartIdx);
    else
        m_modeDepth[0].origYuv.copyFromPicYuv(pic->getPicYuvOrg(), cuAddr, absPartIdx);

    bool mightSplit = !(cuData->flags & CU::LEAF);
    bool mightNotSplit = !(cuData->flags & CU::SPLIT_MANDATORY);

    if (!depth && !m_param->rdLevel)
        md.origYuv.copyToPicYuv(pic->getPicYuvRec(), cuAddr, 0);

    if (mightNotSplit)
    {
        /* refine minimum recursion depth */
        TComDataCU* colocated0 = slice->m_numRefIdx[0] > 0 ? slice->m_refPicList[0][0]->getCU(cuAddr) : NULL;
        TComDataCU* colocated1 = slice->m_numRefIdx[1] > 0 ? slice->m_refPicList[1][0]->getCU(cuAddr) : NULL;
        char currentQP = parentCU->getQP(0);
        char previousQP = colocated0->getQP(0);
        uint32_t delta = 0, minDepth0 = 4, minDepth1 = 4;
        uint32_t sum0 = 0, sum1 = 0;
        for (uint32_t i = 0; i < cuData->numPartitions; i = i + 4)
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

        uint32_t avgDepth2 = (sum0 + sum1) / cuData->numPartitions;
        minDepth = X265_MIN(minDepth0, minDepth1);
        if (((currentQP - previousQP) < 0) || (((currentQP - previousQP) >= 0) && ((avgDepth2 - 2 * minDepth) > 1)))
            delta = 0;
        else
            delta = 1;
        if (minDepth > 0)
            minDepth = minDepth - delta;
    }

    if (mightNotSplit && depth >= minDepth)
    {
        if (depth)
        {
            /* Initialize all prediction CUs based on parentCU */
            md.pred[PRED_2Nx2N].cu.initSubCU(parentCU, cuData, partitionIndex);
            md.pred[PRED_MERGE].cu.initSubCU(parentCU, cuData, partitionIndex);
            md.pred[PRED_SKIP].cu.initSubCU(parentCU, cuData, partitionIndex);
            if (m_param->bEnableRectInter)
            {
                md.pred[PRED_2NxN].cu.initSubCU(parentCU, cuData, partitionIndex);
                md.pred[PRED_Nx2N].cu.initSubCU(parentCU, cuData, partitionIndex);
            }
            if (slice->m_sliceType == P_SLICE)
                md.pred[PRED_INTRA].cu.initSubCU(parentCU, cuData, partitionIndex);
        }
        else
        {
            md.pred[PRED_2Nx2N].cu.initCU(pic, cuAddr);
            md.pred[PRED_MERGE].cu.initCU(pic, cuAddr);
            md.pred[PRED_SKIP].cu.initCU(pic, cuAddr);
            if (m_param->bEnableRectInter)
            {
                md.pred[PRED_2NxN].cu.initCU(pic, cuAddr);
                md.pred[PRED_Nx2N].cu.initCU(pic, cuAddr);
            }
            if (slice->m_sliceType == P_SLICE)
                md.pred[PRED_INTRA].cu.initCU(pic, cuAddr);
        }

        if (m_param->bDistributeModeAnalysis)
        {
            /* with distributed analysis, we perform more speculative work.
             * We do not have early outs for when skips are found so we
             * always evaluate intra and all inter and merge modes
             *
             * jobs are numbered as:
             *  0 = intra
             *  1 = inter 2Nx2N
             *  2 = inter Nx2N
             *  3 = inter 2NxN */
            m_totalNumJobs = 2 + m_param->bEnableRectInter * 2;
            m_numAcquiredJobs = slice->m_sliceType != P_SLICE; /* skip intra for B slices */
            m_numCompletedJobs = m_numAcquiredJobs;
            m_curDepth = depth;
            m_curCUData = cuData;
            m_bJobsQueued = true;
            JobProvider::enqueue();

            for (int i = 0; i < m_totalNumJobs - m_numCompletedJobs; i++)
                m_pool->pokeIdleThread();

            /* the master worker thread (this one) does merge analysis */
            checkMerge2Nx2N_rd0_4(cuData, depth);

            bool earlyskip = false;
            md.bestMode = &md.pred[PRED_SKIP];
            if (m_param->rdLevel >= 1)
            {
                if (md.pred[PRED_MERGE].cu.m_totalRDCost < md.bestMode->cu.m_totalRDCost)
                    md.bestMode = &md.pred[PRED_MERGE];
                earlyskip = m_param->bEnableEarlySkip && md.bestMode->cu.isSkipped(0);
            }
            else
            {
                if (md.pred[PRED_MERGE].cu.m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                    md.bestMode = &md.pred[PRED_MERGE];
            }

            if (earlyskip)
            {
                /* a SKIP was found, consume remaining jobs to conserve work */
                JobProvider::dequeue();
                while (m_totalNumJobs > m_numAcquiredJobs)
                {
                    int id = ATOMIC_INC(&m_numAcquiredJobs);
                    if (m_totalNumJobs >= id)
                    {
                        if (ATOMIC_INC(&m_numCompletedJobs) == m_totalNumJobs)
                            m_modeCompletionEvent.trigger();
                    }
                }
            }
            else
            {
                /* participate in processing remaining jobs */
                while (findJob(-1))
                    ;
                JobProvider::dequeue();
            }
            m_bJobsQueued = false;
            m_modeCompletionEvent.wait();

            if (!earlyskip)
            {
                /* select best inter mode based on sa8d cost */
                Mode *bestInter = &md.pred[PRED_2Nx2N];
                if (m_param->bEnableRectInter)
                {
                    if (md.pred[PRED_Nx2N].cu.m_sa8dCost < bestInter->cu.m_sa8dCost)
                        bestInter = &md.pred[PRED_Nx2N];
                    if (md.pred[PRED_Nx2N].cu.m_sa8dCost < bestInter->cu.m_sa8dCost)
                        bestInter = &md.pred[PRED_Nx2N];
                }

                if (m_param->rdLevel > 2)
                {
                    /* build chroma prediction for best inter */
                    for (int partIdx = 0; partIdx < bestInter->cu.getNumPartInter(); partIdx++)
                    {
                        prepMotionCompensation(&bestInter->cu, cuData, partIdx);
                        motionCompensation(&bestInter->predYuv, false, true);
                    }

                    /* RD selection between inter and merge */
                    encodeResAndCalcRdInterCU(md.pred[PRED_2Nx2N], cuData, &md.origYuv, &md.tempResi);
                    m_rdContexts[depth].temp.store(bestInter->contexts); /* TODO: pass mode to encodeResAndCalcRdInterCU(), save to mode.contexts */

                    if (md.bestMode->cu.m_totalRDCost < bestInter->cu.m_totalRDCost)
                        md.bestMode = bestInter;

                    if (md.pred[PRED_INTRA].cu.m_totalRDCost < md.bestMode->cu.m_totalRDCost)
                        md.bestMode = &md.pred[PRED_INTRA];
                }
                else
                {
                    if (bestInter->cu.m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                        md.bestMode = bestInter;

                    if (md.pred[PRED_INTRA].cu.m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                        md.bestMode = &md.pred[PRED_INTRA];
                }
            }
        }
        else
        {
            /* Compute Merge Cost */
            checkMerge2Nx2N_rd0_4(cuData, depth);

            bool earlyskip = false;
            md.bestMode = &md.pred[PRED_SKIP];
            if (m_param->rdLevel >= 1)
            {
                if (md.pred[PRED_MERGE].cu.m_totalRDCost < md.bestMode->cu.m_totalRDCost)
                    md.bestMode = &md.pred[PRED_MERGE];
                earlyskip = m_param->bEnableEarlySkip && md.bestMode->cu.isSkipped(0);
            }
            else
            {
                if (md.pred[PRED_MERGE].cu.m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                    md.bestMode = &md.pred[PRED_MERGE];
            }

            if (!earlyskip)
            {
                checkInter_rd0_4(md.pred[PRED_2Nx2N], cuData, SIZE_2Nx2N);
                Mode *bestInter = &md.pred[PRED_2Nx2N];

                if (m_param->bEnableRectInter)
                {
                    checkInter_rd0_4(md.pred[PRED_Nx2N], cuData, SIZE_Nx2N);
                    if (md.pred[PRED_Nx2N].cu.m_sa8dCost < bestInter->cu.m_sa8dCost)
                        bestInter = &md.pred[PRED_Nx2N];
                    checkInter_rd0_4(md.pred[PRED_2NxN], cuData, SIZE_2NxN);
                    if (md.pred[PRED_Nx2N].cu.m_sa8dCost < bestInter->cu.m_sa8dCost)
                        bestInter = &md.pred[PRED_Nx2N];
                }

                if (m_param->rdLevel > 2)
                {
                    /* Calculate RD cost of best inter option */
                    int numPart = bestInter->cu.getNumPartInter();
                    for (int partIdx = 0; partIdx < numPart; partIdx++)
                    {
                        prepMotionCompensation(&bestInter->cu, cuData, partIdx);
                        motionCompensation(&bestInter->predYuv, false, true);
                    }

                    encodeResAndCalcRdInterCU(md.pred[PRED_2Nx2N], cuData, &md.origYuv, &md.tempResi);
                    m_rdContexts[depth].temp.store(bestInter->contexts);

                    if (bestInter->cu.m_totalRDCost < md.bestMode->cu.m_totalRDCost)
                        md.bestMode = bestInter;

                    bool bdoIntra = md.bestMode->cu.getCbf(0, TEXT_LUMA) || md.bestMode->cu.getCbf(0, TEXT_CHROMA_U) || md.bestMode->cu.getCbf(0, TEXT_CHROMA_V);
                    if (slice->m_sliceType == P_SLICE && bdoIntra)
                    {
                        checkIntraInInter_rd0_4(md.pred[PRED_INTRA], cuData);
                        encodeIntraInInter(md.pred[PRED_INTRA], cuData);
                        if (md.pred[PRED_INTRA].cu.m_totalRDCost < md.bestMode->cu.m_totalRDCost)
                            md.bestMode = &md.pred[PRED_INTRA];
                    }
                }
                else
                {
                    /* SA8D choice between merge/skip, inter, and intra */
                    if (bestInter->cu.m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                        md.bestMode = bestInter;

                    checkIntraInInter_rd0_4(md.pred[PRED_INTRA], cuData);
                    if (md.pred[PRED_INTRA].cu.m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                        md.bestMode = &md.pred[PRED_INTRA];
                }
            } // !earlyskip
        }  // !pmode

        /* low RD levels require follow-up work on best mode */
        if (m_param->rdLevel == 2)
        {
            /* finally code the best mode selected from SA8D costs */
            TComDataCU* bestCU = &md.bestMode->cu;
            if (bestCU->getPredictionMode(0) == MODE_INTER)
            {
                int numPart = bestCU->getNumPartInter();
                for (int partIdx = 0; partIdx < numPart; partIdx++)
                {
                    prepMotionCompensation(bestCU, cuData, partIdx);
                    motionCompensation(&md.bestMode->predYuv, false, true);
                }
                encodeResAndCalcRdInterCU(*md.bestMode, cuData, &md.origYuv, &md.tempResi);
                m_rdContexts[depth].temp.store(md.bestMode->contexts);
            }
            else if (bestCU->getPredictionMode(0) == MODE_INTRA)
                encodeIntraInInter(*md.bestMode, cuData);
        }
        else if (m_param->rdLevel <= 1)
        {
            /* Generate recon YUV for this CU. Note: does not update any CABAC context! */
            TComDataCU* bestCU = &md.bestMode->cu;
            if (bestCU->getPredictionMode(0) == MODE_INTER)
            {
                int numPart = bestCU->getNumPartInter();
                for (int partIdx = 0; partIdx < numPart; partIdx++)
                {
                    prepMotionCompensation(bestCU, cuData, partIdx);
                    motionCompensation(&md.bestMode->predYuv, false, true);
                }

                md.bestMode->resiYuv.subtract(&md.origYuv, &md.bestMode->predYuv, bestCU->getLog2CUSize(0));
                /* TODO: generateCoeffRecon() should take Mode */
                generateCoeffRecon(bestCU, cuData, &md.origYuv, &md.bestMode->predYuv, &md.bestMode->resiYuv, &md.bestMode->reconYuv);
            }
            else
                generateCoeffRecon(bestCU, cuData, &md.origYuv, &md.bestMode->predYuv, &md.bestMode->resiYuv, &md.bestMode->reconYuv);
        }

        TComDataCU* bestCU = &md.bestMode->cu;
        if (m_param->rdLevel > 0) // checkDQP can be done only after residual encoding is done
            checkDQP(bestCU, cuData);

        if (m_param->rdLevel > 1 && depth < g_maxCUDepth)
        {
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(bestCU, 0, depth);
            bestCU->m_totalBits += m_entropyCoder.getNumberOfWrittenBits(); // split bits

            if (m_rdCost.m_psyRd)
                bestCU->m_totalRDCost = m_rdCost.calcPsyRdCost(bestCU->m_totalDistortion, bestCU->m_totalBits, bestCU->m_psyEnergy);
            else
                bestCU->m_totalRDCost = m_rdCost.calcRdCost(bestCU->m_totalDistortion, bestCU->m_totalBits);
        }

        // copy original YUV samples in lossless mode
        if (bestCU->isLosslessCoded(0))
            fillOrigYUVBuffer(bestCU, &md.origYuv);
    }

    /* do not try splits if best mode is already a skip */
    mightSplit &= !md.bestMode || !md.bestMode->cu.isSkipped(0);

    if (mightSplit && depth && depth >= minDepth)
    {
        // early exit when the RD cost of best mode at depth n is less than the sum of average of RD cost of the neighbor
        // CU's(above, aboveleft, aboveright, left, colocated) and avg cost of that CU at depth "n" with weightage for each quantity

        const TComDataCU* above = parentCU->getCUAbove();
        const TComDataCU* aboveLeft = parentCU->getCUAboveLeft();
        const TComDataCU* aboveRight = parentCU->getCUAboveRight();
        const TComDataCU* left = parentCU->getCULeft();
        TComDataCU* ctu = pic->getPicSym()->getCU(cuAddr);
        uint64_t neighCost = 0, cuCost = 0, neighCount = 0, cuCount = 0;

        cuCost += ctu->m_avgCost[depth] * ctu->m_count[depth];
        cuCount += ctu->m_count[depth];
        if (above)
        {
            neighCost += above->m_avgCost[depth] * above->m_count[depth];
            neighCount += above->m_count[depth];
        }
        if (aboveLeft)
        {
            neighCost += aboveLeft->m_avgCost[depth] * aboveLeft->m_count[depth];
            neighCount += aboveLeft->m_count[depth];
        }
        if (aboveRight)
        {
            neighCost += aboveRight->m_avgCost[depth] * aboveRight->m_count[depth];
            neighCount += aboveRight->m_count[depth];
        }
        if (left)
        {
            neighCost += left->m_avgCost[depth] * left->m_count[depth];
            neighCount += left->m_count[depth];
        }

        // give 60% weight to all CU's and 40% weight to neighbour CU's
        uint64_t avgCost = 0;
        if (neighCount + cuCount)
            avgCost = ((3 * cuCost) + (2 * neighCost)) / ((3 * cuCount) + (2 * neighCount));

        if (md.bestMode->cu.m_totalRDCost < avgCost && avgCost)
            mightSplit = false;
    }

    if (mightSplit)
    {
        Mode* splitPred = &md.pred[PRED_SPLIT];
        TComDataCU* splitCU = &splitPred->cu;
        splitCU->initSubCU(parentCU, cuData, 0); // prepare splitCU to accumulate costs
        splitCU->m_totalRDCost = splitCU->m_sa8dCost = 0;

        uint32_t nextDepth = cuData->depth + 1;
        ModeDepth& nd = m_modeDepth[nextDepth];
        invalidateContexts(nextDepth);

        m_rdContexts[nextDepth].cur.load(m_rdContexts[cuData->depth].cur);

        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++)
        {
            CU *childCuData = pic->getCU(cuAddr)->m_cuLocalData + cuData->childIdx + partUnitIdx;
            if (childCuData->flags & CU::PRESENT)
            {
                compressInterCU_rd0_4(splitCU, childCuData, partUnitIdx, minDepth);

                if (nd.bestMode->cu.getPredictionMode(0) != MODE_INTRA)
                {
                    /* more early-out statistics */
                    TComDataCU* ctu = pic->getPicSym()->getCU(cuAddr);
                    uint64_t temp = ctu->m_avgCost[nextDepth] * ctu->m_count[nextDepth];
                    ctu->m_count[nextDepth] += 1;
                    ctu->m_avgCost[nextDepth] = (temp + nd.bestMode->cu.m_totalRDCost) / ctu->m_count[nextDepth];
                }

                // Save best CU and pred data for this sub CU
                splitCU->copyPartFrom(&nd.bestMode->cu, childCuData->numPartitions, partUnitIdx, nextDepth);
                if (m_param->rdLevel > 1)
                {
                    nd.bestMode->reconYuv.copyToPartYuv(&splitPred->reconYuv, childCuData->numPartitions * partUnitIdx);
                    if (partUnitIdx < 3) m_rdContexts[nextDepth].cur.load(nd.bestMode->contexts);
                }
                else
                    nd.bestMode->predYuv.copyToPartYuv(&splitPred->predYuv, childCuData->numPartitions * partUnitIdx);
            }
            else
                splitCU->copyToPic(nextDepth);
        }
        if (m_param->rdLevel > 1)
        {
            if (mightNotSplit)
            {
                m_entropyCoder.resetBits();
                m_entropyCoder.codeSplitFlag(splitCU, 0, depth);
                splitCU->m_totalBits += m_entropyCoder.getNumberOfWrittenBits();
            }
            if (m_rdCost.m_psyRd)
                splitCU->m_totalRDCost = m_rdCost.calcPsyRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits, splitCU->m_psyEnergy);
            else
                splitCU->m_totalRDCost = m_rdCost.calcRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits);
            nd.bestMode->contexts.store(splitPred->contexts);
        }
        else
        {
            m_rdContexts[depth].cur.store(splitPred->contexts); // NOP
            splitCU->m_sa8dCost = m_rdCost.calcRdSADCost(splitCU->m_totalDistortion, splitCU->m_totalBits);
        }
        checkDQP(splitCU, cuData);

        if (!depth)
        {
            /* more early-out statistics */
            TComDataCU* ctu = pic->getPicSym()->getCU(cuAddr);
            uint64_t temp = ctu->m_avgCost[depth] * ctu->m_count[depth];
            ctu->m_count[depth] += 1;
            ctu->m_avgCost[depth] = (temp + md.bestMode->cu.m_totalRDCost) / ctu->m_count[depth];
        }

        if (m_param->rdLevel > 1)
        {
            if (splitCU->m_totalRDCost < md.bestMode->cu.m_totalRDCost)
                md.bestMode = splitPred;
        }
        else
        {
            if (splitCU->m_sa8dCost < md.bestMode->cu.m_sa8dCost)
                md.bestMode = splitPred;
        }
    }

    /* Copy Best data to Picture for next partition prediction */
    md.bestMode->cu.copyToPic(depth);

    if (m_param->rdLevel <= 1)
    {
        if (!depth)
            // finish CTU, generate recon
            encodeResidue(pic->getPicSym()->getCU(cuAddr), cuData, 0, 0);
    }
    else
    {
        /* Copy Yuv data to picture Yuv */
        if (mightNotSplit)
            md.bestMode->reconYuv.copyToPicYuv(pic->getPicYuvRec(), cuAddr, absPartIdx);
    }

    x265_emms();
}

void Analysis::compressInterCU_rd5_6(TComDataCU* parentCU, CU *cuData, uint32_t partitionIndex)
{
    Frame* pic = parentCU->m_pic;
    Slice* slice = parentCU->m_slice;
    uint32_t depth = cuData->depth;
    uint32_t cuAddr = parentCU->m_cuAddr;
    uint32_t absPartIdx = cuData->encodeIdx;
    ModeDepth& md = m_modeDepth[depth];
    md.bestMode = NULL;

    if (depth)
        // copy partition YUV from depth 0 CTU cache
        m_modeDepth[0].origYuv.copyPartToYuv(&md.origYuv, absPartIdx);
    else
        // get original YUV data from picture
        m_modeDepth[0].origYuv.copyFromPicYuv(pic->getPicYuvOrg(), cuAddr, absPartIdx);

    bool earlySkip = false;
    int cu_split_flag = !(cuData->flags & CU::LEAF);
    int cu_unsplit_flag = !(cuData->flags & CU::SPLIT_MANDATORY);

    X265_CHECK(slice->m_sliceType != I_SLICE, "compressInterCU_rd5_6() called for I_SLICE\n");

    // We need to split, so don't try these modes.
    if (cu_unsplit_flag)
    {
        if (depth)
        {
            for (int i = 0; i < MAX_PRED_TYPES; i++)
                md.pred[i].cu.initSubCU(parentCU, cuData, partitionIndex);
        }
        else
        {
            for (int i = 0; i < MAX_PRED_TYPES; i++)
                md.pred[i].cu.initCU(pic, parentCU->m_cuAddr);
        }

        m_quant.setQPforQuant(parentCU);

        checkMerge2Nx2N_rd5_6(cuData, depth, earlySkip);

        if (!earlySkip)
        {
            checkInter_rd5_6(md.pred[PRED_2Nx2N], cuData, SIZE_2Nx2N, false);

            // TODO: remove me
            if (cuData->log2CUSize != 3 && depth == g_maxCUDepth &&
                (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0)))
                checkInter_rd5_6(md.pred[PRED_NxN], cuData, SIZE_NxN, false);

            if (m_param->bEnableRectInter)
            {
                // Nx2N rect
                if (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0))
                    checkInter_rd5_6(md.pred[PRED_Nx2N], cuData, SIZE_Nx2N, false);
                if (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0))
                    checkInter_rd5_6(md.pred[PRED_2NxN], cuData, SIZE_2NxN, false);
            }

            // Try AMP (SIZE_2NxnU, SIZE_2NxnD, SIZE_nLx2N, SIZE_nRx2N)
            if (slice->m_sps->maxAMPDepth > depth)
            {
                bool bHor = false, bVer = false, bMergeOnly;

                /* TODO: Check how HM used parent size; ours was broken */
                deriveTestModeAMP(&md.bestMode->cu, bHor, bVer, bMergeOnly);

                if (bHor)
                {
                    if (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0))
                        checkInter_rd5_6(md.pred[PRED_2NxnU], cuData, SIZE_2NxnU, bMergeOnly);
                    if (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0))
                        checkInter_rd5_6(md.pred[PRED_2NxnD], cuData, SIZE_2NxnD, bMergeOnly);
                }
                if (bVer)
                {
                    if (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0))
                        checkInter_rd5_6(md.pred[PRED_nLx2N], cuData, SIZE_nLx2N, bMergeOnly);
                    if (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0))
                        checkInter_rd5_6(md.pred[PRED_nRx2N], cuData, SIZE_nRx2N, bMergeOnly);
                }
            }

            if ((slice->m_sliceType != B_SLICE || m_param->bIntraInBFrames) &&
                (!m_param->bEnableCbfFastMode || md.bestMode->cu.getQtRootCbf(0)))
            {
                checkIntraInInter_rd5_6(md.pred[PRED_INTRA], cuData, SIZE_2Nx2N);

                if (depth == g_maxCUDepth && cuData->log2CUSize > slice->m_sps->quadtreeTULog2MinSize)
                    checkIntraInInter_rd5_6(md.pred[PRED_INTRA_NxN], cuData, SIZE_NxN);
            }
        }

        TComDataCU* bestCU = &md.bestMode->cu;
        if (depth < g_maxCUDepth)
        {
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(bestCU, 0, depth);
            bestCU->m_totalBits += m_entropyCoder.getNumberOfWrittenBits(); // split bits
            if (m_rdCost.m_psyRd)
                bestCU->m_totalRDCost = m_rdCost.calcPsyRdCost(bestCU->m_totalDistortion, bestCU->m_totalBits, bestCU->m_psyEnergy);
            else
                bestCU->m_totalRDCost = m_rdCost.calcRdCost(bestCU->m_totalDistortion, bestCU->m_totalBits);
        }

        // copy original YUV samples in lossless mode
        if (bestCU->isLosslessCoded(0))
            fillOrigYUVBuffer(bestCU, &md.origYuv);
    }

    TComDataCU* bestCU = &md.bestMode->cu;

    // estimate split cost
    if (cu_split_flag && !bestCU->isSkipped(0))
    {
        uint32_t nextDepth = depth + 1;
        invalidateContexts(nextDepth);
        Mode* splitPred = &md.pred[PRED_SPLIT];
        TComDataCU *splitCU = &splitPred->cu;
        ModeDepth& nd = m_modeDepth[nextDepth];

        m_rdContexts[nextDepth].cur.load(m_rdContexts[depth].cur);

        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++)
        {
            CU *childCUData = pic->getCU(cuAddr)->m_cuLocalData + cuData->childIdx + partUnitIdx;
            splitCU->initSubCU(parentCU, childCUData, partUnitIdx);

            if (childCUData->flags & CU::PRESENT)
            {
                compressInterCU_rd5_6(splitCU, childCUData, partUnitIdx);

                splitCU->copyPartFrom(&nd.bestMode->cu, childCUData->numPartitions, partUnitIdx, nextDepth);
                nd.bestMode->reconYuv.copyToPartYuv(&splitPred->predYuv, childCUData->numPartitions * partUnitIdx);
                if (partUnitIdx < 3)
                    m_rdContexts[nextDepth].cur.load(nd.bestMode->contexts);
            }
            else
                splitCU->copyToPic(nextDepth);
        }

        if (cu_unsplit_flag)
        {
            /* TODO: check if absPartIdx is right, does this code a 1? hard-code bits */
            m_entropyCoder.resetBits();
            m_entropyCoder.codeSplitFlag(splitCU, 0, depth);
            splitCU->m_totalBits += m_entropyCoder.getNumberOfWrittenBits();
            if (m_rdCost.m_psyRd)
                splitCU->m_totalRDCost = m_rdCost.calcPsyRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits, splitCU->m_psyEnergy);
            else
                splitCU->m_totalRDCost = m_rdCost.calcRdCost(splitCU->m_totalDistortion, splitCU->m_totalBits);
        }

        if (depth == slice->m_pps->maxCuDQPDepth && slice->m_pps->bUseDQP)
        {
            bool hasResidual = false;
            for (uint32_t blkIdx = 0; blkIdx < cuData->numPartitions; blkIdx++)
            {
                if (splitCU->getCbf(blkIdx, TEXT_LUMA) || splitCU->getCbf(blkIdx, TEXT_CHROMA_U) || splitCU->getCbf(blkIdx, TEXT_CHROMA_V))
                {
                    hasResidual = true;
                    break;
                }
            }

            uint32_t targetPartIdx = 0;
            if (hasResidual)
            {
                bool foundNonZeroCbf = false;
                splitCU->setQPSubCUs(splitCU->getRefQP(targetPartIdx), splitCU, 0, depth, foundNonZeroCbf);
                X265_CHECK(foundNonZeroCbf, "expected to find non-zero CBF\n");
            }
            else
                splitCU->setQPSubParts(splitCU->getRefQP(targetPartIdx), 0, depth); // set QP to default QP
        }

        nd.bestMode->contexts.store(splitPred->contexts);
        checkBestMode(*splitPred, depth);
    }

    // Copy best data to picsym and recon
    md.bestMode->cu.copyToPic(depth);
    md.bestMode->reconYuv.copyToPicYuv(pic->getPicYuvRec(), cuAddr, absPartIdx);
}

void Analysis::checkMerge2Nx2N_rd0_4(CU* cuData, uint32_t depth)
{
    ModeDepth& md = m_modeDepth[depth];

    /* Note that these two Mode instances are named MERGE and SKIP but they may
     * be reversed. We use the two modes to toggle between */
    Mode* mergePred = &md.pred[PRED_MERGE];
    Mode* skipPred  = &md.pred[PRED_SKIP];
    TComDataCU* mergeCU = &md.pred[PRED_MERGE].cu;
    TComDataCU* skipCU = &md.pred[PRED_SKIP].cu;

    X265_CHECK(mergeCU->m_slice->m_sliceType != I_SLICE, "Evaluating merge in I slice\n");

    mergeCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to CTU level
    mergeCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    mergeCU->setPredModeSubParts(MODE_INTER, 0, depth);
    mergeCU->setMergeFlag(0, true);

    skipCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to CTU level
    skipCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    skipCU->setPredModeSubParts(MODE_INTER, 0, depth);
    skipCU->setMergeFlag(0, true);

    TComMvField mvFieldNeighbours[MRG_MAX_NUM_CANDS][2]; // double length for mv of both lists
    uint8_t interDirNeighbours[MRG_MAX_NUM_CANDS];
    uint32_t maxNumMergeCand = mergeCU->m_slice->m_maxNumMergeCand;
    mergeCU->getInterMergeCandidates(0, 0, mvFieldNeighbours, interDirNeighbours, maxNumMergeCand);

    TComYuv *fencYuv = &md.origYuv;
    TComYuv *predYuv = &md.pred[PRED_MERGE].predYuv;

    int bestSadCand = -1;
    int sizeIdx = mergeCU->getLog2CUSize(0) - 2;
    for (uint32_t i = 0; i < maxNumMergeCand; ++i)
    {
        if (!m_bFrameParallel ||
            (mvFieldNeighbours[i][0].mv.y < (m_param->searchRange + 1) * 4 &&
             mvFieldNeighbours[i][1].mv.y < (m_param->searchRange + 1) * 4))
        {
            // set MC parameters, interprets depth relative to CTU level
            mergeCU->setMergeIndex(0, i);
            mergeCU->setInterDirSubParts(interDirNeighbours[i], 0, 0, depth);
            mergeCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(mvFieldNeighbours[i][0], SIZE_2Nx2N, 0, 0);
            mergeCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(mvFieldNeighbours[i][1], SIZE_2Nx2N, 0, 0);

            // do MC only for Luma part
            prepMotionCompensation(mergeCU, cuData, 0);
            motionCompensation(predYuv, true, false);

            uint32_t bitsCand = getTUBits(i, maxNumMergeCand);
            mergeCU->m_totalBits = bitsCand;
            mergeCU->m_totalDistortion = primitives.sa8d[sizeIdx](fencYuv->getLumaAddr(), fencYuv->getStride(), predYuv->getLumaAddr(), predYuv->getStride());
            mergeCU->m_sa8dCost = m_rdCost.calcRdSADCost(mergeCU->m_totalDistortion, mergeCU->m_totalBits);

            if (mergeCU->m_sa8dCost < skipCU->m_sa8dCost)
            {
                bestSadCand = i;
                std::swap(mergePred, skipPred);
                mergeCU = &mergePred->cu;
                predYuv = &mergePred->predYuv;
            }
        }
    }

    /* skipPred points to the be prediction and costs */
    skipCU = &md.pred[PRED_SKIP].cu;
    if (bestSadCand < 0)
        return;

    mergeCU->setMergeIndex(0, bestSadCand);
    mergeCU->setInterDirSubParts(interDirNeighbours[bestSadCand], 0, 0, depth);
    mergeCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(mvFieldNeighbours[bestSadCand][0], SIZE_2Nx2N, 0, 0);
    mergeCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(mvFieldNeighbours[bestSadCand][1], SIZE_2Nx2N, 0, 0);
    mergeCU->m_totalBits = skipCU->m_totalBits;
    mergeCU->m_totalDistortion = skipCU->m_totalDistortion;
    mergeCU->m_sa8dCost = skipCU->m_sa8dCost;

    if (m_param->rdLevel >= 1)
    {
        for (int partIdx = 0; partIdx < skipCU->getNumPartInter(); partIdx++)
        {
            // calculate the motion compensation for chroma for the best mode selected
            prepMotionCompensation(skipCU, cuData, partIdx);
            motionCompensation(&skipPred->predYuv, false, true);
        }

        if (skipCU->isLosslessCoded(0))
            skipCU->m_totalRDCost = MAX_INT64;
        else
        {
            // Skip (no-residual) mode
            encodeResAndCalcRdSkipCU(md.pred[PRED_SKIP], fencYuv);
            m_rdContexts[depth].temp.store(skipPred->contexts); /* TODO: encodeResAndCalcRdSkipCU() should write to this */
        }

        // Encode with residue
        mergePred->predYuv.copyFromYuv(&skipPred->predYuv);
        encodeResAndCalcRdInterCU(md.pred[PRED_MERGE], cuData, fencYuv, &md.tempResi);
        m_rdContexts[depth].temp.store(mergePred->contexts); /* Pass mode to encodeResAndCalcRdInterCU(), write to contexts */
    }
}

void Analysis::checkMerge2Nx2N_rd5_6(CU* cuData, uint32_t depth, bool& earlySkip)
{
    ModeDepth& md = m_modeDepth[depth];

    /* Note that these two Mode instances are named MERGE and SKIP but they may
     * be reversed. We use the two modes to toggle between */
    Mode* mergePred = &md.pred[PRED_MERGE];
    Mode* skipPred = &md.pred[PRED_SKIP];
    mergePred->cu.initEstData();
    skipPred->cu.initEstData();
    TComDataCU* mergeCU = &md.pred[PRED_MERGE].cu;

    X265_CHECK(mergeCU->m_slice->m_sliceType != I_SLICE, "Evaluating merge in I slice\n");

    TComMvField mvFieldNeighbours[MRG_MAX_NUM_CANDS][2]; // double length for mv of both lists
    uint8_t interDirNeighbours[MRG_MAX_NUM_CANDS];
    uint32_t maxNumMergeCand = mergeCU->m_slice->m_maxNumMergeCand;

    mergeCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to CTU level
    mergeCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    mergeCU->getInterMergeCandidates(0, 0, mvFieldNeighbours, interDirNeighbours, maxNumMergeCand);

    int mergeCandBuffer[MRG_MAX_NUM_CANDS];
    for (uint32_t i = 0; i < maxNumMergeCand; ++i)
        mergeCandBuffer[i] = 0;

    bool bestIsSkip = false;
    TComYuv *fencYuv = &md.origYuv;
    uint32_t iterations = mergeCU->isLosslessCoded(0) ? 1 : 2;

    for (uint32_t noResidual = 0; noResidual < iterations; ++noResidual)
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
                    mergeCU->setPredModeSubParts(MODE_INTER, 0, depth); // interprets depth relative to CTU level
                    mergeCU->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
                    mergeCU->setPartSizeSubParts(SIZE_2Nx2N, 0, depth); // interprets depth relative to CTU level
                    mergeCU->setMergeFlag(0, true);
                    mergeCU->setMergeIndex(0, mergeCand);
                    mergeCU->setInterDirSubParts(interDirNeighbours[mergeCand], 0, 0, depth); // interprets depth relative to CTU level
                    mergeCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(mvFieldNeighbours[mergeCand][0], SIZE_2Nx2N, 0, 0); // interprets depth relative to outTempCU level
                    mergeCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(mvFieldNeighbours[mergeCand][1], SIZE_2Nx2N, 0, 0); // interprets depth relative to outTempCU level

                    // do MC
                    prepMotionCompensation(mergeCU, cuData, 0);
                    motionCompensation(&mergePred->predYuv, true, true);

                    // estimate residual and encode everything
                    if (noResidual)
                        encodeResAndCalcRdSkipCU(md.pred[PRED_MERGE], fencYuv);
                    else
                        encodeResAndCalcRdInterCU(md.pred[PRED_MERGE], cuData, fencYuv, &m_modeDepth[depth].tempResi);

                    m_rdContexts[depth].temp.store(mergePred->contexts);

                    /* TODO: Fix the satd cost estimates. Why is merge being chosen in high motion areas: estimated distortion is too low? */
                    if (!noResidual && !mergeCU->getQtRootCbf(0))
                        mergeCandBuffer[mergeCand] = 1;

                    mergeCU->setSkipFlagSubParts(!mergeCU->getQtRootCbf(0), 0, depth);
                    int origQP = mergeCU->getQP(0);
                    checkDQP(mergeCU, cuData);

                    if (mergeCU->m_totalRDCost < skipPred->cu.m_totalRDCost)
                    {
                        std::swap(mergePred, skipPred);
                        mergeCU = &mergePred->cu;
                    }

                    skipPred->cu.setQPSubParts(origQP, 0, depth);
                    skipPred->cu.setSkipFlagSubParts(false, 0, depth);
                    if (!bestIsSkip)
                        bestIsSkip = !skipPred->cu.getQtRootCbf(0);
                }
            }
        }

        md.bestMode = skipPred;
        TComDataCU* bestCU = &md.bestMode->cu;
        if (!noResidual && m_param->bEnableEarlySkip && !bestCU->getQtRootCbf(0))
        {
            if (bestCU->getMergeFlag(0))
                earlySkip = true;
            else
            {
                bool noMvd = true;
                for (uint32_t refListIdx = 0; refListIdx < 2; refListIdx++)
                    if (bestCU->m_slice->m_numRefIdx[refListIdx] > 0)
                        noMvd &= !bestCU->getCUMvField(refListIdx)->getMvd(0).word;
                if (noMvd)
                    earlySkip = true;
            }
        }
    }
}

void Analysis::parallelInterSearch(Mode& interMode, CU* cuData, bool bChroma)
{
    TComDataCU* cu = &interMode.cu;
    Slice *slice = cu->m_slice;
    TComPicYuv *fenc = slice->m_pic->getPicYuvOrg();
    PartSize partSize = cu->getPartitionSize(0);
    m_curMECu = cu;
    m_curCUData = cuData;

    MergeData merge;
    memset(&merge, 0, sizeof(merge));

    uint32_t lastMode = 0;
    for (int partIdx = 0; partIdx < 2; partIdx++)
    {
        uint32_t partAddr;
        int      puWidth, puHeight;
        cu->getPartIndexAndSize(partIdx, partAddr, puWidth, puHeight);

        getBlkBits(partSize, slice->isInterP(), partIdx, lastMode, m_listSelBits);
        prepMotionCompensation(cu, cuData, partIdx);

        pixel* pu = fenc->getLumaAddr(cu->m_cuAddr, cuData->encodeIdx + partAddr);
        m_me.setSourcePU(pu - fenc->getLumaAddr(), puWidth, puHeight);

        m_bestME[0].cost = MAX_UINT;
        m_bestME[1].cost = MAX_UINT;

        /* this worker might already be enqueued, so other threads might be looking at the ME job counts
         * at any time, do these sets in a safe order */
        m_curPart = partIdx;
        m_totalNumME = 0;
        m_numAcquiredME = 0;
        m_numCompletedME = 0;
        m_totalNumME = slice->m_numRefIdx[0] + slice->m_numRefIdx[1];

        if (!m_bJobsQueued)
            JobProvider::enqueue();

        for (int i = 0; i < m_totalNumME; i++)
            m_pool->pokeIdleThread();

        MotionData bidir[2];
        uint32_t mrgCost = MAX_UINT;
        uint32_t bidirCost = MAX_UINT;
        int bidirBits = 0;

        /* the master thread does merge estimation */
        if (partSize != SIZE_2Nx2N)
        {
            merge.absPartIdx = partAddr;
            merge.width = puWidth;
            merge.height = puHeight;
            mrgCost = mergeEstimation(cu, cuData, partIdx, merge);
        }

        /* Participate in unidir motion searches */
        while (m_totalNumME > m_numAcquiredME)
        {
            int id = ATOMIC_INC(&m_numAcquiredME);
            if (m_totalNumME >= id)
            {
                parallelME(-1, id - 1);
                if (ATOMIC_INC(&m_numCompletedME) == m_totalNumME)
                    m_meCompletionEvent.trigger();
            }
        }
        if (!m_bJobsQueued)
            JobProvider::dequeue();
        m_meCompletionEvent.wait();

        /* the master thread does bidir estimation */
        if (slice->isInterB() && !cu->isBipredRestriction() && m_bestME[0].cost != MAX_UINT && m_bestME[1].cost != MAX_UINT)
        {
            ALIGN_VAR_32(pixel, avg[MAX_CU_SIZE * MAX_CU_SIZE]);

            bidir[0] = m_bestME[0];
            bidir[1] = m_bestME[1];

            // Generate reference subpels
            TComPicYuv *refPic0 = slice->m_refPicList[0][m_bestME[0].ref]->getPicYuvRec();
            TComPicYuv *refPic1 = slice->m_refPicList[1][m_bestME[1].ref]->getPicYuvRec();
            
            prepMotionCompensation(cu, cuData, partIdx);
            predInterLumaBlk(refPic0, &m_bidirPredYuv[0], &m_bestME[0].mv);
            predInterLumaBlk(refPic1, &m_bidirPredYuv[1], &m_bestME[1].mv);

            pixel *pred0 = m_bidirPredYuv[0].getLumaAddr(partAddr);
            pixel *pred1 = m_bidirPredYuv[1].getLumaAddr(partAddr);

            int partEnum = partitionFromSizes(puWidth, puHeight);
            primitives.pixelavg_pp[partEnum](avg, puWidth, pred0, m_bidirPredYuv[0].getStride(), pred1, m_bidirPredYuv[1].getStride(), 32);
            int satdCost = m_me.bufSATD(avg, puWidth);

            bidirBits = m_bestME[0].bits + m_bestME[1].bits + m_listSelBits[2] - (m_listSelBits[0] + m_listSelBits[1]);
            bidirCost = satdCost + m_rdCost.getCost(bidirBits);

            MV mvzero(0, 0);
            bool bTryZero = m_bestME[0].mv.notZero() || m_bestME[1].mv.notZero();
            if (bTryZero)
            {
                /* Do not try zero MV if unidir motion predictors are beyond
                 * valid search area */
                MV mvmin, mvmax;
                int merange = X265_MAX(m_param->sourceWidth, m_param->sourceHeight);
                setSearchRange(cu, mvzero, merange, mvmin, mvmax);
                mvmax.y += 2; // there is some pad for subpel refine
                mvmin <<= 2;
                mvmax <<= 2;

                bTryZero &= m_bestME[0].mvp.checkRange(mvmin, mvmax);
                bTryZero &= m_bestME[1].mvp.checkRange(mvmin, mvmax);
            }
            if (bTryZero)
            {
                // coincident blocks of the two reference pictures
                pixel *ref0 = slice->m_mref[0][m_bestME[0].ref].fpelPlane + (pu - fenc->getLumaAddr());
                pixel *ref1 = slice->m_mref[1][m_bestME[1].ref].fpelPlane + (pu - fenc->getLumaAddr());
                intptr_t refStride = slice->m_mref[0][0].lumaStride;

                primitives.pixelavg_pp[partEnum](avg, puWidth, ref0, refStride, ref1, refStride, 32);
                satdCost = m_me.bufSATD(avg, puWidth);

                MV mvp0 = m_bestME[0].mvp;
                int mvpIdx0 = m_bestME[0].mvpIdx;
                uint32_t bits0 = m_bestME[0].bits - m_me.bitcost(m_bestME[0].mv, mvp0) + m_me.bitcost(mvzero, mvp0);

                MV mvp1 = m_bestME[1].mvp;
                int mvpIdx1 = m_bestME[1].mvpIdx;
                uint32_t bits1 = m_bestME[1].bits - m_me.bitcost(m_bestME[1].mv, mvp1) + m_me.bitcost(mvzero, mvp1);

                uint32_t cost = satdCost + m_rdCost.getCost(bits0) + m_rdCost.getCost(bits1);

                /* TODO: why do we do this? */
                // checkBestMVP(amvpCand[0][m_bestME[0].ref], mvzero, mvp0, mvpIdx0, bits0, cost);
                // checkBestMVP(amvpCand[1][m_bestME[1].ref], mvzero, mvp1, mvpIdx1, bits1, cost);

                if (cost < bidirCost)
                {
                    bidir[0].mv = mvzero;
                    bidir[1].mv = mvzero;
                    bidir[0].mvp = mvp0;
                    bidir[1].mvp = mvp1;
                    bidir[0].mvpIdx = mvpIdx0;
                    bidir[1].mvpIdx = mvpIdx1;
                    bidirCost = cost;
                    bidirBits = bits0 + bits1 + m_listSelBits[2] - (m_listSelBits[0] + m_listSelBits[1]);
                }
            }
        }

        /* select best option and store into CU */
        cu->getCUMvField(REF_PIC_LIST_0)->setAllMvField(TComMvField(), partSize, partAddr, 0, partIdx);
        cu->getCUMvField(REF_PIC_LIST_1)->setAllMvField(TComMvField(), partSize, partAddr, 0, partIdx);

        if (mrgCost < bidirCost && mrgCost < m_bestME[0].cost && mrgCost < m_bestME[1].cost)
        {
            cu->setMergeFlag(partAddr, true);
            cu->setMergeIndex(partAddr, merge.index);
            cu->setInterDirSubParts(merge.interDir, partAddr, partIdx, cu->getDepth(partAddr));
            cu->getCUMvField(REF_PIC_LIST_0)->setAllMvField(merge.mvField[0], partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_1)->setAllMvField(merge.mvField[1], partSize, partAddr, 0, partIdx);

            cu->m_totalBits += merge.bits;
        }
        else if (bidirCost < m_bestME[0].cost && bidirCost < m_bestME[1].cost)
        {
            lastMode = 2;

            cu->setMergeFlag(partAddr, false);
            cu->setInterDirSubParts(3, partAddr, partIdx, cu->getDepth(0));
            cu->getCUMvField(REF_PIC_LIST_0)->setAllMv(bidir[0].mv, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_0)->setAllRefIdx(m_bestME[0].ref, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_0)->setMvd(partAddr, bidir[0].mv - bidir[0].mvp);
            cu->setMVPIdx(REF_PIC_LIST_0, partAddr, bidir[0].mvpIdx);

            cu->getCUMvField(REF_PIC_LIST_1)->setAllMv(bidir[1].mv, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_1)->setAllRefIdx(m_bestME[1].ref, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_1)->setMvd(partAddr, bidir[1].mv - bidir[1].mvp);
            cu->setMVPIdx(REF_PIC_LIST_1, partAddr, bidir[1].mvpIdx);

            cu->m_totalBits += bidirBits;
        }
        else if (m_bestME[0].cost <= m_bestME[1].cost)
        {
            lastMode = 0;

            cu->setMergeFlag(partAddr, false);
            cu->setInterDirSubParts(1, partAddr, partIdx, cu->getDepth(0));
            cu->getCUMvField(REF_PIC_LIST_0)->setAllMv(m_bestME[0].mv, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_0)->setAllRefIdx(m_bestME[0].ref, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_0)->setMvd(partAddr, m_bestME[0].mv - m_bestME[0].mvp);
            cu->setMVPIdx(REF_PIC_LIST_0, partAddr, m_bestME[0].mvpIdx);

            cu->m_totalBits += m_bestME[0].bits;
        }
        else
        {
            lastMode = 1;

            cu->setMergeFlag(partAddr, false);
            cu->setInterDirSubParts(2, partAddr, partIdx, cu->getDepth(0));
            cu->getCUMvField(REF_PIC_LIST_1)->setAllMv(m_bestME[1].mv, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_1)->setAllRefIdx(m_bestME[1].ref, partSize, partAddr, 0, partIdx);
            cu->getCUMvField(REF_PIC_LIST_1)->setMvd(partAddr, m_bestME[1].mv - m_bestME[1].mvp);
            cu->setMVPIdx(REF_PIC_LIST_1, partAddr, m_bestME[1].mvpIdx);

            cu->m_totalBits += m_bestME[1].bits;
        }

        prepMotionCompensation(cu, cuData, partIdx);
        motionCompensation(&interMode.predYuv, true, bChroma);

        if (partSize == SIZE_2Nx2N)
            return;
    }
}

void Analysis::checkInter_rd0_4(Mode& interMode, CU* cuData, PartSize partSize)
{
    TComDataCU* cu = &interMode.cu;
    uint32_t depth = cu->getDepth(0);
    cu->setPartSizeSubParts(partSize, 0, depth);
    cu->setPredModeSubParts(MODE_INTER, 0, depth);
    cu->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    cu->m_totalBits = 0;

    TComYuv* fencYuv = &m_modeDepth[depth].origYuv;
    TComYuv* predYuv = &interMode.predYuv;

    if (m_param->bDistributeMotionEstimation && (cu->m_slice->m_numRefIdx[0] + cu->m_slice->m_numRefIdx[1]) > 2)
    {
        parallelInterSearch(interMode, cuData, false);
        x265_emms();
        int sizeIdx = cu->getLog2CUSize(0) - 2;
        cu->m_totalDistortion = primitives.sa8d[sizeIdx](fencYuv->getLumaAddr(), fencYuv->getStride(), predYuv->getLumaAddr(), predYuv->getStride());
        cu->m_sa8dCost = m_rdCost.calcRdSADCost(cu->m_totalDistortion, cu->m_totalBits);
    }
    else if (predInterSearch(interMode, cuData, false, false))
    {
        int sizeIdx = cu->getLog2CUSize(0) - 2;
        uint32_t distortion = primitives.sa8d[sizeIdx](fencYuv->getLumaAddr(), fencYuv->getStride(), predYuv->getLumaAddr(), predYuv->getStride());
        cu->m_totalDistortion = distortion;
        cu->m_sa8dCost = m_rdCost.calcRdSADCost(distortion, cu->m_totalBits);
    }
    else
    {
        cu->m_totalDistortion = MAX_UINT;
        cu->m_totalRDCost = MAX_INT64;
    }
}

void Analysis::checkInter_rd5_6(Mode& interMode, CU* cuData, PartSize partSize, bool bMergeOnly)
{
    TComDataCU* cu = &interMode.cu;
    uint32_t depth = cu->getDepth(0);

    cu->setSkipFlagSubParts(false, 0, depth);
    cu->setPartSizeSubParts(partSize, 0, depth);
    cu->setPredModeSubParts(MODE_INTER, 0, depth);
    cu->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);
    cu->initEstData();

    TComYuv* fencYuv = &m_modeDepth[depth].origYuv;

    if (m_param->bDistributeMotionEstimation && !bMergeOnly && (cu->m_slice->m_numRefIdx[0] + cu->m_slice->m_numRefIdx[1]) > 2)
    {
        parallelInterSearch(interMode, cuData, true);
        encodeResAndCalcRdInterCU(interMode, cuData, fencYuv, &m_modeDepth[depth].tempResi);

        m_rdContexts[depth].temp.store(interMode.contexts);
        checkDQP(cu, cuData);
        checkBestMode(interMode, depth);
    }
    else if (predInterSearch(interMode, cuData, bMergeOnly, true))
    {
        encodeResAndCalcRdInterCU(interMode, cuData, fencYuv, &m_modeDepth[depth].tempResi);
        m_rdContexts[depth].temp.store(interMode.contexts);
        checkDQP(cu, cuData);
        checkBestMode(interMode, depth);
    }
}

/* Note that this function does not save the best intra prediction, it must
 * be generated later. It records the best mode in the cu */
void Analysis::checkIntraInInter_rd0_4(Mode& intramode, CU* cuData)
{
    TComDataCU* cu = &intramode.cu;
    uint32_t depth = cu->getDepth(0);

    cu->setPartSizeSubParts(SIZE_2Nx2N, 0, depth);
    cu->setPredModeSubParts(MODE_INTRA, 0, depth);
    cu->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    uint32_t initTrDepth = 0;
    uint32_t log2TrSize  = cu->getLog2CUSize(0) - initTrDepth;
    uint32_t tuSize      = 1 << log2TrSize;
    const uint32_t partOffset  = 0;

    // Reference sample smoothing
    TComPattern::initAdiPattern(cu, cuData, partOffset, initTrDepth, m_predBuf, m_refAbove, m_refLeft, m_refAboveFlt, m_refLeftFlt, ALL_IDX);

    pixel* fenc     = m_modeDepth[depth].origYuv.getLumaAddr();
    uint32_t stride = m_modeDepth[depth].origYuv.getStride();

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
    int predsize = scaleTuSize * scaleTuSize;

    uint32_t preds[3];
    cu->getIntraDirLumaPredictor(partOffset, preds);

    uint64_t mpms;
    uint32_t rbits = getIntraRemModeBits(cu, partOffset, depth, preds, mpms);

    // DC
    primitives.intra_pred[DC_IDX][sizeIdx](tmp, scaleStride, left, above, 0, (scaleTuSize <= 16));
    bsad = sa8d(fenc, scaleStride, tmp, scaleStride) << costShift;
    bmode = mode = DC_IDX;
    bbits = (mpms & ((uint64_t)1 << mode)) ? getIntraModeBits(cu, mode, partOffset, depth) : rbits;
    bcost = m_rdCost.calcRdSADCost(bsad, bbits);

    pixel *abovePlanar = above;
    pixel *leftPlanar  = left;

    if (tuSize & (8 | 16 | 32))
    {
        abovePlanar = aboveFiltered;
        leftPlanar  = leftFiltered;
    }

    // PLANAR
    primitives.intra_pred[PLANAR_IDX][sizeIdx](tmp, scaleStride, leftPlanar, abovePlanar, 0, 0);
    sad = sa8d(fenc, scaleStride, tmp, scaleStride) << costShift;
    mode = PLANAR_IDX;
    bits = (mpms & ((uint64_t)1 << mode)) ? getIntraModeBits(cu, mode, partOffset, depth) : rbits;
    cost = m_rdCost.calcRdSADCost(sad, bits);
    COPY4_IF_LT(bcost, cost, bmode, mode, bsad, sad, bbits, bits);

    // Transpose NxN
    primitives.transpose[sizeIdx](buf_trans, fenc, scaleStride);

    primitives.intra_pred_allangs[sizeIdx](tmp, above, left, aboveFiltered, leftFiltered, (scaleTuSize <= 16));

    bool modeHor;
    pixel *cmp;
    intptr_t srcStride;

#define TRY_ANGLE(angle) \
    modeHor = angle < 18; \
    cmp = modeHor ? buf_trans : fenc; \
    srcStride = modeHor ? scaleTuSize : scaleStride; \
    sad = sa8d(cmp, srcStride, &tmp[(angle - 2) * predsize], scaleTuSize) << costShift; \
    bits = (mpms & ((uint64_t)1 << angle)) ? getIntraModeBits(cu, angle, partOffset, depth) : rbits; \
    cost = m_rdCost.calcRdSADCost(sad, bits)

    if (m_param->bEnableFastIntra)
    {
        int asad = 0;
        uint32_t lowmode, highmode, amode = 5, abits = 0;
        uint64_t acost = MAX_INT64;

        /* pick the best angle, sampling at distance of 5 */
        for (mode = 5; mode < 35; mode += 5)
        {
            TRY_ANGLE(mode);
            COPY4_IF_LT(acost, cost, amode, mode, asad, sad, abits, bits);
        }

        /* refine best angle at distance 2, then distance 1 */
        for (uint32_t dist = 2; dist >= 1; dist--)
        {
            lowmode = amode - dist;
            highmode = amode + dist;

            X265_CHECK(lowmode >= 2 && lowmode <= 34, "low intra mode out of range\n");
            TRY_ANGLE(lowmode);
            COPY4_IF_LT(acost, cost, amode, lowmode, asad, sad, abits, bits);

            X265_CHECK(highmode >= 2 && highmode <= 34, "high intra mode out of range\n");
            TRY_ANGLE(highmode);
            COPY4_IF_LT(acost, cost, amode, highmode, asad, sad, abits, bits);
        }

        if (amode == 33)
        {
            TRY_ANGLE(34);
            COPY4_IF_LT(acost, cost, amode, 34, asad, sad, abits, bits);
        }

        COPY4_IF_LT(bcost, acost, bmode, amode, bsad, asad, bbits, abits);
    }
    else // calculate and search all intra prediction angles for lowest cost
    {
        for (mode = 2; mode < 35; mode++)
        {
            TRY_ANGLE(mode);
            COPY4_IF_LT(bcost, cost, bmode, mode, bsad, sad, bbits, bits);
        }
    }

    cu->setLumaIntraDirSubParts(bmode, partOffset, depth + initTrDepth);
    cu->m_totalBits = bbits;
    cu->m_totalDistortion = bsad;
    cu->m_sa8dCost = bcost;
}

void Analysis::checkIntraInInter_rd5_6(Mode &intraMode, CU* cuData, PartSize partSize)
{
    TComDataCU* cu = &intraMode.cu;
    uint32_t depth = cu->getDepth(0);

    PPAScopeEvent(CheckRDCostIntra + depth);

    m_quant.setQPforQuant(cu);
    cu->initEstData();
    cu->setSkipFlagSubParts(false, 0, depth);
    cu->setPartSizeSubParts(partSize, 0, depth);
    cu->setPredModeSubParts(MODE_INTRA, 0, depth);
    cu->setCUTransquantBypassSubParts(!!m_param->bLossless, 0, depth);

    uint32_t tuDepthRange[2];
    cu->getQuadtreeTULog2MinSizeInCU(tuDepthRange, 0);

    TComYuv* fencYuv = &m_modeDepth[depth].origYuv;

    estIntraPredQT(intraMode, cuData, fencYuv, tuDepthRange);
    estIntraPredChromaQT(intraMode, cuData, fencYuv);

    m_entropyCoder.resetBits();
    if (cu->m_slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder.codeCUTransquantBypassFlag(cu->getCUTransquantBypass(0));

    if (!cu->m_slice->isIntra())
    {
        m_entropyCoder.codeSkipFlag(cu, 0);
        m_entropyCoder.codePredMode(cu->getPredictionMode(0));
    }
    m_entropyCoder.codePartSize(cu, 0, depth);
    m_entropyCoder.codePredInfo(cu, 0);
    cu->m_mvBits = m_entropyCoder.getNumberOfWrittenBits();

    // Encode Coefficients
    bool bCodeDQP = m_bEncodeDQP;
    m_entropyCoder.codeCoeff(cu, 0, depth, bCodeDQP, tuDepthRange);
    m_entropyCoder.store(intraMode.contexts);
    cu->m_totalBits = m_entropyCoder.getNumberOfWrittenBits();
    cu->m_coeffBits = cu->m_totalBits - cu->m_mvBits;

    if (m_rdCost.m_psyRd)
    {
        int part = cu->getLog2CUSize(0) - 2;
        TComYuv* reconYuv = &intraMode.reconYuv;
        cu->m_psyEnergy = m_rdCost.psyCost(part, fencYuv->getLumaAddr(), fencYuv->getStride(), reconYuv->getLumaAddr(), reconYuv->getStride());
        cu->m_totalRDCost = m_rdCost.calcPsyRdCost(cu->m_totalDistortion, cu->m_totalBits, cu->m_psyEnergy);
    }
    else
        cu->m_totalRDCost = m_rdCost.calcRdCost(cu->m_totalDistortion, cu->m_totalBits);

    checkDQP(cu, cuData);
    checkBestMode(intraMode, depth);
}

void Analysis::encodeIntraInInter(Mode& intraMode, CU* cuData)
{
    TComDataCU* cu = &intraMode.cu;
    uint32_t depth = cu->getDepth(0);
    uint32_t initTrDepth = cu->getPartitionSize(0) == SIZE_2Nx2N ? 0 : 1;
    uint64_t puCost = 0;
    uint32_t puBits = 0;
    uint32_t psyEnergy = 0;

    // set context models
    m_entropyCoder.load(m_rdContexts[depth].cur);

    m_quant.setQPforQuant(cu);

    uint32_t tuDepthRange[2];
    cu->getQuadtreeTULog2MinSizeInCU(tuDepthRange, 0);

    ShortYuv* resiYuv = &intraMode.resiYuv;
    TComYuv*  reconYuv = &intraMode.reconYuv;
    TComYuv*  predYuv = &intraMode.predYuv;
    TComYuv*  fencYuv = &m_modeDepth[depth].origYuv;

    /* TODO: why is recon a second call? pass intraMode to this function */
    uint32_t puDistY = xRecurIntraCodingQT(cu, cuData, initTrDepth, 0, fencYuv, predYuv, resiYuv, false, puCost, puBits, psyEnergy, tuDepthRange);
    xSetIntraResultQT(cu, initTrDepth, 0, reconYuv);

    cu->copyToPic(cu->getDepth(0), 0, initTrDepth);

    // set distortion (rate and r-d costs are determined later)
    cu->m_totalDistortion = puDistY;
    /* TODO: these cost vars can be moved from TComDataCU to Mode */

    estIntraPredChromaQT(intraMode, cuData, fencYuv); /* TODO: combine with xRecurIntraCodingQT */

    m_entropyCoder.resetBits();
    if (cu->m_slice->m_pps->bTransquantBypassEnabled)
        m_entropyCoder.codeCUTransquantBypassFlag(cu->getCUTransquantBypass(0));

    if (!cu->m_slice->isIntra())
    {
        m_entropyCoder.codeSkipFlag(cu, 0);
        m_entropyCoder.codePredMode(cu->getPredictionMode(0));
    }
    m_entropyCoder.codePartSize(cu, 0, depth);
    m_entropyCoder.codePredInfo(cu, 0);
    cu->m_mvBits += m_entropyCoder.getNumberOfWrittenBits();

    // Encode Coefficients
    bool bCodeDQP = m_bEncodeDQP;
    m_entropyCoder.codeCoeff(cu, 0, depth, bCodeDQP, tuDepthRange);
    m_entropyCoder.store(intraMode.contexts);

    cu->m_totalBits = m_entropyCoder.getNumberOfWrittenBits();
    cu->m_coeffBits = cu->m_totalBits - cu->m_mvBits;
    if (m_rdCost.m_psyRd)
    {
        int part = cu->getLog2CUSize(0) - 2;
        cu->m_psyEnergy = m_rdCost.psyCost(part, fencYuv->getLumaAddr(), fencYuv->getStride(), reconYuv->getLumaAddr(), reconYuv->getStride());
        cu->m_totalRDCost = m_rdCost.calcPsyRdCost(cu->m_totalDistortion, cu->m_totalBits, cu->m_psyEnergy);
    }
    else
        cu->m_totalRDCost = m_rdCost.calcRdCost(cu->m_totalDistortion, cu->m_totalBits);
}

void Analysis::encodeResidue(TComDataCU*, CU*, uint32_t, uint32_t) {}

#if 0 /* FIXME */
void Analysis::encodeResidue(TComDataCU* ctu, CU* cuData, uint32_t absPartIdx, uint32_t depth)
{
    Frame* pic = ctu->m_pic;
    uint32_t cuAddr = ctu->m_cuAddr;

    if (depth < ctu->getDepth(absPartIdx) && depth < g_maxCUDepth)
    {
        uint32_t nextDepth = depth + 1;
        uint32_t qNumParts = (NUM_CU_PARTITIONS >> (depth << 1)) >> 2;
        for (uint32_t partUnitIdx = 0; partUnitIdx < 4; partUnitIdx++, absPartIdx += qNumParts)
        {
            CU *childCUData = ctu->m_cuLocalData + cuData->childIdx + partUnitIdx;
            if (childCUData->flags & CU::PRESENT)
                encodeResidue(ctu, childCUData, absPartIdx, nextDepth);
        }
        return;
    }

    TComDataCU* cu;
    if (depth)
    {
        // Any CU at this depth would work, I think
        cu = &m_modeDepth[depth].pred[PRED_2Nx2N].cu;
        cu->copyFromPic(ctu, cuData);
    }
    else
        cu = ctu;

    m_quant.setQPforQuant(cu);

    if (ctu->getPredictionMode(absPartIdx) == MODE_INTER)
    {
        int log2CUSize = cu->getLog2CUSize(0);
        if (!ctu->getSkipFlag(absPartIdx))
        {
            const int sizeIdx = log2CUSize - 2;
            // Calculate Residue
            pixel* src2 = m_bestPredYuv[0]->getLumaAddr(absPartIdx);
            pixel* src1 = m_origYuv[0]->getLumaAddr(absPartIdx);
            int16_t* dst = m_tmpResiYuv[depth]->getLumaAddr();
            uint32_t src2stride = m_bestPredYuv[0]->getStride();
            uint32_t src1stride = m_origYuv[0]->getStride();
            uint32_t dststride = m_tmpResiYuv[depth]->m_width;
            primitives.luma_sub_ps[sizeIdx](dst, dststride, src1, src2, src1stride, src2stride);

            src2 = m_bestPredYuv[0]->getCbAddr(absPartIdx);
            src1 = m_origYuv[0]->getCbAddr(absPartIdx);
            dst = m_tmpResiYuv[depth]->getCbAddr();
            src2stride = m_bestPredYuv[0]->getCStride();
            src1stride = m_origYuv[0]->getCStride();
            dststride = m_tmpResiYuv[depth]->m_cwidth;
            primitives.chroma[m_param->internalCsp].sub_ps[sizeIdx](dst, dststride, src1, src2, src1stride, src2stride);

            src2 = m_bestPredYuv[0]->getCrAddr(absPartIdx);
            src1 = m_origYuv[0]->getCrAddr(absPartIdx);
            dst = m_tmpResiYuv[depth]->getCrAddr();
            primitives.chroma[m_param->internalCsp].sub_ps[sizeIdx](dst, dststride, src1, src2, src1stride, src2stride);

            uint32_t tuDepthRange[2];
            cu->getQuadtreeTULog2MinSizeInCU(tuDepthRange, 0);
            // Residual encoding
            residualTransformQuantInter(cu, cuData, 0, m_origYuv[0], m_tmpResiYuv[depth], cu->getDepth(0), tuDepthRange);
            checkDQP(cu);

            if (ctu->getMergeFlag(absPartIdx) && cu->getPartitionSize(0) == SIZE_2Nx2N && !cu->getQtRootCbf(0))
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
                primitives.luma_add_ps[sizeIdx](reco, dststride, pred, res, src1stride, src2stride);

                pred = m_bestPredYuv[0]->getCbAddr(absPartIdx);
                res = m_tmpResiYuv[depth]->getCbAddr();
                reco = m_bestRecoYuv[depth]->getCbAddr();
                dststride = m_bestRecoYuv[depth]->getCStride();
                src1stride = m_bestPredYuv[0]->getCStride();
                src2stride = m_tmpResiYuv[depth]->m_cwidth;
                primitives.chroma[m_param->internalCsp].add_ps[sizeIdx](reco, dststride, pred, res, src1stride, src2stride);

                pred = m_bestPredYuv[0]->getCrAddr(absPartIdx);
                res = m_tmpResiYuv[depth]->getCrAddr();
                reco = m_bestRecoYuv[depth]->getCrAddr();
                primitives.chroma[m_param->internalCsp].add_ps[sizeIdx](reco, dststride, pred, res, src1stride, src2stride);
                m_bestRecoYuv[depth]->copyToPicYuv(pic->getPicYuvRec(), cuAddr, absPartIdx);
                return;
            }
        }

        // Generate Recon
        int part = partitionFromLog2Size(log2CUSize);
        TComPicYuv* rec = pic->getPicYuvRec();
        pixel* src = m_bestPredYuv[0]->getLumaAddr(absPartIdx);
        pixel* dst = rec->getLumaAddr(cuAddr, absPartIdx);
        uint32_t srcstride = m_bestPredYuv[0]->getStride();
        uint32_t dststride = rec->getStride();
        primitives.luma_copy_pp[part](dst, dststride, src, srcstride);

        src = m_bestPredYuv[0]->getCbAddr(absPartIdx);
        dst = rec->getCbAddr(cuAddr, absPartIdx);
        srcstride = m_bestPredYuv[0]->getCStride();
        dststride = rec->getCStride();
        primitives.chroma[m_param->internalCsp].copy_pp[part](dst, dststride, src, srcstride);

        src = m_bestPredYuv[0]->getCrAddr(absPartIdx);
        dst = rec->getCrAddr(cuAddr, absPartIdx);
        primitives.chroma[m_param->internalCsp].copy_pp[part](dst, dststride, src, srcstride);
    }
    else
    {
        m_origYuv[0]->copyPartToYuv(m_origYuv[depth], absPartIdx);
        generateCoeffRecon(cu, cuData, m_origYuv[depth], m_modePredYuv[PRED_INTRA][depth], m_tmpResiYuv[depth], m_tmpRecoYuv[depth]);
        checkDQP(cu);
        m_tmpRecoYuv[depth]->copyToPicYuv(pic->getPicYuvRec(), cuAddr, absPartIdx);
        cu->copyCodedToPic(depth);
    }
}
#endif

/* check whether current try is the best with identifying the depth of current try */
void Analysis::checkBestMode(Mode& mode, uint32_t depth)
{
    ModeDepth& md = m_modeDepth[depth];
    if (md.bestMode)
    {
        if (mode.cu.m_totalRDCost < md.bestMode->cu.m_totalRDCost)
            md.bestMode = &mode;
    }
    else
        md.bestMode = &mode;
}

void Analysis::deriveTestModeAMP(TComDataCU* bestCU, bool &bHor, bool &bVer, bool &bMergeOnly)
{
    bMergeOnly = bestCU->getLog2CUSize(0) == 6;

    if (bestCU->getPartitionSize(0) == SIZE_2NxN)
        bHor = true;
    else if (bestCU->getPartitionSize(0) == SIZE_Nx2N)
        bVer = true;
    else if (bestCU->getPartitionSize(0) == SIZE_2Nx2N && bestCU->getMergeFlag(0) == false && bestCU->isSkipped(0) == false)
    {
        bHor = true;
        bVer = true;
    }
}

void Analysis::checkDQP(TComDataCU* cu, CU *cuData)
{
    Slice* slice = cu->m_slice;

    if (slice->m_pps->bUseDQP && cuData->depth <= slice->m_pps->maxCuDQPDepth)
    {
        if (cu->getDepth(0) > cuData->depth) // detect splits
        {
            bool hasResidual = false;
            for (uint32_t blkIdx = 0; blkIdx < cu->m_numPartitions; blkIdx++)
            {
                if (cu->getCbf(blkIdx, TEXT_LUMA) || cu->getCbf(blkIdx, TEXT_CHROMA_U) || cu->getCbf(blkIdx, TEXT_CHROMA_V))
                {
                    hasResidual = true;
                    break;
                }
            }
            if (hasResidual)
            {
                bool foundNonZeroCbf = false;
                cu->setQPSubCUs(cu->getRefQP(0), cu, 0, cuData->depth, foundNonZeroCbf);
                X265_CHECK(foundNonZeroCbf, "expected to find non-zero CBF\n");
            }
            else
                cu->setQPSubParts(cu->getRefQP(0), 0, cuData->depth);
        }
        else
        {
            if (!cu->getCbf(0, TEXT_LUMA, 0) && !cu->getCbf(0, TEXT_CHROMA_U, 0) && !cu->getCbf(0, TEXT_CHROMA_V, 0))
                cu->setQPSubParts(cu->getRefQP(0), 0, cuData->depth);
        }
    }
}

/* Function for filling original YUV samples of a CU in lossless mode */
void Analysis::fillOrigYUVBuffer(TComDataCU* cu, TComYuv* fencYuv)
{
    /* TODO: is this extra copy really necessary? the source pixels will still
     * be available when getLumaOrigYuv() is used */

    uint32_t width  = 1 << cu->getLog2CUSize(0);
    uint32_t height = 1 << cu->getLog2CUSize(0);

    pixel* srcY = fencYuv->getLumaAddr();
    pixel* dstY = cu->getLumaOrigYuv();
    uint32_t srcStride = fencYuv->getStride();

    /* TODO: square block copy primitive */
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
            dstY[x] = srcY[x];

        dstY += width;
        srcY += srcStride;
    }

    pixel* srcCb = fencYuv->getChromaAddr(1);
    pixel* srcCr = fencYuv->getChromaAddr(2);

    pixel* dstCb = cu->getChromaOrigYuv(1);
    pixel* dstCr = cu->getChromaOrigYuv(2);

    uint32_t srcStrideC = fencYuv->getCStride();
    uint32_t widthC  = width  >> cu->m_hChromaShift;
    uint32_t heightC = height >> cu->m_vChromaShift;

    /* TODO: block copy primitives */
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
