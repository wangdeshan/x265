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

/** \file     TEncGOP.cpp
    \brief    GOP encoder class
*/

#include <list>
#include <algorithm>
#include <functional>

#include "TEncTop.h"
#include "TEncGOP.h"
#include "TEncAnalyze.h"
#include "TLibCommon/SEI.h"
#include "TLibCommon/NAL.h"
#include "PPA/ppa.h"
#include "NALwrite.h"
#include <time.h>
#include <math.h>

/**
 * Produce an ascii(hex) representation of picture digest.
 *
 * Returns: a statically allocated null-terminated string.  DO NOT FREE.
 */
inline const char*digestToString(const unsigned char digest[3][16], int numChar)
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

static inline Int getLSB(Int poc, Int maxLSB)
{
    if (poc >= 0)
    {
        return poc % maxLSB;
    }
    else
    {
        return (maxLSB - ((-poc) % maxLSB)) % maxLSB;
    }
}

using namespace std;

//! \ingroup TLibEncoder
//! \{

// ====================================================================================================================
// Constructor / destructor / initialization / destroy
// ====================================================================================================================
TEncGOP::TEncGOP()
{
    m_pcCfg               = NULL;
    m_cFrameEncoders      = NULL;
    m_pcEncTop            = NULL;
    m_pcRateCtrl          = NULL;
    m_iLastIDR            = 0;
    m_totalCoded          = 0;
    m_bRefreshPending     = 0;
    m_pocCRA              = 0;
    m_numLongTermRefPicSPS = 0;
    m_cpbRemovalDelay     = 0;
    m_lastBPSEI           = 0;
    ::memset(m_ltRefPicPocLsbSps, 0, sizeof(m_ltRefPicPocLsbSps));
    ::memset(m_ltRefPicUsedByCurrPicFlag, 0, sizeof(m_ltRefPicUsedByCurrPicFlag));
}

TEncGOP::~TEncGOP()
{
}

/** Create list to contain pointers to LCU start addresses of slice.
 */
Void  TEncGOP::create()
{
}

Void  TEncGOP::destroy()
{
    TComList<TComPic*>::iterator iterPic = m_cListPic.begin();
    Int iSize = Int(m_cListPic.size());
    for (Int i = 0; i < iSize; i++)
    {
        TComPic* pcPic = *(iterPic++);

        pcPic->destroy();
        delete pcPic;
        pcPic = NULL;
    }

    if (m_cFrameEncoders)
    {
        m_cFrameEncoders->destroy();
        delete m_cFrameEncoders;
    }

    if (m_recon)
        delete [] m_recon;
}

Void TEncGOP::init(TEncTop* pcTEncTop)
{
    m_pcEncTop             = pcTEncTop;
    m_pcCfg                = pcTEncTop;
    m_pcRateCtrl           = pcTEncTop->getRateCtrl();

    // initialize SPS
    pcTEncTop->xInitSPS(&m_cSPS);

    // initialize PPS
    m_cPPS.setSPS(&m_cSPS);

    pcTEncTop->xInitPPS(&m_cPPS);
    pcTEncTop->xInitRPS(&m_cSPS);

    int numRows = (m_pcCfg->getSourceHeight() + m_cSPS.getMaxCUHeight() - 1) / m_cSPS.getMaxCUHeight();
    m_cFrameEncoders = new x265::EncodeFrame(pcTEncTop->getThreadPool());
    m_cFrameEncoders->init(pcTEncTop, numRows);

    int maxGOP = m_pcCfg->getIntraPeriod() > 1 ? m_pcCfg->getIntraPeriod() : 1;
    m_recon = new x265_picture_t[maxGOP];

    // make references to the last N TComPic's recon frames
    for (int i = 0; i < maxGOP; i++)
    {
        TComPic *pcPic;
        if (m_pcCfg->getUseAdaptiveQP())
        {
            TEncPic* pcEPic = new TEncPic;
            pcEPic->create(m_pcCfg->getSourceWidth(), m_pcCfg->getSourceHeight(), g_uiMaxCUWidth, g_uiMaxCUHeight, g_uiMaxCUDepth,
                           m_cPPS.getMaxCuDQPDepth() + 1, m_pcCfg->getConformanceWindow(), m_pcCfg->getDefaultDisplayWindow());
            pcPic = pcEPic;
        }
        else
        {
            pcPic = new TComPic;
            pcPic->create(m_pcCfg->getSourceWidth(), m_pcCfg->getSourceHeight(), g_uiMaxCUWidth, g_uiMaxCUHeight, g_uiMaxCUDepth,
                          m_pcCfg->getConformanceWindow(), m_pcCfg->getDefaultDisplayWindow());
        }
        if (m_pcCfg->getUseSAO())
        {
            pcPic->getPicSym()->allocSaoParam(m_cFrameEncoders->getSAO());
        }
        m_cListPic.pushBack(pcPic);
    }
}

x265_picture_t *TEncGOP::getReconPictures(UInt POC, UInt count)
{
    for (UInt i = 0; i < count; i++)
    {
        TComList<TComPic*>::iterator iterPic = m_cListPic.begin();
        while (iterPic != m_cListPic.end() && (*iterPic)->getPOC() != POC)
            iterPic++;
        POC++;
        TComPicYuv *recpic = (*iterPic)->getPicYuvRec();
        x265_picture_t& recon = m_recon[i];
        recon.planes[0] = recpic->getLumaAddr();
        recon.stride[0] = recpic->getStride();
        recon.planes[1] = recpic->getCbAddr();
        recon.stride[1] = recpic->getCStride();
        recon.planes[2] = recpic->getCrAddr();
        recon.stride[2] = recpic->getCStride();
        recon.bitDepth = sizeof(Pel) * 8;
    }
    return m_recon;
}

TComPic* TEncGOP::xGetNewPicBuffer()
{
    TComSlice::sortPicList(m_cListPic);
    TComPic *pcPic = NULL;

    TComList<TComPic*>::iterator iterPic  = m_cListPic.begin();
    Int iSize = Int(m_cListPic.size());
    for (Int i = 0; i < iSize; i++)
    {
        pcPic = *(iterPic++);
        if (pcPic->getSlice()->isReferenced() == false)
        {
            pcPic->getPicYuvRec()->setBorderExtension(false);
            return pcPic;
        }
    }
    return NULL;
}

SEIActiveParameterSets* TEncGOP::xCreateSEIActiveParameterSets(TComSPS *sps)
{
    SEIActiveParameterSets *seiActiveParameterSets = new SEIActiveParameterSets();

    seiActiveParameterSets->activeVPSId = m_pcCfg->getVPS()->getVPSId();
    seiActiveParameterSets->m_fullRandomAccessFlag = false;
    seiActiveParameterSets->m_noParamSetUpdateFlag = false;
    seiActiveParameterSets->numSpsIdsMinus1 = 0;
    seiActiveParameterSets->activeSeqParamSetId.resize(seiActiveParameterSets->numSpsIdsMinus1 + 1);
    seiActiveParameterSets->activeSeqParamSetId[0] = sps->getSPSId();
    return seiActiveParameterSets;
}

SEIDisplayOrientation* TEncGOP::xCreateSEIDisplayOrientation()
{
    SEIDisplayOrientation *seiDisplayOrientation = new SEIDisplayOrientation();

    seiDisplayOrientation->cancelFlag = false;
    seiDisplayOrientation->horFlip = false;
    seiDisplayOrientation->verFlip = false;
    seiDisplayOrientation->anticlockwiseRotation = m_pcCfg->getDisplayOrientationSEIAngle();
    return seiDisplayOrientation;
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================
Void TEncGOP::compressGOP(Int iPOCLast, Int iNumPicRcvd, std::list<AccessUnit>& accessUnitsInGOP)
{
    PPAScopeEvent(TEncGOP_compressGOP);

    AccessUnit::iterator  itLocationToPushSliceHeaderNALU; // used to store location where NALU containing slice header is to be inserted
    Int                   picSptDpbOutputDuDelay = 0;
    UInt*                 accumBitsDU = NULL;
    UInt*                 accumNalsDU = NULL;
    UInt                  uiOneBitstreamPerSliceLength = 0;
    TComOutputBitstream*  pcBitstreamRedirect = new TComOutputBitstream;
    TComOutputBitstream*  pcSubstreamsOut = NULL;
    x265::EncodeFrame*    pcEncodeFrame  = &m_cFrameEncoders[0];
    TEncEntropy*          pcEntropyCoder = pcEncodeFrame->getEntropyEncoder(0);
    TEncSlice*            pcSliceEncoder = pcEncodeFrame->getSliceEncoder();
    TEncCavlc*            pcCavlcCoder   = pcEncodeFrame->getCavlcCoder();
    TEncSbac*             pcSbacCoder    = pcEncodeFrame->getSingletonSbac();
    TEncBinCABAC*         pcBinCABAC     = pcEncodeFrame->getBinCABAC();
    TComLoopFilter*       pcLoopFilter   = pcEncodeFrame->getLoopFilter();
    TComBitCounter*       pcBitCounter   = pcEncodeFrame->getBitCounter();
    TEncSampleAdaptiveOffset* pcSAO      = pcEncodeFrame->getSAO();
    Bool bActiveParameterSetSEIPresentInAU = false;
    Bool bBufferingPeriodSEIPresentInAU = false;
    Bool bPictureTimingSEIPresentInAU = false;
    Bool bNestedBufferingPeriodSEIPresentInAU = false;

    if (m_pcCfg->getUseRateCtrl())
    {
        m_pcRateCtrl->initRCGOP(iNumPicRcvd);
    }

    Int gopSize = (iPOCLast == 0) ? 1 : m_pcCfg->getGOPSize();
    Int iNumPicCoded = 0;
    Bool writeSOP = m_pcCfg->getSOPDescriptionSEIEnabled();

    SEIPictureTiming pictureTimingSEI;

    // Initialize Scalable Nesting SEI with single layer values
    SEIScalableNesting scalableNestingSEI;
    scalableNestingSEI.m_bitStreamSubsetFlag           = 1;     // If the nested SEI messages are picture buffering SEI messages, picture timing SEI messages or sub-picture timing SEI messages, bitstream_subset_flag shall be equal to 1
    scalableNestingSEI.m_nestingOpFlag                 = 0;
    scalableNestingSEI.m_nestingNumOpsMinus1           = 0;     // nesting_num_ops_minus1
    scalableNestingSEI.m_allLayersFlag                 = 0;
    scalableNestingSEI.m_nestingNoOpMaxTemporalIdPlus1 = 6 + 1; // nesting_no_op_max_temporal_id_plus1
    scalableNestingSEI.m_nestingNumLayersMinus1        = 1 - 1; // nesting_num_layers_minus1
    scalableNestingSEI.m_nestingLayerId[0]             = 0;
    scalableNestingSEI.m_callerOwnsSEIs                = true;

    SEIDecodingUnitInfo decodingUnitInfoSEI;
    for (Int iGOPid = 0; iGOPid < gopSize; iGOPid++)
    {
        UInt uiColDir = 1;

        //select uiColDir
        Int iCloseLeft = 1, iCloseRight = -1;
        for (Int i = 0; i < m_pcCfg->getGOPEntry(iGOPid).m_numRefPics; i++)
        {
            Int iRef = m_pcCfg->getGOPEntry(iGOPid).m_referencePics[i];
            if (iRef > 0 && (iRef < iCloseRight || iCloseRight == -1))
            {
                iCloseRight = iRef;
            }
            else if (iRef < 0 && (iRef > iCloseLeft || iCloseLeft == 1))
            {
                iCloseLeft = iRef;
            }
        }

        if (iCloseRight > -1)
        {
            iCloseRight = iCloseRight + m_pcCfg->getGOPEntry(iGOPid).m_POC - 1;
        }
        if (iCloseLeft < 1)
        {
            iCloseLeft = iCloseLeft + m_pcCfg->getGOPEntry(iGOPid).m_POC - 1;
            while (iCloseLeft < 0)
            {
                iCloseLeft += gopSize;
            }
        }
        Int iLeftQP = 0, iRightQP = 0;
        for (Int i = 0; i < gopSize; i++)
        {
            if (m_pcCfg->getGOPEntry(i).m_POC == (iCloseLeft % gopSize) + 1)
            {
                iLeftQP = m_pcCfg->getGOPEntry(i).m_QPOffset;
            }
            if (m_pcCfg->getGOPEntry(i).m_POC == (iCloseRight % gopSize) + 1)
            {
                iRightQP = m_pcCfg->getGOPEntry(i).m_QPOffset;
            }
        }

        if (iCloseRight > -1 && iRightQP < iLeftQP)
        {
            uiColDir = 0;
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////// Initial to start encoding
        Int pocCurr = iPOCLast - iNumPicRcvd + m_pcCfg->getGOPEntry(iGOPid).m_POC;
        if (iPOCLast == 0)
        {
            pocCurr = 0;
        }
        if (pocCurr >= m_pcCfg->getFramesToBeEncoded())
        {
            continue;
        }

        if (getNalUnitType(pocCurr, m_iLastIDR) == NAL_UNIT_CODED_SLICE_IDR_W_RADL || getNalUnitType(pocCurr, m_iLastIDR) == NAL_UNIT_CODED_SLICE_IDR_N_LP)
        {
            m_iLastIDR = pocCurr;
        }
        // start a new access unit: create an entry in the list of output access units
        accessUnitsInGOP.push_back(AccessUnit());
        AccessUnit& accessUnit = accessUnitsInGOP.back();

        TComPic*              pcPic = NULL;
        TComSlice*            pcSlice;
        {
            // Locate input picture with the correct POC (makes no assumption on
            // input picture ordering)
            TComList<TComPic*>::iterator iterPic = m_cListPic.begin();
            while (iterPic != m_cListPic.end())
            {
                pcPic = *(iterPic++);
                if (pcPic->getPOC() == pocCurr)
                {
                    break;
                }
            }
        }
        if (!pcPic || pcPic->getPOC() != pocCurr)
        {
            printf("error: Encode frame POC not found in input list!\n");
            assert(0);
            return;
        }

        //  Slice data initialization
        pcSliceEncoder->setSliceIdx(0);
        pcSlice = pcSliceEncoder->initEncSlice(pcPic, pcEncodeFrame, gopSize <= 1, iPOCLast, pocCurr, iGOPid, getSPS(), getPPS());
        pcSlice->setLastIDR(m_iLastIDR);

        //set default slice level flag to the same as SPS level flag
        pcSlice->setScalingList(m_pcEncTop->getScalingList());
        pcSlice->getScalingList()->setUseTransformSkip(getPPS()->getUseTransformSkip());
        if (m_pcEncTop->getUseScalingListId() == SCALING_LIST_OFF)
        {
            pcEncodeFrame->setFlatScalingList();
            pcEncodeFrame->setUseScalingList(false);
            getSPS()->setScalingListPresentFlag(false);
            getPPS()->setScalingListPresentFlag(false);
        }
        else if (m_pcEncTop->getUseScalingListId() == SCALING_LIST_DEFAULT)
        {
            pcSlice->setDefaultScalingList();
            getSPS()->setScalingListPresentFlag(false);
            getPPS()->setScalingListPresentFlag(false);
            pcEncodeFrame->setScalingList(pcSlice->getScalingList());
            pcEncodeFrame->setUseScalingList(true);
        }
        else if (m_pcEncTop->getUseScalingListId() == SCALING_LIST_FILE_READ)
        {
            if (pcSlice->getScalingList()->xParseScalingList(m_pcCfg->getScalingListFile()))
            {
                pcSlice->setDefaultScalingList();
            }
            pcSlice->getScalingList()->checkDcOfMatrix();
            getSPS()->setScalingListPresentFlag(pcSlice->checkDefaultScalingList());
            getPPS()->setScalingListPresentFlag(false);
            pcEncodeFrame->setScalingList(pcSlice->getScalingList());
            pcEncodeFrame->setUseScalingList(true);
        }
        else
        {
            printf("error : ScalingList == %d no support\n", m_pcEncTop->getUseScalingListId());
            assert(0);
        }

        if (pcSlice->getSliceType() == B_SLICE && m_pcCfg->getGOPEntry(iGOPid).m_sliceType == 'P')
        {
            pcSlice->setSliceType(P_SLICE);
        }
        // Set the nal unit type
        pcSlice->setNalUnitType(getNalUnitType(pocCurr, m_iLastIDR));
        if (pcSlice->getTemporalLayerNonReferenceFlag())
        {
            if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_TRAIL_R)
            {
                pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_TRAIL_N);
            }
            if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RADL_R)
            {
                pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_RADL_N);
            }
            if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RASL_R)
            {
                pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_RASL_N);
            }
        }

        // Do decoding refresh marking if any
        pcSlice->decodingRefreshMarking(m_pocCRA, m_bRefreshPending, m_cListPic);
        selectReferencePictureSet(pcSlice, pocCurr, iGOPid);
        pcSlice->getRPS()->setNumberOfLongtermPictures(0);

        if ((pcSlice->checkThatAllRefPicsAreAvailable(m_cListPic, pcSlice->getRPS(), false) != 0) || (pcSlice->isIRAP()))
        {
            pcSlice->createExplicitReferencePictureSetFromReference(m_cListPic, pcSlice->getRPS(), pcSlice->isIRAP());
        }
        pcSlice->applyReferencePictureSet(m_cListPic, pcSlice->getRPS());

        if (pcSlice->getTLayer() > 0)
        {
            if (pcSlice->isTemporalLayerSwitchingPoint(m_cListPic) || pcSlice->getSPS()->getTemporalIdNestingFlag())
            {
                if (pcSlice->getTemporalLayerNonReferenceFlag())
                {
                    pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_TSA_N);
                }
                else
                {
                    pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_TLA_R);
                }
            }
            else if (pcSlice->isStepwiseTemporalLayerSwitchingPointCandidate(m_cListPic))
            {
                Bool isSTSA = true;
                for (Int ii = iGOPid + 1; (ii < m_pcCfg->getGOPSize() && isSTSA == true); ii++)
                {
                    Int lTid = m_pcCfg->getGOPEntry(ii).m_temporalId;
                    if (lTid == pcSlice->getTLayer())
                    {
                        TComReferencePictureSet* nRPS = pcSlice->getSPS()->getRPSList()->getReferencePictureSet(ii);
                        for (Int jj = 0; jj < nRPS->getNumberOfPictures(); jj++)
                        {
                            if (nRPS->getUsed(jj))
                            {
                                Int tPoc = m_pcCfg->getGOPEntry(ii).m_POC + nRPS->getDeltaPOC(jj);
                                Int kk = 0;
                                for (kk = 0; kk < m_pcCfg->getGOPSize(); kk++)
                                {
                                    if (m_pcCfg->getGOPEntry(kk).m_POC == tPoc)
                                        break;
                                }

                                Int tTid = m_pcCfg->getGOPEntry(kk).m_temporalId;
                                if (tTid >= pcSlice->getTLayer())
                                {
                                    isSTSA = false;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (isSTSA == true)
                {
                    if (pcSlice->getTemporalLayerNonReferenceFlag())
                    {
                        pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_STSA_N);
                    }
                    else
                    {
                        pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_STSA_R);
                    }
                }
            }
        }
        arrangeLongtermPicturesInRPS(pcSlice);
        TComRefPicListModification* refPicListModification = pcSlice->getRefPicListModification();
        refPicListModification->setRefPicListModificationFlagL0(false);
        refPicListModification->setRefPicListModificationFlagL1(false);
        pcSlice->setNumRefIdx(REF_PIC_LIST_0, min(m_pcCfg->getGOPEntry(iGOPid).m_numRefPicsActive, pcSlice->getRPS()->getNumberOfPictures()));
        pcSlice->setNumRefIdx(REF_PIC_LIST_1, min(m_pcCfg->getGOPEntry(iGOPid).m_numRefPicsActive, pcSlice->getRPS()->getNumberOfPictures()));

        //  Set reference list
        pcSlice->setRefPicList(m_cListPic);

        //  Slice info. refinement
        if ((pcSlice->getSliceType() == B_SLICE) && (pcSlice->getNumRefIdx(REF_PIC_LIST_1) == 0))
        {
            pcSlice->setSliceType(P_SLICE);
        }

        if (pcSlice->getSliceType() == B_SLICE)
        {
            pcSlice->setColFromL0Flag(1 - uiColDir);
            Bool bLowDelay = true;
            Int  iCurrPOC  = pcSlice->getPOC();
            Int iRefIdx = 0;

            for (iRefIdx = 0; iRefIdx < pcSlice->getNumRefIdx(REF_PIC_LIST_0) && bLowDelay; iRefIdx++)
            {
                if (pcSlice->getRefPic(REF_PIC_LIST_0, iRefIdx)->getPOC() > iCurrPOC)
                {
                    bLowDelay = false;
                }
            }

            for (iRefIdx = 0; iRefIdx < pcSlice->getNumRefIdx(REF_PIC_LIST_1) && bLowDelay; iRefIdx++)
            {
                if (pcSlice->getRefPic(REF_PIC_LIST_1, iRefIdx)->getPOC() > iCurrPOC)
                {
                    bLowDelay = false;
                }
            }

            pcSlice->setCheckLDC(bLowDelay);
        }
        else
        {
            pcSlice->setCheckLDC(true);
        }

        uiColDir = 1 - uiColDir;

        //-------------------------------------------------------------
        pcSlice->setRefPOCList();

        pcSlice->setList1IdxToList0Idx();

        if (m_pcEncTop->getTMVPModeId() == 2)
        {
            if (iGOPid == 0) // first picture in SOP (i.e. forward B)
            {
                pcSlice->setEnableTMVPFlag(0);
            }
            else
            {
                // Note: pcSlice->getColFromL0Flag() is assumed to be always 0 and getcolRefIdx() is always 0.
                pcSlice->setEnableTMVPFlag(1);
            }
            pcSlice->getSPS()->setTMVPFlagsPresent(1);
        }
        else if (m_pcEncTop->getTMVPModeId() == 1)
        {
            pcSlice->getSPS()->setTMVPFlagsPresent(1);
            pcSlice->setEnableTMVPFlag(1);
        }
        else
        {
            pcSlice->getSPS()->setTMVPFlagsPresent(0);
            pcSlice->setEnableTMVPFlag(0);
        }
        /////////////////////////////////////////////////////////////////////////////////////////////////// Compress a slice
        //  Slice compression
        if (m_pcCfg->getUseASR())
        {
            pcSliceEncoder->setSearchRange(pcSlice, pcEncodeFrame);
        }

        Bool bGPBcheck = false;
        if (pcSlice->getSliceType() == B_SLICE)
        {
            if (pcSlice->getNumRefIdx(RefPicList(0)) == pcSlice->getNumRefIdx(RefPicList(1)))
            {
                bGPBcheck = true;
                Int i;
                for (i = 0; i < pcSlice->getNumRefIdx(RefPicList(1)); i++)
                {
                    if (pcSlice->getRefPOC(RefPicList(1), i) != pcSlice->getRefPOC(RefPicList(0), i))
                    {
                        bGPBcheck = false;
                        break;
                    }
                }
            }
        }
        if (bGPBcheck)
        {
            pcSlice->setMvdL1ZeroFlag(true);
        }
        else
        {
            pcSlice->setMvdL1ZeroFlag(false);
        }
        pcPic->getSlice()->setMvdL1ZeroFlag(pcSlice->getMvdL1ZeroFlag());

        Double lambda            = 0.0;
        Int actualHeadBits       = 0;
        Int actualTotalBits      = 0;
        Int estimatedBits        = 0;
        Int tmpBitsBeforeWriting = 0;

        if (m_pcCfg->getUseRateCtrl())
        {
            Int frameLevel = m_pcRateCtrl->getRCSeq()->getGOPID2Level(iGOPid);
            if (pcPic->getSlice()->getSliceType() == I_SLICE)
            {
                frameLevel = 0;
            }
            m_pcRateCtrl->initRCPic(frameLevel);
            estimatedBits = m_pcRateCtrl->getRCPic()->getTargetBits();

            Int sliceQP = m_pcCfg->getInitialQP();
            if ((pcSlice->getPOC() == 0 && m_pcCfg->getInitialQP() > 0) || (frameLevel == 0 && m_pcCfg->getForceIntraQP())) // QP is specified
            {
                Int    NumberBFrames = (m_pcCfg->getGOPSize() - 1);
                Double dLambda_scale = 1.0 - Clip3(0.0, 0.5, 0.05 * (Double)NumberBFrames);
                Double dQPFactor     = 0.57 * dLambda_scale;
                Int    SHIFT_QP      = 12;
                Int    bitdepth_luma_qp_scale = 0;
                Double qp_temp = (Double)sliceQP + bitdepth_luma_qp_scale - SHIFT_QP;
                lambda = dQPFactor * pow(2.0, qp_temp / 3.0);
            }
            else if (frameLevel == 0) // intra case, but use the model
            {
                if (m_pcCfg->getIntraPeriod() != 1) // do not refine allocated bits for all intra case
                {
                    Int bits = m_pcRateCtrl->getRCSeq()->getLeftAverageBits();
                    bits = m_pcRateCtrl->getRCSeq()->getRefineBitsForIntra(bits);
                    if (bits < 200)
                    {
                        bits = 200;
                    }
                    m_pcRateCtrl->getRCPic()->setTargetBits(bits);
                }

                list<TEncRCPic*> listPreviousPicture = m_pcRateCtrl->getPicList();
                lambda  = m_pcRateCtrl->getRCPic()->estimatePicLambda(listPreviousPicture);
                sliceQP = m_pcRateCtrl->getRCPic()->estimatePicQP(lambda, listPreviousPicture);
            }
            else // normal case
            {
                list<TEncRCPic*> listPreviousPicture = m_pcRateCtrl->getPicList();
                lambda  = m_pcRateCtrl->getRCPic()->estimatePicLambda(listPreviousPicture);
                sliceQP = m_pcRateCtrl->getRCPic()->estimatePicQP(lambda, listPreviousPicture);
            }

            sliceQP = Clip3(-pcSlice->getSPS()->getQpBDOffsetY(), MAX_QP, sliceQP);
            m_pcRateCtrl->getRCPic()->setPicEstQP(sliceQP);

            pcSliceEncoder->resetQP(pcPic, pcEncodeFrame, sliceQP, lambda);
        }

        UInt uiInternalAddress = pcPic->getNumPartInCU() - 4;
        UInt uiExternalAddress = pcPic->getPicSym()->getNumberOfCUsInFrame() - 1;
        UInt uiPosX = (uiExternalAddress % pcPic->getFrameWidthInCU()) * g_uiMaxCUWidth + g_auiRasterToPelX[g_auiZscanToRaster[uiInternalAddress]];
        UInt uiPosY = (uiExternalAddress / pcPic->getFrameWidthInCU()) * g_uiMaxCUHeight + g_auiRasterToPelY[g_auiZscanToRaster[uiInternalAddress]];
        UInt uiWidth = pcSlice->getSPS()->getPicWidthInLumaSamples();
        UInt uiHeight = pcSlice->getSPS()->getPicHeightInLumaSamples();
        while (uiPosX >= uiWidth || uiPosY >= uiHeight)
        {
            uiInternalAddress--;
            uiPosX = (uiExternalAddress % pcPic->getFrameWidthInCU()) * g_uiMaxCUWidth + g_auiRasterToPelX[g_auiZscanToRaster[uiInternalAddress]];
            uiPosY = (uiExternalAddress / pcPic->getFrameWidthInCU()) * g_uiMaxCUHeight + g_auiRasterToPelY[g_auiZscanToRaster[uiInternalAddress]];
        }

        uiInternalAddress++;
        if (uiInternalAddress == pcPic->getNumPartInCU())
        {
            uiInternalAddress = 0;
            uiExternalAddress++;
        }
        UInt uiRealEndAddress = uiExternalAddress * pcPic->getNumPartInCU() + uiInternalAddress;

        Int  j;

        // Allocate some coders, now we know how many tiles there are.
        const Bool bWaveFrontsynchro = m_pcCfg->getWaveFrontsynchro();
        const UInt uiHeightInLCUs = pcPic->getPicSym()->getFrameHeightInCU();
        const Int  iNumSubstreams = (bWaveFrontsynchro ? uiHeightInLCUs : 1);

        // Allocate some coders, now we know how many tiles there are.
        pcSubstreamsOut = new TComOutputBitstream[iNumSubstreams];

        pcSlice->setNextSlice(false);
        pcSliceEncoder->compressSlice(pcPic, pcEncodeFrame);  // The bulk of the real work
        pcSlice = pcPic->getSlice();

        // SAO parameter estimation using non-deblocked pixels for LCU bottom and right boundary areas
        if (m_pcCfg->getSaoLcuBasedOptimization() && m_pcCfg->getSaoLcuBoundary())
        {
            pcSAO->resetStats();
            pcSAO->calcSaoStatsCu_BeforeDblk(pcPic);
        }

        //-- Loop filter
        Bool bLFCrossTileBoundary = pcSlice->getPPS()->getLoopFilterAcrossTilesEnabledFlag();
        pcLoopFilter->setCfg(bLFCrossTileBoundary);
        pcLoopFilter->loopFilterPic(pcPic);

        pcSlice = pcPic->getSlice();
        if (pcSlice->getSPS()->getUseSAO())
        {
            pcPic->createNonDBFilterInfo(pcSlice->getSliceCurEndCUAddr(), 0, bLFCrossTileBoundary);
            pcSAO->createPicSaoInfo(pcPic);
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////// File writing
        // Set entropy coder
        pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);

        /* write various header sets. */
        if (iPOCLast == 0)
        {
            OutputNALUnit nalu(NAL_UNIT_VPS);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
            pcEntropyCoder->encodeVPS(m_pcEncTop->getVPS());
            writeRBSPTrailingBits(nalu.m_Bitstream);
            accessUnit.push_back(new NALUnitEBSP(nalu));
            actualTotalBits += UInt(accessUnit.back()->m_nalUnitData.str().size()) * 8;

            nalu = NALUnit(NAL_UNIT_SPS);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
            pcSlice->getSPS()->setNumLongTermRefPicSPS(m_numLongTermRefPicSPS);
            for (Int k = 0; k < m_numLongTermRefPicSPS; k++)
            {
                pcSlice->getSPS()->setLtRefPicPocLsbSps(k, m_ltRefPicPocLsbSps[k]);
                pcSlice->getSPS()->setUsedByCurrPicLtSPSFlag(k, m_ltRefPicUsedByCurrPicFlag[k]);
            }
            if (m_pcCfg->getPictureTimingSEIEnabled() || m_pcCfg->getDecodingUnitInfoSEIEnabled())
            {
                // CHECK_ME: maybe HM's bug
                UInt maxCU = 1500 >> (pcSlice->getSPS()->getMaxCUDepth() << 1);
                UInt numDU = 0;
                if (pcPic->getNumCUsInFrame() % maxCU != 0 || numDU == 0)
                {
                    numDU++;
                }
                pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->setNumDU(numDU);
                pcSlice->getSPS()->setHrdParameters(m_pcCfg->getFrameRate(), numDU, m_pcCfg->getTargetBitrate(), (m_pcCfg->getIntraPeriod() > 0));
            }
            if (m_pcCfg->getBufferingPeriodSEIEnabled() || m_pcCfg->getPictureTimingSEIEnabled() || m_pcCfg->getDecodingUnitInfoSEIEnabled())
            {
                pcSlice->getSPS()->getVuiParameters()->setHrdParametersPresentFlag(true);
            }
            pcEntropyCoder->encodeSPS(pcSlice->getSPS());
            writeRBSPTrailingBits(nalu.m_Bitstream);
            accessUnit.push_back(new NALUnitEBSP(nalu));
            actualTotalBits += UInt(accessUnit.back()->m_nalUnitData.str().size()) * 8;

            nalu = NALUnit(NAL_UNIT_PPS);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
            pcEntropyCoder->encodePPS(pcSlice->getPPS());
            writeRBSPTrailingBits(nalu.m_Bitstream);
            accessUnit.push_back(new NALUnitEBSP(nalu));
            actualTotalBits += UInt(accessUnit.back()->m_nalUnitData.str().size()) * 8;

            if (m_pcCfg->getActiveParameterSetsSEIEnabled())
            {
                SEIActiveParameterSets *sei = xCreateSEIActiveParameterSets(pcSlice->getSPS());

                pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
                m_seiWriter.writeSEImessage(nalu.m_Bitstream, *sei, pcSlice->getSPS());
                writeRBSPTrailingBits(nalu.m_Bitstream);
                accessUnit.push_back(new NALUnitEBSP(nalu));
                delete sei;
                bActiveParameterSetSEIPresentInAU = true;
            }

            if (m_pcCfg->getDisplayOrientationSEIAngle())
            {
                SEIDisplayOrientation *sei = xCreateSEIDisplayOrientation();

                nalu = NALUnit(NAL_UNIT_PREFIX_SEI);
                pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
                m_seiWriter.writeSEImessage(nalu.m_Bitstream, *sei, pcSlice->getSPS());
                writeRBSPTrailingBits(nalu.m_Bitstream);
                accessUnit.push_back(new NALUnitEBSP(nalu));
                delete sei;
            }
        }

        if (writeSOP) // write SOP description SEI (if enabled) at the beginning of GOP
        {
            Int SOPcurrPOC = pocCurr;

            OutputNALUnit nalu(NAL_UNIT_PREFIX_SEI);
            pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);

            SEISOPDescription SOPDescriptionSEI;
            SOPDescriptionSEI.m_sopSeqParameterSetId = pcSlice->getSPS()->getSPSId();

            UInt i = 0;
            UInt prevEntryId = iGOPid;
            for (j = iGOPid; j < gopSize; j++)
            {
                Int deltaPOC = m_pcCfg->getGOPEntry(j).m_POC - m_pcCfg->getGOPEntry(prevEntryId).m_POC;
                if ((SOPcurrPOC + deltaPOC) < m_pcCfg->getFramesToBeEncoded())
                {
                    SOPcurrPOC += deltaPOC;
                    SOPDescriptionSEI.m_sopDescVclNaluType[i] = getNalUnitType(SOPcurrPOC, m_iLastIDR);
                    SOPDescriptionSEI.m_sopDescTemporalId[i] = m_pcCfg->getGOPEntry(j).m_temporalId;
                    SOPDescriptionSEI.m_sopDescStRpsIdx[i] = getReferencePictureSetIdxForSOP(pcSlice, SOPcurrPOC, j);
                    SOPDescriptionSEI.m_sopDescPocDelta[i] = deltaPOC;

                    prevEntryId = j;
                    i++;
                }
            }

            SOPDescriptionSEI.m_numPicsInSopMinus1 = i - 1;

            m_seiWriter.writeSEImessage(nalu.m_Bitstream, SOPDescriptionSEI, pcSlice->getSPS());
            writeRBSPTrailingBits(nalu.m_Bitstream);
            accessUnit.push_back(new NALUnitEBSP(nalu));

            writeSOP = false;
        }

        if ((m_pcCfg->getPictureTimingSEIEnabled() || m_pcCfg->getDecodingUnitInfoSEIEnabled()) &&
            (pcSlice->getSPS()->getVuiParametersPresentFlag()) &&
            ((pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getNalHrdParametersPresentFlag())
             || (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getVclHrdParametersPresentFlag())))
        {
            if (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getSubPicCpbParamsPresentFlag())
            {
                UInt numDU = pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getNumDU();
                pictureTimingSEI.m_numDecodingUnitsMinus1     = (numDU - 1);
                pictureTimingSEI.m_duCommonCpbRemovalDelayFlag = false;

                if (pictureTimingSEI.m_numNalusInDuMinus1 == NULL)
                {
                    pictureTimingSEI.m_numNalusInDuMinus1 = new UInt[numDU];
                }
                if (pictureTimingSEI.m_duCpbRemovalDelayMinus1 == NULL)
                {
                    pictureTimingSEI.m_duCpbRemovalDelayMinus1  = new UInt[numDU];
                }
                if (accumBitsDU == NULL)
                {
                    accumBitsDU = new UInt[numDU];
                }
                if (accumNalsDU == NULL)
                {
                    accumNalsDU = new UInt[numDU];
                }
            }
            pictureTimingSEI.m_auCpbRemovalDelay = std::max<Int>(1, m_totalCoded - m_lastBPSEI); // Syntax element signaled as minus, hence the .
            pictureTimingSEI.m_picDpbOutputDelay = pcSlice->getSPS()->getNumReorderPics(0) + pcSlice->getPOC() - m_totalCoded;
            Int factor = pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getTickDivisorMinus2() + 2;
            pictureTimingSEI.m_picDpbOutputDuDelay = factor * pictureTimingSEI.m_picDpbOutputDelay;
            if (m_pcCfg->getDecodingUnitInfoSEIEnabled())
            {
                picSptDpbOutputDuDelay = factor * pictureTimingSEI.m_picDpbOutputDelay;
            }
        }

        if ((m_pcCfg->getBufferingPeriodSEIEnabled()) && (pcSlice->getSliceType() == I_SLICE) &&
            (pcSlice->getSPS()->getVuiParametersPresentFlag()) &&
            ((pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getNalHrdParametersPresentFlag())
             || (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getVclHrdParametersPresentFlag())))
        {
            OutputNALUnit nalu(NAL_UNIT_PREFIX_SEI);
            pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);

            SEIBufferingPeriod sei_buffering_period;

            UInt uiInitialCpbRemovalDelay = (90000 / 2);              // 0.5 sec
            sei_buffering_period.m_initialCpbRemovalDelay[0][0]     = uiInitialCpbRemovalDelay;
            sei_buffering_period.m_initialCpbRemovalDelayOffset[0][0]     = uiInitialCpbRemovalDelay;
            sei_buffering_period.m_initialCpbRemovalDelay[0][1]     = uiInitialCpbRemovalDelay;
            sei_buffering_period.m_initialCpbRemovalDelayOffset[0][1]     = uiInitialCpbRemovalDelay;

            Double dTmp = (Double)pcSlice->getSPS()->getVuiParameters()->getTimingInfo()->getNumUnitsInTick() / (Double)pcSlice->getSPS()->getVuiParameters()->getTimingInfo()->getTimeScale();

            UInt uiTmp = (UInt)(dTmp * 90000.0);
            uiInitialCpbRemovalDelay -= uiTmp;
            uiInitialCpbRemovalDelay -= uiTmp / (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getTickDivisorMinus2() + 2);
            sei_buffering_period.m_initialAltCpbRemovalDelay[0][0]  = uiInitialCpbRemovalDelay;
            sei_buffering_period.m_initialAltCpbRemovalDelayOffset[0][0]  = uiInitialCpbRemovalDelay;
            sei_buffering_period.m_initialAltCpbRemovalDelay[0][1]  = uiInitialCpbRemovalDelay;
            sei_buffering_period.m_initialAltCpbRemovalDelayOffset[0][1]  = uiInitialCpbRemovalDelay;

            sei_buffering_period.m_rapCpbParamsPresentFlag = 0;
            //for the concatenation, it can be set to one during splicing.
            sei_buffering_period.m_concatenationFlag = 0;
            //since the temporal layer HRD is not ready, we assumed it is fixed
            sei_buffering_period.m_auCpbRemovalDelayDelta = 1;
            sei_buffering_period.m_cpbDelayOffset = 0;
            sei_buffering_period.m_dpbDelayOffset = 0;

            m_seiWriter.writeSEImessage(nalu.m_Bitstream, sei_buffering_period, pcSlice->getSPS());
            writeRBSPTrailingBits(nalu.m_Bitstream);
            {
                UInt seiPositionInAu = xGetFirstSeiLocation(accessUnit);
                UInt offsetPosition = bActiveParameterSetSEIPresentInAU; // Insert BP SEI after APS SEI
                AccessUnit::iterator it;
                for (j = 0, it = accessUnit.begin(); j < seiPositionInAu + offsetPosition; j++)
                {
                    it++;
                }

                accessUnit.insert(it, new NALUnitEBSP(nalu));
                bBufferingPeriodSEIPresentInAU = true;
            }

            if (m_pcCfg->getScalableNestingSEIEnabled())
            {
                OutputNALUnit naluTmp(NAL_UNIT_PREFIX_SEI);
                pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
                pcEntropyCoder->setBitstream(&naluTmp.m_Bitstream);
                scalableNestingSEI.m_nestedSEIs.clear();
                scalableNestingSEI.m_nestedSEIs.push_back(&sei_buffering_period);
                m_seiWriter.writeSEImessage(naluTmp.m_Bitstream, scalableNestingSEI, pcSlice->getSPS());
                writeRBSPTrailingBits(naluTmp.m_Bitstream);
                UInt seiPositionInAu = xGetFirstSeiLocation(accessUnit);
                UInt offsetPosition = bActiveParameterSetSEIPresentInAU + bBufferingPeriodSEIPresentInAU + bPictureTimingSEIPresentInAU; // Insert BP SEI after non-nested APS, BP and PT SEIs
                AccessUnit::iterator it;
                for (j = 0, it = accessUnit.begin(); j < seiPositionInAu + offsetPosition; j++)
                {
                    it++;
                }

                accessUnit.insert(it, new NALUnitEBSP(naluTmp));
                bNestedBufferingPeriodSEIPresentInAU = true;
            }

            m_lastBPSEI = m_totalCoded;
            m_cpbRemovalDelay = 0;
        }
        m_cpbRemovalDelay++;
        if ((m_pcEncTop->getRecoveryPointSEIEnabled()) && (pcSlice->getSliceType() == I_SLICE))
        {
            if (m_pcEncTop->getGradualDecodingRefreshInfoEnabled() && !pcSlice->getRapPicFlag())
            {
                // Gradual decoding refresh SEI
                OutputNALUnit nalu(NAL_UNIT_PREFIX_SEI);
                pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
                pcEntropyCoder->setBitstream(&nalu.m_Bitstream);

                SEIGradualDecodingRefreshInfo seiGradualDecodingRefreshInfo;
                seiGradualDecodingRefreshInfo.m_gdrForegroundFlag = true; // Indicating all "foreground"

                m_seiWriter.writeSEImessage(nalu.m_Bitstream, seiGradualDecodingRefreshInfo, pcSlice->getSPS());
                writeRBSPTrailingBits(nalu.m_Bitstream);
                accessUnit.push_back(new NALUnitEBSP(nalu));
            }
            // Recovery point SEI
            OutputNALUnit nalu(NAL_UNIT_PREFIX_SEI);
            pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);

            SEIRecoveryPoint sei_recovery_point;
            sei_recovery_point.m_recoveryPocCnt    = 0;
            sei_recovery_point.m_exactMatchingFlag = (pcSlice->getPOC() == 0) ? (true) : (false);
            sei_recovery_point.m_brokenLinkFlag    = false;

            m_seiWriter.writeSEImessage(nalu.m_Bitstream, sei_recovery_point, pcSlice->getSPS());
            writeRBSPTrailingBits(nalu.m_Bitstream);
            accessUnit.push_back(new NALUnitEBSP(nalu));
        }

        /* use the main bitstream buffer for storing the marshaled picture */
        pcEntropyCoder->setBitstream(NULL);

        pcSlice = pcPic->getSlice();
        if (pcSlice->getSPS()->getUseSAO())
        {
            // set entropy coder for RD
            pcEntropyCoder->setEntropyCoder(pcSbacCoder, pcSlice);
            if (pcSlice->getSPS()->getUseSAO())
            {
                pcEntropyCoder->resetEntropy();
                pcEntropyCoder->setBitstream(pcBitCounter);

                // CHECK_ME: I think the SAO is use a temp Sbac only, so I always use [0], am I right?
                pcSAO->startSaoEnc(pcPic, pcEntropyCoder, pcEncodeFrame->getRDSbacCoders(0), pcEncodeFrame->getRDGoOnSbacCoder(0));

                SAOParam& cSaoParam = *pcSlice->getPic()->getPicSym()->getSaoParam();

                pcSAO->SAOProcess(&cSaoParam, pcPic->getSlice()->getLambdaLuma(), pcPic->getSlice()->getLambdaChroma(), pcPic->getSlice()->getDepth());
                pcSAO->endSaoEnc();
                pcSAO->PCMLFDisableProcess(pcPic);
            }

            if (pcSlice->getSPS()->getUseSAO())
            {
                pcPic->getSlice()->setSaoEnabledFlag((pcSlice->getPic()->getPicSym()->getSaoParam()->bSaoFlag[0] == 1) ? true : false);
            }
        }

        pcSlice->setNextSlice(false);
        pcSlice = pcPic->getSlice();
        pcSliceEncoder->setSliceIdx(0);

        // Reconstruction slice
        pcSlice->setNextSlice(true);
        pcSlice->setRPS(pcPic->getSlice()->getRPS());
        pcSlice->setRPSidx(pcPic->getSlice()->getRPSidx());
        pcSliceEncoder->xDetermineStartAndBoundingCUAddr(pcPic, true);

        uiInternalAddress = (pcSlice->getSliceCurEndCUAddr() - 1) % pcPic->getNumPartInCU();
        uiExternalAddress = (pcSlice->getSliceCurEndCUAddr() - 1) / pcPic->getNumPartInCU();
        uiPosX = (uiExternalAddress % pcPic->getFrameWidthInCU()) * g_uiMaxCUWidth + g_auiRasterToPelX[g_auiZscanToRaster[uiInternalAddress]];
        uiPosY = (uiExternalAddress / pcPic->getFrameWidthInCU()) * g_uiMaxCUHeight + g_auiRasterToPelY[g_auiZscanToRaster[uiInternalAddress]];
        uiWidth = pcSlice->getSPS()->getPicWidthInLumaSamples();
        uiHeight = pcSlice->getSPS()->getPicHeightInLumaSamples();
        while (uiPosX >= uiWidth || uiPosY >= uiHeight)
        {
            uiInternalAddress--;
            uiPosX = (uiExternalAddress % pcPic->getFrameWidthInCU()) * g_uiMaxCUWidth + g_auiRasterToPelX[g_auiZscanToRaster[uiInternalAddress]];
            uiPosY = (uiExternalAddress / pcPic->getFrameWidthInCU()) * g_uiMaxCUHeight + g_auiRasterToPelY[g_auiZscanToRaster[uiInternalAddress]];
        }

        uiInternalAddress++;
        if (uiInternalAddress == pcPic->getNumPartInCU())
        {
            uiInternalAddress = 0;
            uiExternalAddress = (uiExternalAddress + 1);
        }
        UInt endAddress = (uiExternalAddress * pcPic->getNumPartInCU() + uiInternalAddress);
        pcSlice->allocSubstreamSizes(iNumSubstreams);
        for (UInt ui = 0; ui < iNumSubstreams; ui++)
        {
            pcSubstreamsOut[ui].clear();
        }

        pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
        pcEntropyCoder->resetEntropy();
        /* start slice NALunit */
        OutputNALUnit nalu(pcSlice->getNalUnitType(), pcSlice->getTLayer());
        Bool sliceSegment = (!pcSlice->isNextSlice());
        if (!sliceSegment)
        {
            uiOneBitstreamPerSliceLength = 0; // start of a new slice
        }
        pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
        tmpBitsBeforeWriting = pcEntropyCoder->getNumberOfWrittenBits();
        pcEntropyCoder->encodeSliceHeader(pcSlice);
        actualHeadBits += (pcEntropyCoder->getNumberOfWrittenBits() - tmpBitsBeforeWriting);

        // is it needed?
        if (!sliceSegment)
        {
            pcBitstreamRedirect->writeAlignOne();
        }
        else
        {
            // We've not completed our slice header info yet, do the alignment later.
        }
        pcSbacCoder->init((TEncBinIf*)pcBinCABAC);
        pcEntropyCoder->setEntropyCoder(pcSbacCoder, pcSlice);
        pcEntropyCoder->resetEntropy();
        pcEncodeFrame->resetEntropy(pcSlice);

        if (pcSlice->isNextSlice())
        {
            // set entropy coder for writing
            pcSbacCoder->init((TEncBinIf*)pcBinCABAC);
            {
                pcEncodeFrame->resetEntropy(pcSlice);
                pcEncodeFrame->getSbacCoder(0)->load(pcSbacCoder);
                pcEntropyCoder->setEntropyCoder(pcEncodeFrame->getSbacCoder(0), pcSlice); //ALF is written in substream #0 with CABAC coder #0 (see ALF param encoding below)
            }
            pcEntropyCoder->resetEntropy();
            // File writing
            if (!sliceSegment)
            {
                pcEntropyCoder->setBitstream(pcBitstreamRedirect);
            }
            else
            {
                pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
            }
            // for now, override the TILES_DECODER setting in order to write substreams.
            pcEntropyCoder->setBitstream(&pcSubstreamsOut[0]);
        }
        pcSlice->setFinalized(true);

        pcSbacCoder->load(pcEncodeFrame->getSbacCoder(0));

        pcSlice->setTileOffstForMultES(uiOneBitstreamPerSliceLength);
        pcSlice->setTileLocationCount(0);
        pcSliceEncoder->encodeSlice(pcPic, pcSubstreamsOut, pcEncodeFrame);

        {
            // Construct the final bitstream by flushing and concatenating substreams.
            // The final bitstream is either nalu.m_Bitstream or pcBitstreamRedirect;
            UInt* puiSubstreamSizes = pcSlice->getSubstreamSizes();
            for (UInt ui = 0; ui < iNumSubstreams; ui++)
            {
                // Flush all substreams -- this includes empty ones.
                // Terminating bit and flush.
                pcEntropyCoder->setEntropyCoder(pcEncodeFrame->getSbacCoder(ui), pcSlice);
                pcEntropyCoder->setBitstream(&pcSubstreamsOut[ui]);
                pcEntropyCoder->encodeTerminatingBit(1);
                pcEntropyCoder->encodeSliceFinish();

                pcSubstreamsOut[ui].writeByteAlignment(); // Byte-alignment in slice_data() at end of sub-stream

                // Byte alignment is necessary between tiles when tiles are independent.
                if (ui + 1 < iNumSubstreams)
                {
                    puiSubstreamSizes[ui] = pcSubstreamsOut[ui].getNumberOfWrittenBits() + (pcSubstreamsOut[ui].countStartCodeEmulations() << 3);
                }
            }

            // Complete the slice header info.
            pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
            pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
            pcEntropyCoder->encodeTilesWPPEntryPoint(pcSlice);

            // Substreams...
            Int nss = pcSlice->getPPS()->getEntropyCodingSyncEnabledFlag() ? pcSlice->getNumEntryPointOffsets() + 1 : iNumSubstreams;
            for (Int i = 0; i < nss; i++)
            {
                pcBitstreamRedirect->addSubstream(&pcSubstreamsOut[i]);
            }
        }

        // If current NALU is the first NALU of slice (containing slice header) and more NALUs exist (due to multiple dependent slices) then buffer it.
        // If current NALU is the last NALU of slice and a NALU was buffered, then (a) Write current NALU (b) Update an write buffered NALU at approproate location in NALU list.
        xAttachSliceDataToNalUnit(pcEntropyCoder, nalu, pcBitstreamRedirect);
        accessUnit.push_back(new NALUnitEBSP(nalu));
        actualTotalBits += UInt(accessUnit.back()->m_nalUnitData.str().size()) * 8;
        uiOneBitstreamPerSliceLength += nalu.m_Bitstream.getNumberOfWrittenBits(); // length of bitstream after byte-alignment

        if ((m_pcCfg->getPictureTimingSEIEnabled() || m_pcCfg->getDecodingUnitInfoSEIEnabled()) &&
            (pcSlice->getSPS()->getVuiParametersPresentFlag()) &&
            ((pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getNalHrdParametersPresentFlag())
                || (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getVclHrdParametersPresentFlag())) &&
            (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getSubPicCpbParamsPresentFlag()))
        {
            UInt numNalus = 0;
            UInt numRBSPBytes = 0;
            for (AccessUnit::const_iterator it = accessUnit.begin(); it != accessUnit.end(); it++)
            {
                UInt numRBSPBytes_nal = UInt((*it)->m_nalUnitData.str().size());
                if ((*it)->m_nalUnitType != NAL_UNIT_PREFIX_SEI && (*it)->m_nalUnitType != NAL_UNIT_SUFFIX_SEI)
                {
                    numRBSPBytes += numRBSPBytes_nal;
                    numNalus++;
                }
            }

            accumBitsDU[0] = (numRBSPBytes << 3);
            accumNalsDU[0] = numNalus; // SEI not counted for bit count; hence shouldn't be counted for # of NALUs - only for consistency
        }

        if (pcSlice->getSPS()->getUseSAO())
        {
            if (pcSlice->getSPS()->getUseSAO())
            {
                pcSAO->destroyPicSaoInfo();
            }
            pcPic->destroyNonDBFilterInfo();
        }

        pcPic->compressMotion();

        const Char* digestStr = NULL;
        if (m_pcCfg->getDecodedPictureHashSEIEnabled())
        {
            /* calculate MD5sum for entire reconstructed picture */
            SEIDecodedPictureHash sei_recon_picture_digest;
            if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 1)
            {
                sei_recon_picture_digest.method = SEIDecodedPictureHash::MD5;
                calcMD5(*pcPic->getPicYuvRec(), sei_recon_picture_digest.digest);
                digestStr = digestToString(sei_recon_picture_digest.digest, 16);
            }
            else if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 2)
            {
                sei_recon_picture_digest.method = SEIDecodedPictureHash::CRC;
                calcCRC(*pcPic->getPicYuvRec(), sei_recon_picture_digest.digest);
                digestStr = digestToString(sei_recon_picture_digest.digest, 2);
            }
            else if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 3)
            {
                sei_recon_picture_digest.method = SEIDecodedPictureHash::CHECKSUM;
                calcChecksum(*pcPic->getPicYuvRec(), sei_recon_picture_digest.digest);
                digestStr = digestToString(sei_recon_picture_digest.digest, 4);
            }
            OutputNALUnit onalu(NAL_UNIT_SUFFIX_SEI, pcSlice->getTLayer());

            /* write the SEI messages */
            pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
            m_seiWriter.writeSEImessage(onalu.m_Bitstream, sei_recon_picture_digest, pcSlice->getSPS());
            writeRBSPTrailingBits(onalu.m_Bitstream);

            accessUnit.insert(accessUnit.end(), new NALUnitEBSP(onalu));
        }
        if (m_pcCfg->getTemporalLevel0IndexSEIEnabled())
        {
            SEITemporalLevel0Index sei_temporal_level0_index;
            if (pcSlice->getRapPicFlag())
            {
                m_tl0Idx = 0;
                m_rapIdx = (m_rapIdx + 1) & 0xFF;
            }
            else
            {
                m_tl0Idx = (m_tl0Idx + (pcSlice->getTLayer() ? 0 : 1)) & 0xFF;
            }
            sei_temporal_level0_index.tl0Idx = m_tl0Idx;
            sei_temporal_level0_index.rapIdx = m_rapIdx;

            OutputNALUnit onalu(NAL_UNIT_PREFIX_SEI);

            /* write the SEI messages */
            pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
            m_seiWriter.writeSEImessage(onalu.m_Bitstream, sei_temporal_level0_index, pcSlice->getSPS());
            writeRBSPTrailingBits(onalu.m_Bitstream);

            /* insert the SEI message NALUnit before any Slice NALUnits */
            AccessUnit::iterator it = find_if(accessUnit.begin(), accessUnit.end(), mem_fun(&NALUnit::isSlice));
            accessUnit.insert(it, new NALUnitEBSP(onalu));
        }

        xCalculateAddPSNR(pcPic, pcPic->getPicYuvRec(), accessUnit);

        if (digestStr && m_pcCfg->getLogLevel() >= X265_LOG_DEBUG)
        {
            if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 1)
            {
                fprintf(stderr, " [MD5:%s]", digestStr);
            }
            else if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 2)
            {
                fprintf(stderr, " [CRC:%s]", digestStr);
            }
            else if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 3)
            {
                fprintf(stderr, " [Checksum:%s]", digestStr);
            }
        }
        if (m_pcCfg->getUseRateCtrl())
        {
            Double effectivePercentage = m_pcRateCtrl->getRCPic()->getEffectivePercentage();
            Double avgQP     = m_pcRateCtrl->getRCPic()->calAverageQP();
            Double avgLambda = m_pcRateCtrl->getRCPic()->calAverageLambda();
            if (avgLambda < 0.0)
            {
                avgLambda = lambda;
            }
            m_pcRateCtrl->getRCPic()->updateAfterPicture(actualHeadBits, actualTotalBits, avgQP, avgLambda, effectivePercentage);
            m_pcRateCtrl->getRCPic()->addToPictureLsit(m_pcRateCtrl->getPicList());

            m_pcRateCtrl->getRCSeq()->updateAfterPic(actualTotalBits);
            if (pcSlice->getSliceType() != I_SLICE)
            {
                m_pcRateCtrl->getRCGOP()->updateAfterPicture(actualTotalBits);
            }
            else // for intra picture, the estimated bits are used to update the current status in the GOP
            {
                m_pcRateCtrl->getRCGOP()->updateAfterPicture(estimatedBits);
            }
        }
        if ((m_pcCfg->getPictureTimingSEIEnabled() || m_pcCfg->getDecodingUnitInfoSEIEnabled()) &&
            (pcSlice->getSPS()->getVuiParametersPresentFlag()) &&
            ((pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getNalHrdParametersPresentFlag())
             || (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getVclHrdParametersPresentFlag())))
        {
            TComVUI *vui = pcSlice->getSPS()->getVuiParameters();
            TComHRD *hrd = vui->getHrdParameters();

            if (hrd->getSubPicCpbParamsPresentFlag())
            {
                Int i;
                UInt64 ui64Tmp;
                UInt uiPrev = 0;
                UInt numDU = (pictureTimingSEI.m_numDecodingUnitsMinus1 + 1);
                UInt *pCRD = &pictureTimingSEI.m_duCpbRemovalDelayMinus1[0];
                UInt maxDiff = (hrd->getTickDivisorMinus2() + 2) - 1;

                for (i = 0; i < numDU; i++)
                {
                    pictureTimingSEI.m_numNalusInDuMinus1[i]       = (i == 0) ? (accumNalsDU[i] - 1) : (accumNalsDU[i] - accumNalsDU[i - 1] - 1);
                }

                if (numDU == 1)
                {
                    pCRD[0] = 0; /* don't care */
                }
                else
                {
                    pCRD[numDU - 1] = 0; /* by definition */
                    UInt tmp = 0;
                    UInt accum = 0;

                    for (i = (numDU - 2); i >= 0; i--)
                    {
                        ui64Tmp = (((accumBitsDU[numDU - 1]  - accumBitsDU[i]) * (vui->getTimingInfo()->getTimeScale() / vui->getTimingInfo()->getNumUnitsInTick()) * (hrd->getTickDivisorMinus2() + 2)) / (m_pcCfg->getTargetBitrate()));
                        if ((UInt)ui64Tmp > maxDiff)
                        {
                            tmp++;
                        }
                    }

                    uiPrev = 0;

                    UInt flag = 0;
                    for (i = (numDU - 2); i >= 0; i--)
                    {
                        flag = 0;
                        ui64Tmp = (((accumBitsDU[numDU - 1]  - accumBitsDU[i]) * (vui->getTimingInfo()->getTimeScale() / vui->getTimingInfo()->getNumUnitsInTick()) * (hrd->getTickDivisorMinus2() + 2)) / (m_pcCfg->getTargetBitrate()));

                        if ((UInt)ui64Tmp > maxDiff)
                        {
                            if (uiPrev >= maxDiff - tmp)
                            {
                                ui64Tmp = uiPrev + 1;
                                flag = 1;
                            }
                            else ui64Tmp = maxDiff - tmp + 1;
                        }
                        pCRD[i] = (UInt)ui64Tmp - uiPrev - 1;
                        if ((Int)pCRD[i] < 0)
                        {
                            pCRD[i] = 0;
                        }
                        else if (tmp > 0 && flag == 1)
                        {
                            tmp--;
                        }
                        accum += pCRD[i] + 1;
                        uiPrev = accum;
                    }
                }
            }
            if (m_pcCfg->getPictureTimingSEIEnabled())
            {
                {
                    OutputNALUnit onalu(NAL_UNIT_PREFIX_SEI, pcSlice->getTLayer());
                    pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
                    m_seiWriter.writeSEImessage(onalu.m_Bitstream, pictureTimingSEI, pcSlice->getSPS());
                    writeRBSPTrailingBits(onalu.m_Bitstream);
                    UInt seiPositionInAu = xGetFirstSeiLocation(accessUnit);
                    UInt offsetPosition = bActiveParameterSetSEIPresentInAU
                        + bBufferingPeriodSEIPresentInAU;                  // Insert PT SEI after APS and BP SEI
                    AccessUnit::iterator it;
                    for (j = 0, it = accessUnit.begin(); j < seiPositionInAu + offsetPosition; j++)
                    {
                        it++;
                    }

                    accessUnit.insert(it, new NALUnitEBSP(onalu));
                    bPictureTimingSEIPresentInAU = true;
                }
                if (m_pcCfg->getScalableNestingSEIEnabled()) // put picture timing SEI into scalable nesting SEI
                {
                    OutputNALUnit onalu(NAL_UNIT_PREFIX_SEI, pcSlice->getTLayer());
                    pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
                    scalableNestingSEI.m_nestedSEIs.clear();
                    scalableNestingSEI.m_nestedSEIs.push_back(&pictureTimingSEI);
                    m_seiWriter.writeSEImessage(onalu.m_Bitstream, scalableNestingSEI, pcSlice->getSPS());
                    writeRBSPTrailingBits(onalu.m_Bitstream);
                    UInt seiPositionInAu = xGetFirstSeiLocation(accessUnit);
                    UInt offsetPosition = bActiveParameterSetSEIPresentInAU
                        + bBufferingPeriodSEIPresentInAU + bPictureTimingSEIPresentInAU + bNestedBufferingPeriodSEIPresentInAU; // Insert PT SEI after APS and BP SEI
                    AccessUnit::iterator it;
                    for (j = 0, it = accessUnit.begin(); j < seiPositionInAu + offsetPosition; j++)
                    {
                        it++;
                    }

                    accessUnit.insert(it, new NALUnitEBSP(onalu));
                }
            }
            if (m_pcCfg->getDecodingUnitInfoSEIEnabled() && hrd->getSubPicCpbParamsPresentFlag())
            {
                pcEntropyCoder->setEntropyCoder(pcCavlcCoder, pcSlice);
                for (Int i = 0; i < (pictureTimingSEI.m_numDecodingUnitsMinus1 + 1); i++)
                {
                    OutputNALUnit onalu(NAL_UNIT_PREFIX_SEI, pcSlice->getTLayer());

                    SEIDecodingUnitInfo tempSEI;
                    tempSEI.m_decodingUnitIdx = i;
                    tempSEI.m_duSptCpbRemovalDelay = pictureTimingSEI.m_duCpbRemovalDelayMinus1[i] + 1;
                    tempSEI.m_dpbOutputDuDelayPresentFlag = false;
                    tempSEI.m_picSptDpbOutputDuDelay = picSptDpbOutputDuDelay;

                    AccessUnit::iterator it;
                    // Insert the first one in the right location, before the first slice
                    if (i == 0)
                    {
                        // Insert before the first slice.
                        m_seiWriter.writeSEImessage(onalu.m_Bitstream, tempSEI, pcSlice->getSPS());
                        writeRBSPTrailingBits(onalu.m_Bitstream);

                        UInt seiPositionInAu = xGetFirstSeiLocation(accessUnit);
                        UInt offsetPosition = bActiveParameterSetSEIPresentInAU
                            + bBufferingPeriodSEIPresentInAU
                            + bPictureTimingSEIPresentInAU;          // Insert DU info SEI after APS, BP and PT SEI
                        for (j = 0, it = accessUnit.begin(); j < seiPositionInAu + offsetPosition; j++)
                        {
                            it++;
                        }

                        accessUnit.insert(it, new NALUnitEBSP(onalu));
                    }
                    else
                    {
                        Int ctr;
                        // For the second decoding unit onwards we know how many NALUs are present
                        for (ctr = 0, it = accessUnit.begin(); it != accessUnit.end(); it++)
                        {
                            if (ctr == accumNalsDU[i - 1])
                            {
                                // Insert before the first slice.
                                m_seiWriter.writeSEImessage(onalu.m_Bitstream, tempSEI, pcSlice->getSPS());
                                writeRBSPTrailingBits(onalu.m_Bitstream);

                                accessUnit.insert(it, new NALUnitEBSP(onalu));
                                break;
                            }
                            if ((*it)->m_nalUnitType != NAL_UNIT_PREFIX_SEI && (*it)->m_nalUnitType != NAL_UNIT_SUFFIX_SEI)
                            {
                                ctr++;
                            }
                        }
                    }
                }
            }
        }

        bActiveParameterSetSEIPresentInAU = false;
        bBufferingPeriodSEIPresentInAU    = false;
        bPictureTimingSEIPresentInAU      = false;
        bNestedBufferingPeriodSEIPresentInAU = false;
        iNumPicCoded++;
        m_totalCoded++;

        if (m_pcCfg->getLogLevel() >= X265_LOG_DEBUG)
        {
            /* logging: insert a newline at end of picture period */
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        delete[] pcSubstreamsOut;
    }

    delete pcBitstreamRedirect;

    if (accumBitsDU != NULL) delete [] accumBitsDU;
    if (accumNalsDU != NULL) delete [] accumNalsDU;

    if (m_pcCfg->getUseRateCtrl())
    {
        m_pcRateCtrl->destroyRCGOP();
    }

    assert(iNumPicCoded == iNumPicRcvd);
}

// This is a function that
// determines what Reference Picture Set to use
// for a specific slice (with POC = POCCurr)
Void TEncGOP::selectReferencePictureSet(TComSlice* slice, Int POCCurr, Int GOPid)
{
    slice->setRPSidx(GOPid);
    UInt intraPeriod = m_pcCfg->getIntraPeriod();
    Int gopSize = m_pcCfg->getGOPSize();

    for (Int extraNum = gopSize; extraNum < m_pcCfg->getExtraRPSs() + gopSize; extraNum++)
    {
        if (intraPeriod > 0 && m_pcCfg->getDecodingRefreshType() > 0)
        {
            Int POCIndex = POCCurr % intraPeriod;
            if (POCIndex == 0)
            {
                POCIndex = intraPeriod;
            }
            if (POCIndex == m_pcCfg->getGOPEntry(extraNum).m_POC)
            {
                slice->setRPSidx(extraNum);
            }
        }
        else
        {
            if (POCCurr == m_pcCfg->getGOPEntry(extraNum).m_POC)
            {
                slice->setRPSidx(extraNum);
            }
        }
    }

    slice->setRPS(getSPS()->getRPSList()->getReferencePictureSet(slice->getRPSidx()));
    slice->getRPS()->setNumberOfPictures(slice->getRPS()->getNumberOfNegativePictures() + slice->getRPS()->getNumberOfPositivePictures());
}

Int TEncGOP::getReferencePictureSetIdxForSOP(TComSlice* slice, Int POCCurr, Int GOPid)
{
    int rpsIdx = GOPid;
    UInt intraPeriod = m_pcCfg->getIntraPeriod();
    Int gopSize = m_pcCfg->getGOPSize();

    for (Int extraNum = gopSize; extraNum < m_pcCfg->getExtraRPSs() + gopSize; extraNum++)
    {
        if (intraPeriod > 0 && m_pcCfg->getDecodingRefreshType() > 0)
        {
            Int POCIndex = POCCurr % intraPeriod;
            if (POCIndex == 0)
            {
                POCIndex = intraPeriod;
            }
            if (POCIndex == m_pcCfg->getGOPEntry(extraNum).m_POC)
            {
                rpsIdx = extraNum;
            }
        }
        else
        {
            if (POCCurr == m_pcCfg->getGOPEntry(extraNum).m_POC)
            {
                rpsIdx = extraNum;
            }
        }
    }

    return rpsIdx;
}

// ====================================================================================================================
// Protected member functions
// ====================================================================================================================

#if VERBOSE_RATE
static const Char* nalUnitTypeToString(NalUnitType type)
{
    switch (type)
    {
    case NAL_UNIT_CODED_SLICE_TRAIL_R:    return "TRAIL_R";
    case NAL_UNIT_CODED_SLICE_TRAIL_N:    return "TRAIL_N";
    case NAL_UNIT_CODED_SLICE_TLA_R:      return "TLA_R";
    case NAL_UNIT_CODED_SLICE_TSA_N:      return "TSA_N";
    case NAL_UNIT_CODED_SLICE_STSA_R:     return "STSA_R";
    case NAL_UNIT_CODED_SLICE_STSA_N:     return "STSA_N";
    case NAL_UNIT_CODED_SLICE_BLA_W_LP:   return "BLA_W_LP";
    case NAL_UNIT_CODED_SLICE_BLA_W_RADL: return "BLA_W_RADL";
    case NAL_UNIT_CODED_SLICE_BLA_N_LP:   return "BLA_N_LP";
    case NAL_UNIT_CODED_SLICE_IDR_W_RADL: return "IDR_W_RADL";
    case NAL_UNIT_CODED_SLICE_IDR_N_LP:   return "IDR_N_LP";
    case NAL_UNIT_CODED_SLICE_CRA:        return "CRA";
    case NAL_UNIT_CODED_SLICE_RADL_R:     return "RADL_R";
    case NAL_UNIT_CODED_SLICE_RASL_R:     return "RASL_R";
    case NAL_UNIT_VPS:                    return "VPS";
    case NAL_UNIT_SPS:                    return "SPS";
    case NAL_UNIT_PPS:                    return "PPS";
    case NAL_UNIT_ACCESS_UNIT_DELIMITER:  return "AUD";
    case NAL_UNIT_EOS:                    return "EOS";
    case NAL_UNIT_EOB:                    return "EOB";
    case NAL_UNIT_FILLER_DATA:            return "FILLER";
    case NAL_UNIT_PREFIX_SEI:             return "SEI";
    case NAL_UNIT_SUFFIX_SEI:             return "SEI";
    default:                              return "UNK";
    }
}
#endif // if VERBOSE_RATE

// TODO:
//   1 - as a performance optimization, if we're not reporting PSNR we do not have to measure PSNR
//       (we do not yet have a switch to disable PSNR reporting)
//   2 - it would be better to accumulate SSD of each CTU at the end of processCTU() while it is cache-hot
//       in fact, we almost certainly are already measuring the CTU distortion and not accumulating it
static UInt64 computeSSD(Pel *pOrg, Pel *pRec, Int iStride, Int iWidth, Int iHeight)
{
    UInt64 uiSSD = 0;

    if ((iWidth | iHeight) & 3)
    {
        /* Slow Path */
        for (Int y = 0; y < iHeight; y++)
        {
            for (Int x = 0; x < iWidth; x++)
            {
                Int iDiff = (Int)(pOrg[x] - pRec[x]);
                uiSSD += iDiff * iDiff;
            }

            pOrg += iStride;
            pRec += iStride;
        }
        return uiSSD;
    }
    Int y = 0;
    /* Consume Y in chunks of 64 */
    for (; y + 64 <= iHeight ; y += 64)
    {
        Int x = 0;
        for (; x + 64 <= iWidth ; x += 64)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_64x64]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        for (; x + 16 <= iWidth ; x += 16)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_16x64]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        for (; x + 4 <= iWidth ; x += 4)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_4x64]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        pOrg += iStride * 64;
        pRec += iStride * 64;
    }
    /* Consume Y in chunks of 16 */
    for (;y + 16 <= iHeight ; y += 16)
    {
        Int x = 0;
        for (; x + 64 <= iWidth ; x += 64)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_64x16]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        for (; x + 16 <= iWidth ; x += 16)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_16x16]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        for (; x + 4 <= iWidth ; x += 4)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_4x16]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        pOrg += iStride * 16;
        pRec += iStride * 16;
    }
    /* Consume Y in chunks of 4 */
    for (;y + 4 <= iHeight ; y += 4)
    {
        Int x = 0;
        for (; x + 64 <= iWidth ; x += 64)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_64x4]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        for (; x + 16 <= iWidth ; x += 16)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_16x4]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        for (; x + 4 <= iWidth ; x += 4)
            uiSSD += x265::primitives.sse_pp[x265::PARTITION_4x4]((pixel *)pOrg + x, (intptr_t)iStride, (pixel *)pRec + x, iStride);
        pOrg += iStride * 4;
        pRec += iStride * 4;
    }
    return uiSSD;
}

Void TEncGOP::xCalculateAddPSNR(TComPic* pcPic, TComPicYuv* pcPicD, const AccessUnit& accessUnit)
{
    //===== calculate PSNR =====
    Int iStride = pcPicD->getStride();
    Int iWidth  = pcPicD->getWidth() - m_pcEncTop->getPad(0);
    Int iHeight = pcPicD->getHeight() - m_pcEncTop->getPad(1);
    Int iSize = iWidth * iHeight;

    UInt64 uiSSDY = computeSSD(pcPic->getPicYuvOrg()->getLumaAddr(), pcPicD->getLumaAddr(), iStride, iWidth, iHeight);

    iHeight >>= 1;
    iWidth  >>= 1;
    iStride = pcPicD->getCStride();

    UInt64 uiSSDU = computeSSD(pcPic->getPicYuvOrg()->getCbAddr(), pcPicD->getCbAddr(), iStride, iWidth, iHeight);
    UInt64 uiSSDV = computeSSD(pcPic->getPicYuvOrg()->getCrAddr(), pcPicD->getCrAddr(), iStride, iWidth, iHeight);

    Int maxvalY = 255 << (g_bitDepthY - 8);
    Int maxvalC = 255 << (g_bitDepthC - 8);
    Double fRefValueY = (Double)maxvalY * maxvalY * iSize;
    Double fRefValueC = (Double)maxvalC * maxvalC * iSize / 4.0;
    Double dYPSNR = (uiSSDY ? 10.0 * log10(fRefValueY / (Double)uiSSDY) : 99.99);
    Double dUPSNR = (uiSSDU ? 10.0 * log10(fRefValueC / (Double)uiSSDU) : 99.99);
    Double dVPSNR = (uiSSDV ? 10.0 * log10(fRefValueC / (Double)uiSSDV) : 99.99);

    /* calculate the size of the access unit, excluding:
     *  - any AnnexB contributions (start_code_prefix, zero_byte, etc.,)
     *  - SEI NAL units
     */
    UInt numRBSPBytes = 0;
    for (AccessUnit::const_iterator it = accessUnit.begin(); it != accessUnit.end(); it++)
    {
        UInt numRBSPBytes_nal = UInt((*it)->m_nalUnitData.str().size());
#if VERBOSE_RATE
        printf("*** %6s numBytesInNALunit: %u\n", nalUnitTypeToString((*it)->m_nalUnitType), numRBSPBytes_nal);
#endif
        if ((*it)->m_nalUnitType != NAL_UNIT_PREFIX_SEI && (*it)->m_nalUnitType != NAL_UNIT_SUFFIX_SEI)
        {
            numRBSPBytes += numRBSPBytes_nal;
        }
    }
    UInt uibits = numRBSPBytes * 8;

    /* Acquire encoder global lock to accumulate statistics and print debug info to console */
    x265::ScopedLock(m_pcEncTop->m_statLock);
    m_pcEncTop->m_vRVM_RP.push_back(uibits);

    //===== add PSNR =====
    m_pcEncTop->m_gcAnalyzeAll.addResult(dYPSNR, dUPSNR, dVPSNR, (Double)uibits);
    TComSlice*  pcSlice = pcPic->getSlice();
    if (pcSlice->isIntra())
    {
        m_pcEncTop->m_gcAnalyzeI.addResult(dYPSNR, dUPSNR, dVPSNR, (Double)uibits);
    }
    if (pcSlice->isInterP())
    {
        m_pcEncTop->m_gcAnalyzeP.addResult(dYPSNR, dUPSNR, dVPSNR, (Double)uibits);
    }
    if (pcSlice->isInterB())
    {
        m_pcEncTop->m_gcAnalyzeB.addResult(dYPSNR, dUPSNR, dVPSNR, (Double)uibits);
    }

    if (m_pcCfg->getLogLevel() < X265_LOG_DEBUG)
        return;

    Char c = (pcSlice->isIntra() ? 'I' : pcSlice->isInterP() ? 'P' : 'B');
    
    if (!pcSlice->isReferenced())
        c += 32;  // lower case if unreferenced

    printf("\rPOC %4d TId: %1d ( %c-SLICE, nQP %d QP %d ) %10d bits",
           pcSlice->getPOC(),
           pcSlice->getTLayer(),
           c,
           pcSlice->getSliceQpBase(),
           pcSlice->getSliceQp(),
           uibits);

    printf(" [Y:%6.2lf U:%6.2lf V:%6.2lf]", dYPSNR, dUPSNR, dVPSNR);

    if (pcSlice->isIntra())
        return;
    Int numLists = pcSlice->isInterP() ? 1 : 2;
    for (Int iRefList = 0; iRefList < numLists; iRefList++)
    {
        printf(" [L%d ", iRefList);
        for (Int iRefIndex = 0; iRefIndex < pcSlice->getNumRefIdx(RefPicList(iRefList)); iRefIndex++)
        {
            printf("%d ", pcSlice->getRefPOC(RefPicList(iRefList), iRefIndex) - pcSlice->getLastIDR());
        }

        printf("]");
    }
}

/** Function for deciding the nal_unit_type.
 * \param pocCurr POC of the current picture
 * \returns the nal unit type of the picture
 * This function checks the configuration and returns the appropriate nal_unit_type for the picture.
 */
NalUnitType TEncGOP::getNalUnitType(Int pocCurr, Int lastIDR)
{
    if (pocCurr == 0)
    {
        return NAL_UNIT_CODED_SLICE_IDR_W_RADL;
    }
    if (pocCurr % m_pcCfg->getIntraPeriod() == 0)
    {
        if (m_pcCfg->getDecodingRefreshType() == 1)
        {
            return NAL_UNIT_CODED_SLICE_CRA;
        }
        else if (m_pcCfg->getDecodingRefreshType() == 2)
        {
            return NAL_UNIT_CODED_SLICE_IDR_W_RADL;
        }
    }
    if (m_pocCRA > 0)
    {
        if (pocCurr < m_pocCRA)
        {
            // All leading pictures are being marked as TFD pictures here since current encoder uses all
            // reference pictures while encoding leading pictures. An encoder can ensure that a leading
            // picture can be still decodable when random accessing to a CRA/CRANT/BLA/BLANT picture by
            // controlling the reference pictures used for encoding that leading picture. Such a leading
            // picture need not be marked as a TFD picture.
            return NAL_UNIT_CODED_SLICE_RASL_R;
        }
    }
    if (lastIDR > 0)
    {
        if (pocCurr < lastIDR)
        {
            return NAL_UNIT_CODED_SLICE_RADL_R;
        }
    }
    return NAL_UNIT_CODED_SLICE_TRAIL_R;
}

/** Attaches the input bitstream to the stream in the output NAL unit
    Updates rNalu to contain concatenated bitstream. rpcBitstreamRedirect is cleared at the end of this function call.
 *  \param codedSliceData contains the coded slice data (bitstream) to be concatenated to rNalu
 *  \param rNalu          target NAL unit
 */
Void TEncGOP::xAttachSliceDataToNalUnit(TEncEntropy* pcEntropyCoder, OutputNALUnit& rNalu, TComOutputBitstream*& codedSliceData)
{
    // Byte-align
    rNalu.m_Bitstream.writeByteAlignment(); // Slice header byte-alignment

    // Perform bitstream concatenation
    if (codedSliceData->getNumberOfWrittenBits() > 0)
    {
        rNalu.m_Bitstream.addSubstream(codedSliceData);
    }

    pcEntropyCoder->setBitstream(&rNalu.m_Bitstream);

    codedSliceData->clear();
}

// Function will arrange the long-term pictures in the decreasing order of poc_lsb_lt,
// and among the pictures with the same lsb, it arranges them in increasing delta_poc_msb_cycle_lt value
Void TEncGOP::arrangeLongtermPicturesInRPS(TComSlice *pcSlice)
{
    TComReferencePictureSet *rps = pcSlice->getRPS();

    if (!rps->getNumberOfLongtermPictures())
    {
        return;
    }

    // Arrange long-term reference pictures in the correct order of LSB and MSB,
    // and assign values for pocLSBLT and MSB present flag
    Int longtermPicsPoc[MAX_NUM_REF_PICS], longtermPicsLSB[MAX_NUM_REF_PICS], indices[MAX_NUM_REF_PICS];
    Int longtermPicsMSB[MAX_NUM_REF_PICS];
    Bool mSBPresentFlag[MAX_NUM_REF_PICS];
    ::memset(longtermPicsPoc, 0, sizeof(longtermPicsPoc));  // Store POC values of LTRP
    ::memset(longtermPicsLSB, 0, sizeof(longtermPicsLSB));  // Store POC LSB values of LTRP
    ::memset(longtermPicsMSB, 0, sizeof(longtermPicsMSB));  // Store POC LSB values of LTRP
    ::memset(indices, 0, sizeof(indices));                  // Indices to aid in tracking sorted LTRPs
    ::memset(mSBPresentFlag, 0, sizeof(mSBPresentFlag));    // Indicate if MSB needs to be present

    // Get the long-term reference pictures
    Int offset = rps->getNumberOfNegativePictures() + rps->getNumberOfPositivePictures();
    Int i, ctr = 0;
    Int maxPicOrderCntLSB = 1 << pcSlice->getSPS()->getBitsForPOC();
    for (i = rps->getNumberOfPictures() - 1; i >= offset; i--, ctr++)
    {
        longtermPicsPoc[ctr] = rps->getPOC(i);                              // LTRP POC
        longtermPicsLSB[ctr] = getLSB(longtermPicsPoc[ctr], maxPicOrderCntLSB); // LTRP POC LSB
        indices[ctr] = i;
        longtermPicsMSB[ctr] = longtermPicsPoc[ctr] - longtermPicsLSB[ctr];
    }

    Int numLongPics = rps->getNumberOfLongtermPictures();
    assert(ctr == numLongPics);

    // Arrange pictures in decreasing order of MSB;
    for (i = 0; i < numLongPics; i++)
    {
        for (Int j = 0; j < numLongPics - 1; j++)
        {
            if (longtermPicsMSB[j] < longtermPicsMSB[j + 1])
            {
                std::swap(longtermPicsPoc[j], longtermPicsPoc[j + 1]);
                std::swap(longtermPicsLSB[j], longtermPicsLSB[j + 1]);
                std::swap(longtermPicsMSB[j], longtermPicsMSB[j + 1]);
                std::swap(indices[j], indices[j + 1]);
            }
        }
    }

    for (i = 0; i < numLongPics; i++)
    {
        // Check if MSB present flag should be enabled.
        // Check if the buffer contains any pictures that have the same LSB.
        TComList<TComPic*>::iterator iterPic = m_cListPic.begin();
        TComPic* pcPic;
        while (iterPic != m_cListPic.end())
        {
            pcPic = *iterPic;
            if ((getLSB(pcPic->getPOC(), maxPicOrderCntLSB) == longtermPicsLSB[i])   && // Same LSB
                (pcPic->getSlice()->isReferenced()) &&                                  // Reference picture
                (pcPic->getPOC() != longtermPicsPoc[i]))                                // Not the LTRP itself
            {
                mSBPresentFlag[i] = true;
                break;
            }
            iterPic++;
        }
    }

    // tempArray for usedByCurr flag
    Bool tempArray[MAX_NUM_REF_PICS];
    ::memset(tempArray, 0, sizeof(tempArray));
    for (i = 0; i < numLongPics; i++)
    {
        tempArray[i] = rps->getUsed(indices[i]);
    }

    // Now write the final values;
    ctr = 0;
    Int currMSB = 0, currLSB = 0;
    // currPicPoc = currMSB + currLSB
    currLSB = getLSB(pcSlice->getPOC(), maxPicOrderCntLSB);
    currMSB = pcSlice->getPOC() - currLSB;

    for (i = rps->getNumberOfPictures() - 1; i >= offset; i--, ctr++)
    {
        rps->setPOC(i, longtermPicsPoc[ctr]);
        rps->setDeltaPOC(i, -pcSlice->getPOC() + longtermPicsPoc[ctr]);
        rps->setUsed(i, tempArray[ctr]);
        rps->setPocLSBLT(i, longtermPicsLSB[ctr]);
        rps->setDeltaPocMSBCycleLT(i, (currMSB - (longtermPicsPoc[ctr] - longtermPicsLSB[ctr])) / maxPicOrderCntLSB);
        rps->setDeltaPocMSBPresentFlag(i, mSBPresentFlag[ctr]);

        assert(rps->getDeltaPocMSBCycleLT(i) >= 0); // Non-negative value
    }

    for (i = rps->getNumberOfPictures() - 1, ctr = 1; i >= offset; i--, ctr++)
    {
        for (Int j = rps->getNumberOfPictures() - 1 - ctr; j >= offset; j--)
        {
            // Here at the encoder we know that we have set the full POC value for the LTRPs, hence we
            // don't have to check the MSB present flag values for this constraint.
            assert(rps->getPOC(i) != rps->getPOC(j)); // If assert fails, LTRP entry repeated in RPS!!!
        }
    }
}

/** Function for finding the position to insert the first of APS and non-nested BP, PT, DU info SEI messages.
 * \param accessUnit Access Unit of the current picture
 * This function finds the position to insert the first of APS and non-nested BP, PT, DU info SEI messages.
 */
Int TEncGOP::xGetFirstSeiLocation(AccessUnit &accessUnit)
{
    // Find the location of the first SEI message
    AccessUnit::iterator it;
    Int seiStartPos = 0;

    for (it = accessUnit.begin(); it != accessUnit.end(); it++, seiStartPos++)
    {
        if ((*it)->isSei() || (*it)->isVcl())
        {
            break;
        }
    }

//  assert(it != accessUnit.end());  // Triggers with some legit configurations
    return seiStartPos;
}

//! \}
