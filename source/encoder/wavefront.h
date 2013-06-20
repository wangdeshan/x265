/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Chung Shin Yee <shinyee@multicorewareinc.com>
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

#ifndef __WAVEFRONT__
#define __WAVEFRONT__

#include "TLibCommon/TComBitCounter.h"
#include "TLibCommon/TComPic.h"
#include "TLibEncoder/TEncSbac.h"
#include "TLibEncoder/TEncBinCoderCABAC.h"

#include "threadpool.h"

class TEncTop;

namespace x265 {
// private x265 namespace

class CTURow
{
public:

    TEncSbac               m_cSbacCoder;
    TEncSbac               m_cRDGoOnSbacCoder;
    TEncSbac               m_cBufferSbacCoder;
    TEncBinCABAC           m_cBinCoderCABAC;
    TEncBinCABACCounter    m_cRDGoOnBinCodersCABAC;
    TComBitCounter         m_cBitCounter;
    TComRdCost             m_cRdCost;
    TEncEntropy            m_cEntropyCoder;
    TEncSearch             m_cSearch;
    TEncCu                 m_cCuEncoder;
    TComTrQuant            m_cTrQuant;
    TEncSbac            ***m_pppcRDSbacCoders;
    TEncBinCABACCounter ***m_pppcBinCodersCABAC;

    void create(TEncTop* top);

    void destroy();

    void init()
    {
        m_active = 0;
        m_curCol = 0;
    }

    inline void processCU(TComDataCU *pcCU, TComSlice *pcSlice, TEncSbac *pcBufferSBac, bool bSaveCabac);

    /* Threading */
    Lock                m_lock;
    volatile bool       m_active;
    volatile uint32_t   m_curCol;
};

class EncodeFrame : public QueueFrame
{
public:

    EncodeFrame(ThreadPool *);

    virtual ~EncodeFrame() {}

    void init(TEncTop *top, int numRows);

    void destroy();

    void Encode(TComPic *pic, TComSlice* pcSlice);

    void ProcessRow(int irow);

    /* Config broadcast methods */
    void setAdaptiveSearchRange(int iDir, int iRefIdx, int iNewSR)
    {
        for (int i = 0; i < m_nrows; i++)
        {
            m_rows[i].m_cSearch.setAdaptiveSearchRange(iDir, iRefIdx, iNewSR);
        }
    }

    void setLambda(double dLambdaLuma, double dLambdaChroma)
    {
        for (int i = 0; i < m_nrows; i++)
        {
            m_rows[i].m_cRdCost.setLambda(dLambdaLuma);
            m_rows[i].m_cTrQuant.setLambda(dLambdaLuma, dLambdaChroma);
        }
    }

    void setCbDistortionWeight(double weight)
    {
        for (int i = 0; i < m_nrows; i++)
        {
            m_rows[i].m_cRdCost.setCbDistortionWeight(weight);
        }
    }

    void setCrDistortionWeight(double weight)
    {
        for (int i = 0; i < m_nrows; i++)
        {
            m_rows[i].m_cRdCost.setCrDistortionWeight(weight);
        }
    }

    void setFlatScalingList()
    {
        for (int i = 0; i < m_nrows; i++)
        {
            m_rows[i].m_cTrQuant.setFlatScalingList();
        }
    }

    void setUseScalingList(bool flag)
    {
        for (int i = 0; i < m_nrows; i++)
        {
            m_rows[i].m_cTrQuant.setUseScalingList(flag);
        }
    }

    void setScalingList(TComScalingList *list)
    {
        for (int i = 0; i < m_nrows; i++)
        {
            this->m_rows[i].m_cTrQuant.setScalingList(list);
        }
    }

    TEncEntropy* getEntropyEncoder(int row)    { return &this->m_rows[row].m_cEntropyCoder; }

    TEncSbac*    getSbacCoder(int row)         { return &this->m_rows[row].m_cSbacCoder; }

    TEncSbac*    getRDGoOnSbacCoder(int row)   { return &this->m_rows[row].m_cRDGoOnSbacCoder; }

    TEncSbac***  getRDSbacCoders(int row)      { return this->m_rows[row].m_pppcRDSbacCoders; }

    TEncSbac*    getBufferSBac(int row)        { return &this->m_rows[row].m_cBufferSbacCoder; }

    TEncCu*      getCuEncoder(int row)         { return &this->m_rows[row].m_cCuEncoder; }

    TComSlice*   getSlice()                    { return m_pcSlice; }

    /* Frame singletons, last the life of the encoder */
    TEncSbac*               getSingletonSbac() { return &m_cSbacCoder; }
    TComLoopFilter*         getLoopFilter()    { return &m_cLoopFilter; }
    TEncSampleAdaptiveOffset* getSAO()         { return &m_cEncSAO; }
    TEncCavlc*              getCavlcCoder()    { return &m_cCavlcCoder; }
    TEncBinCABAC*           getBinCABAC()      { return &m_cBinCoderCABAC; }
    TComBitCounter*         getBitCounter()    { return &m_cBitCounter; }
    TEncSlice*              getSliceEncoder()  { return &m_cSliceEncoder; }

    void resetEntropy(TComSlice *pcSlice)
    {
        for (int i = 0; i < this->m_nrows; i++)
        {
            this->m_rows[i].m_cEntropyCoder.setEntropyCoder(&this->m_rows[i].m_cSbacCoder, pcSlice);
            this->m_rows[i].m_cEntropyCoder.resetEntropy();
        }
    }

    void resetEncoder()
    {
        // Initialize global singletons (these should eventually be per-slice)
        m_cSbacCoder.init((TEncBinIf*)&m_cBinCoderCABAC);
    }

protected:

    TEncSbac                 m_cSbacCoder;
    TEncBinCABAC             m_cBinCoderCABAC;
    TEncCavlc                m_cCavlcCoder;
    TComLoopFilter           m_cLoopFilter;
    TEncSampleAdaptiveOffset m_cEncSAO;
    TComBitCounter           m_cBitCounter;
    TEncSlice                m_cSliceEncoder;
    TEncCfg*                 m_pcCfg;

    TComSlice*               m_pcSlice;
    TComPic*                 m_pic;

    int                      m_nrows;
    bool                     m_enableWpp;
    CTURow*                  m_rows;
    Event                    m_completionEvent;
};
}

#endif // ifndef __WAVEFRONT__
