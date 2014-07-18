/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Steve Borho <steve@borho.org>
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
#include "TLibCommon/TComSlice.h"

#include "PPA/ppa.h"

#include "encoder.h"
#include "dpb.h"
#include "frameencoder.h"

using namespace x265;

DPB::~DPB()
{
    while (!m_freeList.empty())
    {
        Frame* pic = m_freeList.popFront();
        pic->destroy();
        delete pic;
    }

    while (!m_picList.empty())
    {
        Frame* pic = m_picList.popFront();
        pic->destroy();
        delete pic;
    }

    while (m_picSymFreeList)
    {
        TComPicSym* next = m_picSymFreeList->m_freeListNext;
        m_picSymFreeList->destroy();
        m_picSymFreeList->m_reconPicYuv->destroy();
        delete m_picSymFreeList->m_reconPicYuv;
        delete m_picSymFreeList;
        m_picSymFreeList = next;
    }
}

// move unreferenced pictures from picList to freeList for recycle
void DPB::recycleUnreferenced()
{
    Frame *iterPic = m_picList.first();

    while (iterPic)
    {
        Frame *pic = iterPic;
        iterPic = iterPic->m_next;
        if (pic->getSlice()->m_bReferenced == false && pic->m_countRefEncoders == 0)
        {
            pic->m_reconRowCount.set(0);
            pic->m_bChromaPlanesExtended = false;

            // iterator is invalidated by remove, restart scan
            m_picList.remove(*pic);
            iterPic = m_picList.first();

            m_freeList.pushBack(*pic);
            pic->m_picSym->m_freeListNext = m_picSymFreeList;
            m_picSymFreeList = pic->m_picSym;
            pic->m_picSym = NULL;
            pic->m_reconPicYuv = NULL;
        }
    }
}

void DPB::prepareEncode(Frame *pic)
{
    PPAScopeEvent(DPB_prepareEncode);

    int pocCurr = pic->getSlice()->m_poc;

    m_picList.pushFront(*pic);

    TComSlice* slice = pic->getSlice();
    if (getNalUnitType(pocCurr, m_lastIDR, pic) == NAL_UNIT_CODED_SLICE_IDR_W_RADL ||
        getNalUnitType(pocCurr, m_lastIDR, pic) == NAL_UNIT_CODED_SLICE_IDR_N_LP)
    {
        m_lastIDR = pocCurr;
    }
    slice->m_lastIDR = m_lastIDR;
    slice->m_nalUnitType = getNalUnitType(pocCurr, m_lastIDR, pic);

    // If the slice is un-referenced, change from _R "referenced" to _N "non-referenced" NAL unit type
    if (!slice->m_bReferenced)
    {
        switch (slice->m_nalUnitType)
        {
        case NAL_UNIT_CODED_SLICE_TRAIL_R:
            slice->m_nalUnitType = NAL_UNIT_CODED_SLICE_TRAIL_N;
            break;
        case NAL_UNIT_CODED_SLICE_RADL_R:
            slice->m_nalUnitType = NAL_UNIT_CODED_SLICE_RADL_N;
            break;
        case NAL_UNIT_CODED_SLICE_RASL_R:
            slice->m_nalUnitType = NAL_UNIT_CODED_SLICE_RASL_N;
            break;
        default:
            break;
        }
    }

    // Do decoding refresh marking if any
    decodingRefreshMarking(pocCurr, slice->m_nalUnitType);

    computeRPS(pocCurr, slice->isIRAP(), &slice->m_rps, slice->m_sps->maxDecPicBuffering);

    // Mark pictures in m_piclist as unreferenced if they are not included in RPS
    applyReferencePictureSet(&slice->m_rps, pocCurr);

    slice->m_numRefIdx[0] = X265_MIN(m_maxRefL0, slice->m_rps.m_numberOfNegativePictures); // Ensuring L0 contains just the -ve POC
    slice->m_numRefIdx[1] = X265_MIN(m_maxRefL1, slice->m_rps.m_numberOfPositivePictures);

    slice->setRefPicList(m_picList);

    // Slice type refinement:  TODO: does this ever happen?
    if ((slice->m_sliceType == B_SLICE) && (slice->m_numRefIdx[1] == 0))
        slice->m_sliceType = P_SLICE;

    if (slice->m_sliceType == B_SLICE)
    {
        // TODO: Can we estimate this from lookahead?
        slice->m_colFromL0Flag = 0;

        bool bLowDelay = true;
        int curPOC = slice->m_poc;
        int refIdx = 0;

        for (refIdx = 0; refIdx < slice->m_numRefIdx[0] && bLowDelay; refIdx++)
            if (slice->getRefPic(REF_PIC_LIST_0, refIdx)->getPOC() > curPOC)
                bLowDelay = false;

        for (refIdx = 0; refIdx < slice->m_numRefIdx[1] && bLowDelay; refIdx++)
            if (slice->getRefPic(REF_PIC_LIST_1, refIdx)->getPOC() > curPOC)
                bLowDelay = false;

        slice->m_bCheckLDC = bLowDelay;
    }
    else
        slice->m_bCheckLDC = true;

    slice->setRefPOCList();

    bool bGPBcheck = false;
    if (slice->m_sliceType == B_SLICE)
    {
        if (slice->m_numRefIdx[0] == slice->m_numRefIdx[1])
        {
            bGPBcheck = true;
            for (int i = 0; i < slice->m_numRefIdx[1]; i++)
            {
                if (slice->getRefPOC(REF_PIC_LIST_1, i) != slice->getRefPOC(REF_PIC_LIST_0, i))
                {
                    bGPBcheck = false;
                    break;
                }
            }
        }
    }

    slice->m_bLMvdL1Zero = bGPBcheck;

    /* Increment reference count of all motion-referenced frames.  This serves two purposes. First
     * it prevents the frame from being recycled, and second the referenced frames know how many
     * other FrameEncoders are using them for motion reference */
    int numPredDir = slice->isInterP() ? 1 : slice->isInterB() ? 2 : 0;
    for (int l = 0; l < numPredDir; l++)
    {
        for (int ref = 0; ref < slice->m_numRefIdx[l]; ref++)
        {
            Frame *refpic = slice->getRefPic(l, ref);
            ATOMIC_INC(&refpic->m_countRefEncoders);
        }
    }
}

void DPB::computeRPS(int curPoc, bool isRAP, RPS * rps, unsigned int maxDecPicBuffer)
{
    unsigned int poci = 0, numNeg = 0, numPos = 0;

    Frame* iterPic = m_picList.first();

    while (iterPic && (poci < maxDecPicBuffer - 1))
    {
        if ((iterPic->getPOC() != curPoc) && (iterPic->getSlice()->m_bReferenced))
        {
            rps->m_POC[poci] = iterPic->getPOC();
            rps->m_deltaPOC[poci] = rps->m_POC[poci] - curPoc;
            (rps->m_deltaPOC[poci] < 0) ? numNeg++ : numPos++;
            rps->m_used[poci] = !isRAP;
            poci++;
        }
        iterPic = iterPic->m_next;
    }

    rps->m_numberOfPictures = poci;
    rps->m_numberOfPositivePictures = numPos;
    rps->m_numberOfNegativePictures = numNeg;

    rps->sortDeltaPOC();
}

/** Function for marking the reference pictures when an IDR/CRA/CRANT/BLA/BLANT is encountered.
 * \param pocCRA POC of the CRA/CRANT/BLA/BLANT picture
 * \param bRefreshPending flag indicating if a deferred decoding refresh is pending
 * \param picList reference to the reference picture list
 * This function marks the reference pictures as "unused for reference" in the following conditions.
 * If the nal_unit_type is IDR/BLA/BLANT, all pictures in the reference picture list
 * are marked as "unused for reference"
 *    If the nal_unit_type is BLA/BLANT, set the pocCRA to the temporal reference of the current picture.
 * Otherwise
 *    If the bRefreshPending flag is true (a deferred decoding refresh is pending) and the current
 *    temporal reference is greater than the temporal reference of the latest CRA/CRANT/BLA/BLANT picture (pocCRA),
 *    mark all reference pictures except the latest CRA/CRANT/BLA/BLANT picture as "unused for reference" and set
 *    the bRefreshPending flag to false.
 *    If the nal_unit_type is CRA/CRANT, set the bRefreshPending flag to true and pocCRA to the temporal
 *    reference of the current picture.
 * Note that the current picture is already placed in the reference list and its marking is not changed.
 * If the current picture has a nal_ref_idc that is not 0, it will remain marked as "used for reference".
 */
void DPB::decodingRefreshMarking(int pocCurr, NalUnitType nalUnitType)
{
    if (nalUnitType == NAL_UNIT_CODED_SLICE_BLA_W_LP
        || nalUnitType == NAL_UNIT_CODED_SLICE_BLA_W_RADL
        || nalUnitType == NAL_UNIT_CODED_SLICE_BLA_N_LP
        || nalUnitType == NAL_UNIT_CODED_SLICE_IDR_W_RADL
        || nalUnitType == NAL_UNIT_CODED_SLICE_IDR_N_LP) // IDR or BLA picture
    {
        // mark all pictures as not used for reference
        Frame* iterPic = m_picList.first();
        while (iterPic)
        {
            if (iterPic->getPOC() != pocCurr)
                iterPic->getSlice()->m_bReferenced = false;
            iterPic = iterPic->m_next;
        }

        if (nalUnitType == NAL_UNIT_CODED_SLICE_BLA_W_LP
            || nalUnitType == NAL_UNIT_CODED_SLICE_BLA_W_RADL
            || nalUnitType == NAL_UNIT_CODED_SLICE_BLA_N_LP)
        {
            m_pocCRA = pocCurr;
        }
    }
    else // CRA or No DR
    {
        if (m_bRefreshPending == true && pocCurr > m_pocCRA) // CRA reference marking pending
        {
            Frame* iterPic = m_picList.first();
            while (iterPic)
            {
                if (iterPic->getPOC() != pocCurr && iterPic->getPOC() != m_pocCRA)
                    iterPic->getSlice()->m_bReferenced = false;
                iterPic = iterPic->m_next;
            }

            m_bRefreshPending = false;
        }
        if (nalUnitType == NAL_UNIT_CODED_SLICE_CRA) // CRA picture found
        {
            m_bRefreshPending = true;
            m_pocCRA = pocCurr;
        }
    }
}

/** Function for applying picture marking based on the Reference Picture Set */
void DPB::applyReferencePictureSet(RPS *rps, int curPoc)
{
    Frame* outPic;
    int i, isReference;

    // loop through all pictures in the reference picture buffer
    Frame* iterPic = m_picList.first();

    while (iterPic)
    {
        outPic = iterPic;
        iterPic = iterPic->m_next;

        if (!outPic->getSlice()->m_bReferenced)
            continue;

        isReference = 0;
        // loop through all pictures in the Reference Picture Set
        // to see if the picture should be kept as reference picture
        for (i = 0; i < rps->m_numberOfPositivePictures + rps->m_numberOfNegativePictures; i++)
        {
            if (outPic->getPicSym()->getSlice()->m_poc == curPoc + rps->m_deltaPOC[i])
                isReference = 1;
        }

        // mark the picture as "unused for reference" if it is not in
        // the Reference Picture Set
        if (outPic->getPicSym()->getSlice()->m_poc != curPoc && isReference == 0)
            outPic->getSlice()->m_bReferenced = false;
    }
}

/** Function for deciding the nal_unit_type.
 * \param pocCurr POC of the current picture
 * \returns the nal unit type of the picture
 * This function checks the configuration and returns the appropriate nal_unit_type for the picture.
 */
NalUnitType DPB::getNalUnitType(int curPOC, int lastIDR, Frame* pic)
{
    if (curPOC == 0)
        return NAL_UNIT_CODED_SLICE_IDR_W_RADL;

    if (pic->m_lowres.bKeyframe)
    {
        if (m_bOpenGOP)
            return NAL_UNIT_CODED_SLICE_CRA;
        else
            return NAL_UNIT_CODED_SLICE_IDR_W_RADL;
    }
    if (m_pocCRA > 0)
    {
        if (curPOC < m_pocCRA)
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
        if (curPOC < lastIDR)
            return NAL_UNIT_CODED_SLICE_RADL_R;
    }
    return NAL_UNIT_CODED_SLICE_TRAIL_R;
}
