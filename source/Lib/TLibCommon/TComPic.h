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

/** \file     TComPic.h
    \brief    picture class (header)
*/

#ifndef __TCOMPIC__
#define __TCOMPIC__

// Include files
#include "CommonDef.h"
#include "TComPicSym.h"
#include "TComPicYuv.h"

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// picture class (symbol + YUV buffers)
class TComPic
{
private:

    TComPicSym*           m_pcPicSym;             // Symbols
    TComPicYuv*           m_pcPicYuvOrg;
    TComPicYuv*           m_pcPicYuvRec;

    Window                m_conformanceWindow;
    Window                m_defaultDisplayWindow;

    UInt                  m_uiTLayer;             // Temporal layer
    Bool                  m_bUsedByCurr;          // Used by current picture
    Bool                  m_bIsLongTerm;          // IS long term picture
    Bool                  m_bCheckLTMSB;

public:

    TComPic();
    virtual ~TComPic();

    Void          create(Int width, Int height, UInt uiMaxWidth, UInt uiMaxHeight, UInt uiMaxDepth, Window &conformanceWindow, Window &defaultDisplayWindow);

    virtual Void  destroy();

    UInt          getTLayer()               { return m_uiTLayer; }

    Void          setTLayer(UInt uiTLayer)  { m_uiTLayer = uiTLayer; }

    Bool          getUsedByCurr()           { return m_bUsedByCurr; }

    Void          setUsedByCurr(Bool bUsed) { m_bUsedByCurr = bUsed; }

    Bool          getIsLongTerm()           { return m_bIsLongTerm; }

    Void          setIsLongTerm(Bool lt)    { m_bIsLongTerm = lt; }

    Void          setCheckLTMSBPresent(Bool b) { m_bCheckLTMSB = b; }

    Bool          getCheckLTMSBPresent()  { return m_bCheckLTMSB; }

    TComPicSym*   getPicSym()             { return m_pcPicSym; }

    TComSlice*    getSlice()              { return m_pcPicSym->getSlice(); }

    Int           getPOC()                { return m_pcPicSym->getSlice()->getPOC(); }

    TComDataCU*   getCU(UInt cuAddr)    { return m_pcPicSym->getCU(cuAddr); }

    TComPicYuv*   getPicYuvOrg()          { return m_pcPicYuvOrg; }

    TComPicYuv*   getPicYuvRec()          { return m_pcPicYuvRec; }

    UInt          getNumCUsInFrame()      { return m_pcPicSym->getNumberOfCUsInFrame(); }

    UInt          getNumPartInWidth()     { return m_pcPicSym->getNumPartInWidth(); }

    UInt          getNumPartInHeight()    { return m_pcPicSym->getNumPartInHeight(); }

    UInt          getNumPartInCU()        { return m_pcPicSym->getNumPartition(); }

    UInt          getFrameWidthInCU()     { return m_pcPicSym->getFrameWidthInCU(); }

    UInt          getFrameHeightInCU()    { return m_pcPicSym->getFrameHeightInCU(); }

    UInt          getMinCUWidth()         { return m_pcPicSym->getMinCUWidth(); }

    UInt          getMinCUHeight()        { return m_pcPicSym->getMinCUHeight(); }

    UInt          getParPelX(UChar uhPartIdx) { return getParPelX(uhPartIdx); }

    UInt          getParPelY(UChar uhPartIdx) { return getParPelX(uhPartIdx); }

    Int           getStride()             { return m_pcPicYuvRec->getStride(); }

    Int           getCStride()            { return m_pcPicYuvRec->getCStride(); }

    Void          compressMotion();

    Window&       getConformanceWindow()  { return m_conformanceWindow; }

    Window&       getDefDisplayWindow()   { return m_defaultDisplayWindow; }

    Void          createNonDBFilterInfo(Int lastSliceCUAddr, Int sliceGranularityDepth, Bool bNDBFilterCrossTileBoundary = true);
    Void          createNonDBFilterInfoLCU(Int sliceID, TComDataCU* cu, UInt startSU, UInt endSU, Int sliceGranularyDepth, UInt picWidth, UInt picHeight);
    Void          destroyNonDBFilterInfo();
}; // END CLASS DEFINITION TComPic

//! \}

#endif // __TCOMPIC__
