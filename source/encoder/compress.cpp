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
 * For more information, contact us at licensing@multicorewareinc.com.
 *****************************************************************************/

#include "TLibEncoder/TEncCu.h"
#include <math.h>

#if _MSC_VER
#pragma warning (disable: 4244)
#pragma warning (disable: 4018)
#endif

using namespace x265;

Void TEncCu::xComputeCostInter(TComDataCU*& rpcTempCU, PartSize ePartSize, UInt Index, Bool bUseMRG)
{
    UChar uhDepth = rpcTempCU->getDepth(0);

    rpcTempCU->setDepthSubParts(uhDepth, 0);

    rpcTempCU->setSkipFlagSubParts(false, 0, uhDepth);

    rpcTempCU->setPartSizeSubParts(ePartSize,  0, uhDepth);
    rpcTempCU->setPredModeSubParts(MODE_INTER, 0, uhDepth);
    rpcTempCU->setCUTransquantBypassSubParts(m_pcEncCfg->getCUTransquantBypassFlagValue(),      0, uhDepth);

    rpcTempCU->setMergeAMP(true);
    m_ppcRecoYuvTemp[uhDepth]->clear();
    m_ppcResiYuvTemp[uhDepth]->clear();

    m_pcPredSearch->predInterSearch(rpcTempCU, m_ppcOrigYuv[uhDepth], m_ppcPredYuvMode[Index][uhDepth], bUseMRG);  
    
}

Void TEncCu::xCompressInterCU(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, TComDataCU*& pcCU, UInt uiDepth, UInt PartitionIndex)
{
    m_abortFlag = false;
    TComPic* pcPic = rpcBestCU->getPic();
    
    // get Original YUV data from picture
    m_ppcOrigYuv[uiDepth]->copyFromPicYuv(pcPic->getPicYuvOrg(), rpcBestCU->getAddr(), rpcBestCU->getZorderIdxInCU());

    // variables for fast encoder decision
    Bool    bTrySplit    = true;

    // variable for Early CU determination
    Bool    bSubBranch = true;

    // variable for Cbf fast mode PU decision
    Bool earlyDetectionSkipMode = false;

    Bool bTrySplitDQP  = true;

    Bool bBoundary = false;
    UInt uiLPelX   = rpcBestCU->getCUPelX();
    UInt uiRPelX   = uiLPelX + rpcBestCU->getWidth(0)  - 1;
    UInt uiTPelY   = rpcBestCU->getCUPelY();
    UInt uiBPelY   = uiTPelY + rpcBestCU->getHeight(0) - 1;

    Int iQP = m_pcEncCfg->getUseRateCtrl() ? m_pcRateCtrl->getRCQP() : rpcTempCU->getQP(0);

    // If slice start or slice end is within this cu...
    TComSlice * pcSlice = rpcTempCU->getPic()->getSlice(rpcTempCU->getPic()->getCurrSliceIdx());
    Bool bSliceEnd = (pcSlice->getSliceCurEndCUAddr() > rpcTempCU->getSCUAddr() && pcSlice->getSliceCurEndCUAddr() < rpcTempCU->getSCUAddr() + rpcTempCU->getTotalNumPart());
    Bool bInsidePicture = (uiRPelX < rpcBestCU->getSlice()->getSPS()->getPicWidthInLumaSamples()) && (uiBPelY < rpcBestCU->getSlice()->getSPS()->getPicHeightInLumaSamples());
    // We need to split, so don't try these modes.
    TComYuv* YuvTemp;
    if (!bSliceEnd && bInsidePicture)
    {
        // variables for fast encoder decision
        bTrySplit    = true;
        
        if(uiDepth==0)
        {
            m_InterCU_2Nx2N[uiDepth]->initCU(pcCU->getPic(), pcCU->getAddr());
            m_InterCU_Nx2N[uiDepth]->initCU(pcCU->getPic(), pcCU->getAddr());
            m_InterCU_2NxN[uiDepth]->initCU(pcCU->getPic(), pcCU->getAddr());
            m_IntrainInterCU[uiDepth]->initCU(pcCU->getPic(), pcCU->getAddr());
            m_MergeCU[uiDepth]->initCU(pcCU->getPic(), pcCU->getAddr());
        }
        else
        {
            m_InterCU_2Nx2N[uiDepth]->initSubCU(pcCU, PartitionIndex, uiDepth, iQP); 
            m_InterCU_2NxN[uiDepth]->initSubCU(pcCU, PartitionIndex, uiDepth, iQP); 
            m_InterCU_Nx2N[uiDepth]->initSubCU(pcCU, PartitionIndex, uiDepth, iQP); 
            m_IntrainInterCU[uiDepth]->initSubCU(pcCU, PartitionIndex, uiDepth, iQP); 
            m_MergeCU[uiDepth]->initSubCU(pcCU, PartitionIndex, uiDepth, iQP); 
        }

        xComputeCostInter(m_InterCU_2Nx2N[uiDepth], SIZE_2Nx2N, 0);
        
        bTrySplitDQP = bTrySplit;

        if (uiDepth <= m_addSADDepth)
        {
            m_LCUPredictionSAD += m_temporalSAD;
            m_addSADDepth = uiDepth;
        }

        if (!earlyDetectionSkipMode)
        {
            if (m_pcEncCfg->getUseRectInter())
            {
                xComputeCostInter(m_InterCU_Nx2N[uiDepth], SIZE_Nx2N, 1);                                
                xComputeCostInter(m_InterCU_2NxN[uiDepth], SIZE_2NxN, 2);
            }      
        }

        // Early CU determination
        if (rpcBestCU->isSkipped(0))
        {
            bSubBranch = false;
        }
        else
        {
            bSubBranch = false;
        }
        
        rpcBestCU = m_InterCU_2Nx2N[uiDepth];
        
        YuvTemp = m_ppcPredYuvMode[0][uiDepth];
        m_ppcPredYuvMode[0][uiDepth] = m_ppcPredYuvBest[uiDepth];
        m_ppcPredYuvBest[uiDepth] = YuvTemp;

        if(m_InterCU_Nx2N[uiDepth]->getTotalCost() < rpcBestCU->getTotalCost())
        {
            rpcBestCU = m_InterCU_Nx2N[uiDepth];
            
            YuvTemp = m_ppcPredYuvMode[1][uiDepth];
            m_ppcPredYuvMode[1][uiDepth] = m_ppcPredYuvBest[uiDepth];
            m_ppcPredYuvBest[uiDepth] = YuvTemp;
        }
        if(m_InterCU_2NxN[uiDepth]->getTotalCost() < rpcBestCU->getTotalCost())
        {
            rpcBestCU = m_InterCU_2NxN[uiDepth];
            
            YuvTemp = m_ppcPredYuvMode[2][uiDepth];
            m_ppcPredYuvMode[2][uiDepth] = m_ppcPredYuvBest[uiDepth];
            m_ppcPredYuvBest[uiDepth] = YuvTemp;
        }
        m_pcPredSearch->encodeResAndCalcRdInterCU(rpcBestCU, m_ppcOrigYuv[uiDepth], m_ppcPredYuvBest[uiDepth], m_ppcResiYuvTemp[uiDepth], m_ppcResiYuvBest[uiDepth], m_ppcRecoYuvBest[uiDepth], false);
    }
    else if (!(bSliceEnd && bInsidePicture))
    {
        bBoundary = true;
        m_addSADDepth++;
    }

    rpcTempCU->initEstData(uiDepth, iQP);
    // further split

    if (bSubBranch && bTrySplitDQP && uiDepth < g_uiMaxCUDepth - g_uiAddCUDepth)
    {
        UChar       uhNextDepth         = uiDepth + 1;
        TComDataCU* pcSubBestPartCU     = m_ppcBestCU[uhNextDepth];
        TComDataCU* pcSubTempPartCU     = m_ppcTempCU[uhNextDepth];
        UInt uiPartUnitIdx = 0;
        rpcTempCU->getTotalCost() = 0;
        for (; uiPartUnitIdx < 4; uiPartUnitIdx++)
        {
            pcSubBestPartCU->initSubCU(rpcTempCU, uiPartUnitIdx, uhNextDepth, iQP);     // clear sub partition datas or init.
            pcSubTempPartCU->initSubCU(rpcTempCU, uiPartUnitIdx, uhNextDepth, iQP);     // clear sub partition datas or init.

            Bool bInSlice = pcSubBestPartCU->getSCUAddr() < pcSlice->getSliceCurEndCUAddr();
            if (bInSlice && (pcSubBestPartCU->getCUPelX() < pcSlice->getSPS()->getPicWidthInLumaSamples()) && (pcSubBestPartCU->getCUPelY() < pcSlice->getSPS()->getPicHeightInLumaSamples()))
            {
                if (0 == uiPartUnitIdx) //initialize RD with previous depth buffer
                {
                    m_pppcRDSbacCoder[uhNextDepth][CI_CURR_BEST]->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);
                }
                else
                {
                    m_pppcRDSbacCoder[uhNextDepth][CI_CURR_BEST]->load(m_pppcRDSbacCoder[uhNextDepth][CI_NEXT_BEST]);
                }

                // The following if condition has to be commented out in case the early Abort based on comparison of parentCu cost, childCU cost is not required.
                xCompressInterCU(pcSubBestPartCU, pcSubTempPartCU, rpcTempCU, uhNextDepth, uiPartUnitIdx);
                
                rpcTempCU->copyPartFrom(pcSubBestPartCU, uiPartUnitIdx, uhNextDepth); // Keep best part data to current temporary data.
                xCopyYuv2Tmp(pcSubBestPartCU->getTotalNumPart() * uiPartUnitIdx, uhNextDepth);                
            }
            else if (bInSlice)
            {
                pcSubBestPartCU->copyToPic(uhNextDepth);
                rpcTempCU->copyPartFrom(pcSubBestPartCU, uiPartUnitIdx, uhNextDepth);
            }
        }

        if (!bBoundary)
        {
            m_pcEntropyCoder->resetBits();
            m_pcEntropyCoder->encodeSplitFlag(rpcTempCU, 0, uiDepth, true);

            rpcTempCU->getTotalBits() += m_pcEntropyCoder->getNumberOfWrittenBits();     // split bits
            rpcTempCU->getTotalBins() += ((TEncBinCABAC*)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
        }
        else
            rpcTempCU->getTotalCost()  = CALCRDCOST(rpcTempCU->getTotalBits(), rpcTempCU->getTotalDistortion(), m_pcRdCost->m_dLambda);

        if ((g_uiMaxCUWidth >> uiDepth) == rpcTempCU->getSlice()->getPPS()->getMinCuDQPSize() && rpcTempCU->getSlice()->getPPS()->getUseDQP())
        {
            Bool hasResidual = false;
            for (UInt uiBlkIdx = 0; uiBlkIdx < rpcTempCU->getTotalNumPart(); uiBlkIdx++)
            {
                if (rpcTempCU->getCbf(uiBlkIdx, TEXT_LUMA) || rpcTempCU->getCbf(uiBlkIdx, TEXT_CHROMA_U) || rpcTempCU->getCbf(uiBlkIdx, TEXT_CHROMA_V))
                {
                    hasResidual = true;
                    break;
                }
            }

            UInt uiTargetPartIdx;
            uiTargetPartIdx = 0;
            if (hasResidual)
            {
                Bool foundNonZeroCbf = false;
                rpcTempCU->setQPSubCUs(rpcTempCU->getRefQP(uiTargetPartIdx), rpcTempCU, 0, uiDepth, foundNonZeroCbf);
                assert(foundNonZeroCbf);
            }
            else
            {
                rpcTempCU->setQPSubParts(rpcTempCU->getRefQP(uiTargetPartIdx), 0, uiDepth);     // set QP to default QP
            }
        }

        m_pppcRDSbacCoder[uhNextDepth][CI_NEXT_BEST]->store(m_pppcRDSbacCoder[uiDepth][CI_TEMP_BEST]);
        if(rpcTempCU->getTotalCost() < rpcBestCU->getTotalCost())
        {
            TComDataCU* temp = rpcBestCU;
            rpcBestCU = rpcTempCU;
            rpcTempCU = temp;
            
            YuvTemp = m_ppcRecoYuvTemp[uiDepth];
            m_ppcRecoYuvTemp[uiDepth] = m_ppcRecoYuvBest[uiDepth];
            m_ppcRecoYuvBest[uiDepth] = YuvTemp;
            m_pppcRDSbacCoder[uiDepth][CI_TEMP_BEST]->store(m_pppcRDSbacCoder[uiDepth][CI_NEXT_BEST]);
        }        
    }

    rpcBestCU->copyToPic(uiDepth);                                                   // Copy Best data to Picture for next partition prediction.
    xCopyYuv2Pic(rpcBestCU->getPic(), rpcBestCU->getAddr(), rpcBestCU->getZorderIdxInCU(), uiDepth, uiDepth, rpcBestCU, uiLPelX, uiTPelY);   // Copy Yuv data to picture Yuv

    if (bBoundary || (bSliceEnd && bInsidePicture))
    {
        return;
    }

    // Assert if Best prediction mode is NONE
    // Selected mode's RD-cost must be not MAX_DOUBLE.
    assert(rpcBestCU->getPartitionSize(0) != SIZE_NONE);
    assert(rpcBestCU->getPredictionMode(0) != MODE_NONE);
    assert(rpcBestCU->getTotalCost() != MAX_DOUBLE);    
}

#if _MSC_VER
#pragma warning (default: 4244)
#pragma warning (disable: 4018)
#endif