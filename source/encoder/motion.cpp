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
 * For more information, contact us at licensing@multicorewareinc.com.
 *****************************************************************************/

#include "primitives.h"
#include "common.h"
#include "motion.h"
#include <string.h>
#include <stdio.h>

using namespace x265;

static int size_scale[NUM_PARTITIONS];

#define SAD_THRESH(v) (bcost < (((v>>4) * size_scale[partEnum])))

static void init_scales(void)
{
    int dims[] = {4, 8, 12, 16, 24, 32, 48, 64};

    int i = 0;
    for (int h = 0; h < sizeof(dims)/sizeof(int); h++)
        for (int w = 0; w < sizeof(dims)/sizeof(int); w++)
            size_scale[i++] = (dims[h] * dims[w]) >> 4;
}

void MotionEstimate::setSourcePU(int offset, int width, int height)
{
    if (size_scale[0] == 0)
        init_scales();

    partEnum = PartitionFromSizes(width, height);

    sad = primitives.sad[partEnum];
    satd = primitives.satd[partEnum];
    sad_x3 = primitives.sad_x3[partEnum];
    sad_x4 = primitives.sad_x4[partEnum];

    blockWidth = width;
    blockHeight = height;
    blockOffset = offset;

    /* copy block into local buffer */
    pixel *fencblock = fencplanes[0] + offset;
    primitives.cpyblock(width, height, fenc, FENC_STRIDE, fencblock, fencLumaStride);
}

/* (x-1)%6 */
static const uint8_t mod6m1[8] = { 5, 0, 1, 2, 3, 4, 5, 0 };
/* radius 2 hexagon. repeated entries are to avoid having to compute mod6 every time. */
static const int8_t hex2[8][2] = { { -1, -2 }, { -2, 0 }, { -1, 2 }, { 1, 2 }, { 2, 0 }, { 1, -2 }, { -1, -2 }, { -2, 0 } };
static const int8_t square1[9][2] = { { 0, 0 }, { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 }, { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };

static __inline int x265_predictor_difference(const  MV *mvc, intptr_t numCandidates)
{
    int sum = 0;

    for (int i = 0; i < numCandidates - 1; i++)
    {
        sum += abs(mvc[i].x - mvc[i + 1].x)
            +  abs(mvc[i].y - mvc[i + 1].y);
    }

    return sum;
}

#define BITS_MVD(mx, my) (mvcost(MV(mx, my) << 2))

#define COST_MV(mx, my) \
    do \
    { \
        int cost = fpelSad(fref, bmv) + mvcost(bmv); \
        COPY3_IF_LT(bcost, cost, bmv.x, mx, bmv.y, my); \
    } while (0)

#define COST_MV_X3_DIR(m0x, m0y, m1x, m1y, m2x, m2y, costs) \
    { \
        size_t stride = ref->lumaStride; \
        pixel *pix_base = fref + bmv.x + bmv.y * stride; \
        sad_x3(fenc, \
               pix_base + (m0x) + (m0y) * stride, \
               pix_base + (m1x) + (m1y) * stride, \
               pix_base + (m2x) + (m2y) * stride, \
               stride, costs); \
        (costs)[0] += mvcost((bmv + MV(m0x, m0y)) << 2); \
        (costs)[1] += mvcost((bmv + MV(m1x, m1y)) << 2); \
        (costs)[2] += mvcost((bmv + MV(m2x, m2y)) << 2); \
    }

#define COST_MV_X4(m0x, m0y, m1x, m1y, m2x, m2y, m3x, m3y) \
    { \
        size_t stride = ref->lumaStride; \
        sad_x4(fenc, \
               pix_base + (m0x) + (m0y) * stride, \
               pix_base + (m1x) + (m1y) * stride, \
               pix_base + (m2x) + (m2y) * stride, \
               pix_base + (m3x) + (m3y) * stride, \
               stride, costs); \
        costs[0] += mvcost((omv + MV(m0x, m0y)) << 2); \
        costs[1] += mvcost((omv + MV(m1x, m1y)) << 2); \
        costs[2] += mvcost((omv + MV(m2x, m2y)) << 2); \
        costs[3] += mvcost((omv + MV(m3x, m3y)) << 2); \
        COPY3_IF_LT(bcost, costs[0], bmv.x, omv.x + (m0x), bmv.y, omv.y + (m0y)); \
        COPY3_IF_LT(bcost, costs[1], bmv.x, omv.x + (m1x), bmv.y, omv.y + (m1y)); \
        COPY3_IF_LT(bcost, costs[2], bmv.x, omv.x + (m2x), bmv.y, omv.y + (m2y)); \
        COPY3_IF_LT(bcost, costs[3], bmv.x, omv.x + (m3x), bmv.y, omv.y + (m3y)); \
    }

#define COST_MV_X4_DIR(m0x, m0y, m1x, m1y, m2x, m2y, m3x, m3y, costs) \
    { \
        size_t stride = ref->lumaStride; \
        pixel *pix_base = fref + bmv.x + bmv.y * stride; \
        sad_x4(fenc, \
               pix_base + (m0x) + (m0y) * stride, \
               pix_base + (m1x) + (m1y) * stride, \
               pix_base + (m2x) + (m2y) * stride, \
               pix_base + (m3x) + (m3y) * stride, \
               stride, costs); \
        (costs)[0] += mvcost((bmv + MV(m0x, m0y)) << 2); \
        (costs)[1] += mvcost((bmv + MV(m1x, m1y)) << 2); \
        (costs)[2] += mvcost((bmv + MV(m2x, m2y)) << 2); \
        (costs)[3] += mvcost((bmv + MV(m3x, m3y)) << 2); \
    }

#define DIA1_ITER(mx, my) \
    { \
        omv.x = mx; omv.y = my; \
        COST_MV_X4(0, -1, 0, 1, -1, 0, 1, 0); \
    }

#define CROSS(start, x_max, y_max) \
    { \
        int16_t i = start; \
        if ((x_max) <= X265_MIN(mvmax.x - omv.x, omv.x - mvmin.x)) \
            for (; i < (x_max) - 2; i += 4) { \
                COST_MV_X4(i, 0, -i, 0, i + 2, 0, -i - 2, 0); } \
        for (; i < (x_max); i += 2) \
        { \
            if (omv.x + i <= mvmax.x) \
                COST_MV(omv.x + i, omv.y); \
            if (omv.x - i >= mvmin.x) \
                COST_MV(omv.x - i, omv.y); \
        } \
        i = start; \
        if ((y_max) <= X265_MIN(mvmax.y - omv.y, omv.y - mvmin.y)) \
            for (; i < (y_max) - 2; i += 4) { \
                COST_MV_X4(0, i, 0, -i, 0, i + 2, 0, -i - 2); } \
        for (; i < (y_max); i += 2) \
        { \
            if (omv.y + i <= mvmax.y) \
                COST_MV(omv.x, omv.y + i); \
            if (omv.y - i >= mvmin.y) \
                COST_MV(omv.x, omv.y - i); \
        } \
    }

#if _MSC_VER
#pragma warning(disable: 4127) // conditional  expression is constant
#endif
int MotionEstimate::motionEstimate(const MV &qmvp,
                                   int       numCandidates,
                                   const MV *mvc,
                                   int       merange,
                                   MV &      outQMv)
{
    ALIGN_VAR_16(int, costs[16]);
    pixel *fref = ref->lumaPlane[0][0] + blockOffset;

    setMVP(qmvp);

    MV qmvmin = mvmin.toQPel();
    MV qmvmax = mvmax.toQPel();

    // measure SAD cost at MVP
    MV bmv = qmvp.clipped(qmvmin, qmvmax);
    MV bestpre = bmv;
    int bcost = qpelSad(bmv);

    /* measure SAD cost at each candidate */
    for (int i = 0; i < numCandidates; i++)
    {
        MV m = mvc[i].clipped(qmvmin, qmvmax);
        if (m == 0) // zero is measured later
            continue;

        int cost = qpelSad(m) + mvcost(m);
        if (cost < bcost)
        {
            bcost = cost;
            bestpre = m;
        }
    }

    if (bmv != 0)
    {
        int cost = fpelSad(fref, 0) + mvcost(0);
        if (cost < bcost)
        {
            bcost = cost;
            bestpre = 0;
        }
    }
    /* remember the best SAD cost of motion candidates */
    int bprecost = bcost;

    /* Measure full pel SAD at MVP */
    bmv = bmv.roundToFPel();
    bcost = fpelSad(fref, bmv) + mvcost(bmv.toQPel());
    MV pmv = qmvp; // this needs to be confirmation pridictor Motion vector
    MV omv = bmv;

    omv.x = bmv.x;
    omv.y = bmv.y;

    int meMethod = 2;
    switch (meMethod)
    {
    case 0:
    {
        /* diamond search, radius 1 */
        bcost <<= 4;
        int i = merange;
        do
        {
            COST_MV_X4_DIR(0, -1, 0, 1, -1, 0, 1, 0, costs);
            COPY1_IF_LT(bcost, (costs[0] << 4) + 1);
            COPY1_IF_LT(bcost, (costs[1] << 4) + 3);
            COPY1_IF_LT(bcost, (costs[2] << 4) + 4);
            COPY1_IF_LT(bcost, (costs[3] << 4) + 12);
            if (!(bcost & 15))
                break;
            bmv.x -= (bcost << 28) >> 30;
            bmv.y -= (bcost << 30) >> 30;
            bcost &= ~15;
        }
        while (--i && bmv.checkRange(mvmin, mvmax));
        bcost >>= 4;
        break;
    }

    case 1:
    {
me_hex2:
        /* hexagon */
        COST_MV_X3_DIR(-2, 0, -1, 2,  1, 2, costs);
        COST_MV_X3_DIR(2, 0,  1, -2, -1, -2, costs + 3);
        bcost <<= 3;
        COPY1_IF_LT(bcost, (costs[0] << 3) + 2);
        COPY1_IF_LT(bcost, (costs[1] << 3) + 3);
        COPY1_IF_LT(bcost, (costs[2] << 3) + 4);
        COPY1_IF_LT(bcost, (costs[3] << 3) + 5);
        COPY1_IF_LT(bcost, (costs[4] << 3) + 6);
        COPY1_IF_LT(bcost, (costs[5] << 3) + 7);

        if (bcost & 7)
        {
            int dir = (bcost & 7) - 2;
            bmv.x += hex2[dir + 1][0];
            bmv.y += hex2[dir + 1][1];

            /* half hexagon, not overlapping the previous iteration */
            for (int i = (merange >> 1) - 1; i > 0 && bmv.checkRange(mvmin, mvmax); i--)
            {
                COST_MV_X3_DIR(hex2[dir + 0][0], hex2[dir + 0][1],
                               hex2[dir + 1][0], hex2[dir + 1][1],
                               hex2[dir + 2][0], hex2[dir + 2][1],
                               costs);
                bcost &= ~7;
                COPY1_IF_LT(bcost, (costs[0] << 3) + 1);
                COPY1_IF_LT(bcost, (costs[1] << 3) + 2);
                COPY1_IF_LT(bcost, (costs[2] << 3) + 3);
                if (!(bcost & 7))
                    break;
                dir += (bcost & 7) - 2;
                dir = mod6m1[dir + 1];
                bmv.x += hex2[dir + 1][0];
                bmv.y += hex2[dir + 1][1];
            }
        }
        bcost >>= 3;

        /* square refine */
        int dir = 0;
        COST_MV_X4_DIR(0, -1,  0, 1, -1, 0, 1, 0, costs);
        COPY2_IF_LT(bcost, costs[0], dir, 1);
        COPY2_IF_LT(bcost, costs[1], dir, 2);
        COPY2_IF_LT(bcost, costs[2], dir, 3);
        COPY2_IF_LT(bcost, costs[3], dir, 4);
        COST_MV_X4_DIR(-1, -1, -1, 1, 1, -1, 1, 1, costs);
        COPY2_IF_LT(bcost, costs[0], dir, 5);
        COPY2_IF_LT(bcost, costs[1], dir, 6);
        COPY2_IF_LT(bcost, costs[2], dir, 7);
        COPY2_IF_LT(bcost, costs[3], dir, 8);
        bmv.x += square1[dir][0];
        bmv.y += square1[dir][1];
        break;
    }

    case 2:
    {
        int ucost1, ucost2;
        int16_t cross_start = 1;

        /* refine predictors */
        ucost1 = bcost;
        omv.x = bmv.x;
        omv.y = bmv.y;

        pixel *pix_base = fref + omv.x + omv.y * ref->lumaStride;
        DIA1_ITER(pmv.x, pmv.y);
        if (pmv.x | pmv.y)
            DIA1_ITER(0, 0);

        if (PARTITION_4x4 == partEnum)
            goto me_hex2;

        ucost2 = bcost;
        if ((bmv.x | bmv.y) && ((bmv.x - pmv.x) | (bmv.y - pmv.y)))
            DIA1_ITER(bmv.x, bmv.y);

        if (bcost == ucost2)
            cross_start = 3;

        omv.x = bmv.x;
        omv.y = bmv.y;

        /* Early Termination */

        if (bcost == ucost2 && SAD_THRESH(2000))
        {
            COST_MV_X4(0, -2, -1, -1, 1, -1, -2, 0);
            COST_MV_X4(2, 0, -1, 1, 1, 1,  0, 2);
            if (bcost == ucost1 && SAD_THRESH(500))
                break;
            if (bcost == ucost2)
            {
                int16_t range = (int16_t)(merange >> 1) | 1;
                CROSS(3, range, range);
                COST_MV_X4(-1, -2, 1, -2, -2, -1, 2, -1);
                COST_MV_X4(-2, 1, 2, 1, -1, 2, 1, 2);
                if (bcost == ucost2)
                    break;
                cross_start = range + 2;
            }
        }

        // Start Adaptive search range
        if (numCandidates)
        {
            static const uint8_t range_mul[4][4] =
            {
                { 3, 3, 4, 4 },
                { 3, 4, 4, 4 },
                { 4, 4, 4, 5 },
                { 4, 4, 5, 6 },
            };

            int16_t mvd;
            int16_t sad_ctx, mvd_ctx;
            int denom = 1;

            if (numCandidates == 1)
            {
                if (PARTITION_16x16 == partEnum)
                    /* mvc is probably the same as mvp, so the difference isn't meaningful.
                     * but prediction usually isn't too bad, so just use medium range */
                    mvd = 25;
                else
                    mvd = (int16_t)(abs(qmvp.x - mvc[0].x) + abs(qmvp.y - mvc[0].y));
            }
            else
            {
                /* calculate the degree of agreement between predictors. */

                /* in 16x16, mvc includes all the neighbors used to make mvp,
                 * so don't count mvp separately. */

                denom = numCandidates - 1;
                mvd = 0;
                if (partEnum != PARTITION_16x16)
                {
                    mvd = (int16_t)(abs(qmvp.x - mvc[0].x)
                                    + abs(qmvp.y - mvc[0].y));
                    denom++;
                }
                mvd += (int16_t)x265_predictor_difference(mvc, numCandidates);
            }

            sad_ctx = SAD_THRESH(1000) ? 0
                : SAD_THRESH(2000) ? 1
                : SAD_THRESH(4000) ? 2 : 3;
            mvd_ctx = mvd < 10 * denom ? 0
                : mvd < 20 * denom ? 1
                : mvd < 40 * denom ? 2 : 3;

            merange = merange * range_mul[mvd_ctx][sad_ctx] >> 2;
        }

        /* FIXME if the above DIA2/OCT2/CROSS found a new mv, it has not updated omx/omy.
         * we are still centered on the same place as the DIA2. is this desirable? */

        omv.x = bmv.x;
        omv.y = bmv.y;

        CROSS(cross_start, blockWidth, blockHeight >> 1);

        COST_MV_X4(-2, -2, -2, 2, 2, -2, 2, 2);

        /* hexagon grid */
        omv.x = bmv.x;
        omv.y = bmv.y;

        const uint16_t *p_cost_omvx = m_cost_mvx + omv.x * 4;
        const uint16_t *p_cost_omvy = m_cost_mvy + omv.y * 4;
        int16_t i = 1;

        do
        {
            static const int8_t hex4[16][2] =
            {
                { 0, -4 }, { 0, 4 }, { -2, -3 }, { 2, -3 },
                { -4, -2 }, { 4, -2 }, { -4, -1 }, { 4, -1 },
                { -4, 0 }, { 4, 0 }, { -4, 1 }, { 4, 1 },
                { -4, 2 }, { 4, 2 }, { -2, 3 }, { 2, 3 },
            };

            if (4 * i > X265_MIN4(mvmax.x - omv.x, omv.x - mvmin.x,
                                  mvmax.y - omv.y, omv.y - mvmin.y))
            {
                for (int j = 0; j < 16; j++)
                {
                    int16_t mx = omv.x + hex4[j][0] * i;
                    int16_t my = omv.y + hex4[j][1] * i;
                    if (bmv.checkRange(mvmin, mvmax))
                        COST_MV(mx, my);
                }
            }
            else
            {
                int16_t dir = 0;
                size_t stride = ref->lumaStride;
                pixel *fref_base = fref + omv.x + (omv.y - 4 * i) * stride;
                int dy = (int)(i * stride);

#define SADS(k, x0, y0, x1, y1, x2, y2, x3, y3) \
    sad_x4(fenc, \
           fref_base x0 * i + (y0 - 2 * k + 4) * dy, \
           fref_base x1 * i + (y1 - 2 * k + 4) * dy, \
           fref_base x2 * i + (y2 - 2 * k + 4) * dy, \
           fref_base x3 * i + (y3 - 2 * k + 4) * dy, \
           stride, costs + 4 * k); \
    fref_base += 2 * dy;
#define ADD_MVCOST(k, x, y) costs[k] += p_cost_omvx[x * 4 * i] + p_cost_omvy[y * 4 * i]
#define MIN_MV(k, x, y)     COPY2_IF_LT(bcost, costs[k], dir, x * 16 + (y & 15))

                SADS(0, +0, -4, +0, +4, -2, -3, +2, -3);
                SADS(1, -4, -2, +4, -2, -4, -1, +4, -1);
                SADS(2, -4, +0, +4, +0, -4, +1, +4, +1);
                SADS(3, -4, +2, +4, +2, -2, +3, +2, +3);
                ADD_MVCOST(0, 0, -4);
                ADD_MVCOST(1, 0, 4);
                ADD_MVCOST(2, -2, -3);
                ADD_MVCOST(3, 2, -3);
                ADD_MVCOST(4, -4, -2);
                ADD_MVCOST(5, 4, -2);
                ADD_MVCOST(6, -4, -1);
                ADD_MVCOST(7, 4, -1);
                ADD_MVCOST(8, -4, 0);
                ADD_MVCOST(9, 4, 0);
                ADD_MVCOST(10, -4, 1);
                ADD_MVCOST(11, 4, 1);
                ADD_MVCOST(12, -4, 2);
                ADD_MVCOST(13, 4, 2);
                ADD_MVCOST(14, -2, 3);
                ADD_MVCOST(15, 2, 3);
                MIN_MV(0, 0, -4);
                MIN_MV(1, 0, 4);
                MIN_MV(2, -2, -3);
                MIN_MV(3, 2, -3);
                MIN_MV(4, -4, -2);
                MIN_MV(5, 4, -2);
                MIN_MV(6, -4, -1);
                MIN_MV(7, 4, -1);
                MIN_MV(8, -4, 0);
                MIN_MV(9, 4, 0);
                MIN_MV(10, -4, 1);
                MIN_MV(11, 4, 1);
                MIN_MV(12, -4, 2);
                MIN_MV(13, 4, 2);
                MIN_MV(14, -2, 3);
                MIN_MV(15, 2, 3);
#undef SADS
#undef ADD_MVCOST
#undef MIN_MV
                if (dir)
                {
                    bmv.x = omv.x + i * (dir >> 4);
                    bmv.y = omv.y + i * ((dir << 28) >> 28);
                }
            }
        }
        while (++i <= merange >> 2);
        if (bmv.y <= mvmax.y && bmv.y >= mvmin.y && bmv.x <= mvmax.x && bmv.x >= mvmin.x)
            goto me_hex2;
        break;
    }     // case 1 End - UMH
    }

    if (bprecost < bcost)
        bmv = bestpre;
    else
        bmv = bmv.toQPel(); // promote bmv to qpel

    /* bmv has the best full pel motion vector found by SAD search or motion candidates */

    bcost = qpelSatd(bmv) + mvcost(bmv); // remeasure BMV using SATD

    /* HPEL refinement followed by QPEL refinement */

    bcost <<= 4;
    int16_t res = 2;
    do
    {
        MV mv = bmv + MV(0, -res);
        int cost = qpelSatd(mv) + mvcost(mv);
        COPY1_IF_LT(bcost, (cost << 4) + 1);

        mv = bmv + MV(0,  res);
        cost = qpelSatd(mv) + mvcost(mv);
        COPY1_IF_LT(bcost, (cost << 4) + 3);

        mv = bmv + MV(-res, 0);
        cost = qpelSatd(mv) + mvcost(mv);
        COPY1_IF_LT(bcost, (cost << 4) + 4);

        mv = bmv + MV(res,  0);
        cost = qpelSatd(mv) + mvcost(mv);
        COPY1_IF_LT(bcost, (cost << 4) + 12);

        if (bcost & 15)
        {
            bmv.x -= res * ((bcost << 28) >> 30);
            bmv.y -= res * ((bcost << 30) >> 30);
            bcost &= ~15;
        }

        res >>= 1;
    }
    while (res);

    x265_emms();
    outQMv = bmv;
    return bcost >> 4;
}
