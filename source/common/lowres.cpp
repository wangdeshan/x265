/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Gopu Govindaswamy <gopu@multicorewareinc.com>
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

#include "TLibCommon/TComPic.h"
#include "lowres.h"
#include "mv.h"

using namespace x265;

void Lowres::create(TComPicYuv *orig, int bframes, int32_t *aqMode)
{
    isLowres = true;
    width = orig->getWidth() / 2;
    lines = orig->getHeight() / 2;
    lumaStride = width + 2 * orig->getLumaMarginX();
    if (lumaStride & 31)
        lumaStride += 32 - (lumaStride & 31);
    int cuWidth = (width + X265_LOWRES_CU_SIZE - 1) >> X265_LOWRES_CU_BITS;
    int cuHeight = (lines + X265_LOWRES_CU_SIZE - 1) >> X265_LOWRES_CU_BITS;
    int cuCount = cuWidth * cuHeight;

    /* rounding the width to multiple of lowres CU size */
    width = cuWidth * X265_LOWRES_CU_SIZE;
    lines = cuHeight * X265_LOWRES_CU_SIZE;

    if (*aqMode)
    {
        qpAqOffset = (double*)x265_malloc(sizeof(double) * cuCount);
        invQscaleFactor = (int*)x265_malloc(sizeof(int) * cuCount);
        qpOffset = (double*)x265_malloc(sizeof(double) * cuCount);
        if (!qpAqOffset || !invQscaleFactor || !qpOffset)
            *aqMode = 0;
    }
    propagateCost = (uint16_t*)x265_malloc(sizeof(uint16_t) * cuCount);

    /* allocate lowres buffers */
    for (int i = 0; i < 4; i++)
    {
        buffer[i] = (Pel*)X265_MALLOC(Pel, lumaStride * (lines + 2 * orig->getLumaMarginY()));
    }

    int padoffset = lumaStride * orig->getLumaMarginY() + orig->getLumaMarginX();
    lowresPlane[0] = buffer[0] + padoffset;
    lowresPlane[1] = buffer[1] + padoffset;
    lowresPlane[2] = buffer[2] + padoffset;
    lowresPlane[3] = buffer[3] + padoffset;

    intraCost = (int32_t*)X265_MALLOC(int, cuCount);

    for (int i = 0; i < bframes + 2; i++)
    {
        for (int j = 0; j < bframes + 2; j++)
        {
            rowSatds[i][j] = (int32_t*)X265_MALLOC(int, cuHeight);
            lowresCosts[i][j] = (uint16_t*)X265_MALLOC(uint16_t, cuCount);
        }
    }

    for (int i = 0; i < bframes + 1; i++)
    {
        lowresMvs[0][i] = (MV*)X265_MALLOC(MV, cuCount);
        lowresMvs[1][i] = (MV*)X265_MALLOC(MV, cuCount);
        lowresMvCosts[0][i] = (int32_t*)X265_MALLOC(int, cuCount);
        lowresMvCosts[1][i] = (int32_t*)X265_MALLOC(int, cuCount);
    }
}

void Lowres::destroy(int bframes)
{
    for (int i = 0; i < 4; i++)
    {
        X265_FREE(buffer[i]);
    }

    X265_FREE(intraCost);

    for (int i = 0; i < bframes + 2; i++)
    {
        for (int j = 0; j < bframes + 2; j++)
        {
            X265_FREE(rowSatds[i][j]);
            X265_FREE(lowresCosts[i][j]);
        }
    }

    for (int i = 0; i < bframes + 1; i++)
    {
        X265_FREE(lowresMvs[0][i]);
        X265_FREE(lowresMvs[1][i]);
        X265_FREE(lowresMvCosts[0][i]);
        X265_FREE(lowresMvCosts[1][i]);
    }

    X265_FREE(qpAqOffset);
    X265_FREE(invQscaleFactor);
    X265_FREE(qpOffset);
    X265_FREE(propagateCost);
}

// (re) initialize lowres state
void Lowres::init(TComPicYuv *orig, int poc, int type, int bframes)
{
    bScenecut = true;
    bIntraCalculated = false;
    bLastMiniGopBFrame = false;
    bKeyframe = false; // Not a keyframe unless identified by lookahead
    sliceType = type;
    frameNum = poc;
    leadingBframes = 0;
    satdCost = (uint64_t)-1;
    memset(costEst, -1, sizeof(costEst));

    if (qpAqOffset && invQscaleFactor)
        memset(costEstAq, -1, sizeof(costEstAq));

    for (int y = 0; y < bframes + 2; y++)
    {
        for (int x = 0; x < bframes + 2; x++)
        {
            rowSatds[y][x][0] = -1;
        }
    }

    for (int i = 0; i < bframes + 1; i++)
    {
        lowresMvs[0][i][0].x = 0x7FFF;
        lowresMvs[1][i][0].x = 0x7FFF;
    }

    for (int i = 0; i < bframes + 2; i++)
    {
        intraMbs[i] = 0;
    }

    /* downscale and generate 4 HPEL planes for lookahead */
    primitives.frame_init_lowres_core(orig->getLumaAddr(),
                                      lowresPlane[0], lowresPlane[1], lowresPlane[2], lowresPlane[3],
                                      orig->getStride(), lumaStride, width, lines);

    /* extend hpel planes for motion search */
    orig->xExtendPicCompBorder(lowresPlane[0], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    orig->xExtendPicCompBorder(lowresPlane[1], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    orig->xExtendPicCompBorder(lowresPlane[2], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    orig->xExtendPicCompBorder(lowresPlane[3], lumaStride, width, lines, orig->getLumaMarginX(), orig->getLumaMarginY());
    fpelPlane = lowresPlane[0];
}
