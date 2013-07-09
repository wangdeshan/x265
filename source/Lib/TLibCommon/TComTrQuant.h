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

/** \file     TComTrQuant.h
    \brief    transform and quantization class (header)
*/

#ifndef __TCOMTRQUANT__
#define __TCOMTRQUANT__

#include "CommonDef.h"
#include "TComYuv.h"
#include "TComDataCU.h"
#include "ContextTables.h"

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Constants
// ====================================================================================================================

#define QP_BITS                 15

// ====================================================================================================================
// Type definition
// ====================================================================================================================

typedef struct
{
    Int significantCoeffGroupBits[NUM_SIG_CG_FLAG_CTX][2];
    Int significantBits[NUM_SIG_FLAG_CTX][2];
    Int lastXBits[32];
    Int lastYBits[32];
    Int m_greaterOneBits[NUM_ONE_FLAG_CTX][2];
    Int m_levelAbsBits[NUM_ABS_FLAG_CTX][2];

    Int blockCbpBits[3 * NUM_QT_CBF_CTX][2];
    Int blockRootCbpBits[4][2];
} estBitsSbacStruct;

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// QP class
class QpParam
{
public:

    QpParam();

    Int m_iQP;
    Int m_iPer;
    Int m_iRem;

public:

    Int m_iBits;

    Void setQpParam(Int qpScaled)
    {
        m_iQP   = qpScaled;
        m_iPer  = qpScaled / 6;
        m_iRem  = qpScaled % 6;
        m_iBits = QP_BITS + m_iPer;
    }

    Void clear()
    {
        m_iQP   = 0;
        m_iPer  = 0;
        m_iRem  = 0;
        m_iBits = 0;
    }

    Int per()   const { return m_iPer; }

    Int rem()   const { return m_iRem; }

    Int bits()  const { return m_iBits; }

    Int qp() { return m_iQP; }
}; // END CLASS DEFINITION QpParam

/// transform and quantization class
class TComTrQuant
{
public:

    TComTrQuant();
    ~TComTrQuant();

    // initialize class
    Void init(UInt uiMaxTrSize,
              Bool useRDOQ = false,
              Bool useRDOQTS = false,
              Bool bEnc = false,
              Bool useTransformSkipFast = false,
              Bool bUseAdaptQpSelect = false);

    // transform & inverse transform functions
    UInt transformNxN(TComDataCU * pcCU,
                      Short *      pcResidual,
                      UInt         uiStride,
                      TCoeff *     rpcCoeff,
                      Int *        rpcArlCoeff,
                      UInt         uiWidth,
                      UInt         uiHeight,
                      TextType     eTType,
                      UInt         uiAbsPartIdx,
                      Bool         useTransformSkip = false);

    Void invtransformNxN(Bool transQuantBypass, TextType eText, UInt uiMode, Short* rpcResidual, UInt uiStride, TCoeff*   pcCoeff, UInt uiWidth, UInt uiHeight,  Int scalingListType, Bool useTransformSkip = false);
    Void invRecurTransformNxN(TComDataCU* pcCU, UInt uiAbsPartIdx, TextType eTxt, Short* rpcResidual, UInt uiAddr,   UInt uiStride, UInt uiWidth, UInt uiHeight,
                              UInt uiMaxTrMode,  UInt uiTrMode, TCoeff* rpcCoeff);

    // Misc functions
    Void setQPforQuant(Int qpy, TextType eTxtType, Int qpBdOffset, Int chromaQPOffset);

    Void setLambda(Double dLambdaLuma, Double dLambdaChroma) { m_dLambdaLuma = dLambdaLuma; m_dLambdaChroma = dLambdaChroma; }

    Void selectLambda(TextType eTType) { m_dLambda = (eTType == TEXT_LUMA) ? m_dLambdaLuma : m_dLambdaChroma; }

    Void setRDOQOffset(UInt uiRDOQOffset) { m_uiRDOQOffset = uiRDOQOffset; }

    estBitsSbacStruct* m_pcEstBitsSbac;

    static Int      calcPatternSigCtx(const UInt* sigCoeffGroupFlag, UInt posXCG, UInt posYCG, Int width, Int height);

    static Int      getSigCtxInc(Int      patternSigCtx,
                                 UInt     scanIdx,
                                 Int      posX,
                                 Int      posY,
                                 Int      log2BlkSize,
                                 TextType textureType);

    static UInt getSigCoeffGroupCtxInc(const UInt* uiSigCoeffGroupFlag,
                                       const UInt uiCGPosX,
                                       const UInt uiCGPosY,
                                       Int width, Int height);
    Void initScalingList();
    Void destroyScalingList();
    Void setErrScaleCoeff(UInt list, UInt size, UInt qp);
    Double* getErrScaleCoeff(UInt list, UInt size, UInt qp) { return m_errScale[size][list][qp]; }   //!< get Error Scale Coefficent

    Int* getQuantCoeff(UInt list, UInt qp, UInt size) { return m_quantCoef[size][list][qp]; }        //!< get Quant Coefficent

    Int* getDequantCoeff(UInt list, UInt qp, UInt size) { return m_dequantCoef[size][list][qp]; }    //!< get DeQuant Coefficent

    Void setUseScalingList(Bool bUseScalingList) { m_scalingListEnabledFlag = bUseScalingList; }

    Bool getUseScalingList() { return m_scalingListEnabledFlag; }

    Void setFlatScalingList();
    Void xsetFlatScalingList(UInt list, UInt size, UInt qp);
    Void xSetScalingListEnc(TComScalingList *scalingList, UInt list, UInt size, UInt qp);
    Void xSetScalingListDec(TComScalingList *scalingList, UInt list, UInt size, UInt qp);
    Void setScalingList(TComScalingList *scalingList);
    Void setScalingListDec(TComScalingList *scalingList);
    Void processScalingListEnc(Int *coeff, Int *quantcoeff, Int quantScales, UInt height, UInt width, UInt ratio, Int sizuNum, UInt dc);
    Void processScalingListDec(Int *coeff, Int *dequantcoeff, Int invQuantScales, UInt height, UInt width, UInt ratio, Int sizuNum, UInt dc);
    Void initSliceQpDelta();
    Void storeSliceQpNext(TComSlice* pcSlice);
    Void clearSliceARLCnt();
    Int  getQpDelta(Int qp) { return m_qpDelta[qp]; }

    Int*    getSliceNSamples() { return m_sliceNsamples; }

    Double* getSliceSumC()    { return m_sliceSumC; }

protected:

    Int     m_qpDelta[MAX_QP + 1];
    Int     m_sliceNsamples[LEVEL_RANGE + 1];
    Double  m_sliceSumC[LEVEL_RANGE + 1];
    Int*    m_plTempCoeff;

    QpParam  m_cQP;

    Double   m_dLambda;
    Double   m_dLambdaLuma;
    Double   m_dLambdaChroma;

    UInt     m_uiRDOQOffset;
    UInt     m_uiMaxTrSize;
    Bool     m_bEnc;
    Bool     m_useRDOQ;
    Bool     m_useRDOQTS;
    Bool     m_bUseAdaptQpSelect;
    Bool     m_useTransformSkipFast;
    Bool     m_scalingListEnabledFlag;
    Int      *m_quantCoef[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM];     ///< array of quantization matrix coefficient 4x4
    Int      *m_dequantCoef[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM];   ///< array of dequantization matrix coefficient 4x4

    Double   *m_errScale[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM];

private:

    // skipping Transform
    Void xTransformSkip(Int bitDepth, Short* piBlkResi, UInt uiStride, Int* psCoeff, Int width, Int height);

    Void signBitHidingHDQ(TCoeff* pQCoef, TCoeff* pCoef, UInt const *scan, Int* deltaU, Int width, Int height);

    // quantization
    UInt xQuant(TComDataCU * cu,
                Int *        src,
                TCoeff *     dst,
                Int *        arlDes,
                Int          width,
                Int          height,
                TextType     eTType,
                UInt         absPartIdx);

    // RDOQ functions
    UInt xRateDistOptQuant(TComDataCU * cu,
                           Int *        srcCoeff,
                           TCoeff *     dstCoeff,
                           Int *        arlDstCoeff,
                           UInt         width,
                           UInt         height,
                           TextType     eTType,
                           UInt         absPartIdx);

    __inline UInt xGetCodedLevel(Double& rd64CodedCost,
                                 Double& rd64CodedCost0,
                                 Double& rd64CodedCostSig,
                                 Int     lLevelDouble,
                                 UInt    uiMaxAbsLevel,
                                 UShort  ui16CtxNumSig,
                                 UShort  ui16CtxNumOne,
                                 UShort  ui16CtxNumAbs,
                                 UShort  ui16AbsGoRice,
                                 UInt    c1Idx,
                                 UInt    c2Idx,
                                 Int     iQBits,
                                 Double  dTemp,
                                 Bool    bLast) const;

    __inline Double xGetICRateCost(UInt uiAbsLevel,
                                   UShort ui16CtxNumOne,
                                   UShort ui16CtxNumAbs,
                                   UShort ui16AbsGoRice,
                                   UInt   c1Idx,
                                   UInt   c2Idx) const;

    __inline Int xGetICRate(UInt uiAbsLevel,
                            UShort ui16CtxNumOne,
                            UShort ui16CtxNumAbs,
                            UShort ui16AbsGoRice,
                            UInt   c1Idx,
                            UInt   c2Idx) const;

    __inline Double xGetRateLast(UInt uiPosX, UInt uiPosY) const;

    __inline Double xGetRateSigCoeffGroup(UShort uiSignificanceCoeffGroup, UShort ui16CtxNumSig) const { return m_dLambda * m_pcEstBitsSbac->significantCoeffGroupBits[ui16CtxNumSig][uiSignificanceCoeffGroup]; }
    __inline Double xGetRateSigCoef(UShort uiSignificance, UShort ui16CtxNumSig) const { return m_dLambda * m_pcEstBitsSbac->significantBits[ui16CtxNumSig][uiSignificance]; }
    __inline Double xGetICost(Double dRate) const { return m_dLambda * dRate; } ///< Get the cost for a specific rate
    __inline Double xGetIEPRate() const           { return 32768; }             ///< Get the cost of an equal probable bit

    // dequantization
    Void xDeQuant(Int bitDepth, const TCoeff* src, Int* dst, Int width, Int height, Int scalingListType);

    // inverse transform
    Void xIT(Int bitDepth, UInt mode, Int* coeff, Short* residual, UInt stride, Int width, Int height);

    // inverse skipping transform
    Void xITransformSkip(Int bitDepth, Int* coeff, Short* residual, UInt stride, Int width, Int height);
}; // END CLASS DEFINITION TComTrQuant

//! \}

#endif // __TCOMTRQUANT__
