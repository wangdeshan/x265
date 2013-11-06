/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Min Chen <chenm003@163.com>
 *          Deepthi Devaki <deepthidevaki@multicorewareinc.com>
 *          Steve Borho <steve@borho.org>
 *          ShinYee Chung <shinyee@multicorewareinc.com>
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
#include "TLibCommon/TComRom.h"
#include <assert.h>
#include <xmmintrin.h> // SSE
#include <pmmintrin.h> // SSE3
#include <tmmintrin.h> // SSSE3

using namespace x265;

namespace {
const int angAP[17][64] =
{
    {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64
    },
    {
        0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 8, 9, 10, 11, 12, 13, 13, 14, 15, 16, 17, 17, 18, 19, 20, 21, 21, 22, 23, 24, 25, 26, 26, 27, 28, 29, 30, 30, 31, 32, 33, 34, 34, 35, 36, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45, 46, 47, 47, 48, 49, 50, 51, 52
    },
    {
        0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 13, 13, 14, 15, 15, 16, 17, 17, 18, 19, 19, 20, 21, 21, 22, 22, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32, 32, 33, 34, 34, 35, 36, 36, 37, 38, 38, 39, 40, 40, 41, 42
    },
    {
        0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 34
    },
    {
        0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17, 17, 17, 18, 18, 19, 19, 19, 20, 20, 21, 21, 21, 22, 22, 23, 23, 23, 24, 24, 25, 25, 26
    },
    {
        0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 18
    },
    {
        0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10
    },
    {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4
    },
    { // 0th virtual index; never used; just to help indexing
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4
    },
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4
    },
    {
        -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -3, -3, -3, -3, -3, -3, -3, -4, -4, -4, -4, -4, -4, -5, -5, -5, -5, -5, -5, -5, -6, -6, -6, -6, -6, -6, -7, -7, -7, -7, -7, -7, -8, -8, -8, -8, -8, -8, -8, -9, -9, -9, -9, -9, -9, -10, -10, -10, -10, -10, -10, -10
    },
    {
        -1, -1, -1, -2, -2, -2, -2, -3, -3, -3, -4, -4, -4, -4, -5, -5, -5, -6, -6, -6, -6, -7, -7, -7, -8, -8, -8, -8, -9, -9, -9, -9, -10, -10, -10, -11, -11, -11, -11, -12, -12, -12, -13, -13, -13, -13, -14, -14, -14, -15, -15, -15, -15, -16, -16, -16, -17, -17, -17, -17, -18, -18, -18, -18
    },
    {
        -1, -1, -2, -2, -3, -3, -3, -4, -4, -5, -5, -5, -6, -6, -7, -7, -7, -8, -8, -9, -9, -9, -10, -10, -11, -11, -11, -12, -12, -13, -13, -13, -14, -14, -15, -15, -16, -16, -16, -17, -17, -18, -18, -18, -19, -19, -20, -20, -20, -21, -21, -22, -22, -22, -23, -23, -24, -24, -24, -25, -25, -26, -26, -26
    },
    {
        -1, -2, -2, -3, -3, -4, -4, -5, -5, -6, -6, -7, -7, -8, -8, -9, -10, -10, -11, -11, -12, -12, -13, -13, -14, -14, -15, -15, -16, -16, -17, -17, -18, -19, -19, -20, -20, -21, -21, -22, -22, -23, -23, -24, -24, -25, -25, -26, -27, -27, -28, -28, -29, -29, -30, -30, -31, -31, -32, -32, -33, -33, -34, -34
    },
    {
        -1, -2, -2, -3, -4, -4, -5, -6, -6, -7, -8, -8, -9, -10, -10, -11, -12, -12, -13, -14, -14, -15, -16, -16, -17, -18, -18, -19, -20, -20, -21, -21, -22, -23, -23, -24, -25, -25, -26, -27, -27, -28, -29, -29, -30, -31, -31, -32, -33, -33, -34, -35, -35, -36, -37, -37, -38, -39, -39, -40, -41, -41, -42, -42
    },
    {
        -1, -2, -3, -4, -5, -5, -6, -7, -8, -9, -9, -10, -11, -12, -13, -13, -14, -15, -16, -17, -18, -18, -19, -20, -21, -22, -22, -23, -24, -25, -26, -26, -27, -28, -29, -30, -31, -31, -32, -33, -34, -35, -35, -36, -37, -38, -39, -39, -40, -41, -42, -43, -44, -44, -45, -46, -47, -48, -48, -49, -50, -51, -52, -52
    },
    {
        -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15, -16, -17, -18, -19, -20, -21, -22, -23, -24, -25, -26, -27, -28, -29, -30, -31, -32, -33, -34, -35, -36, -37, -38, -39, -40, -41, -42, -43, -44, -45, -46, -47, -48, -49, -50, -51, -52, -53, -54, -55, -56, -57, -58, -59, -60, -61, -62, -63, -64
    }
};

#define GETAP(X, Y) angAP[8 - (X)][(Y)]

#if !HIGH_BIT_DEPTH
#define PRED_INTRA_ANGLE_4_START() \
    __m128i row11, row12, row21, row22, row31, row32, row41, row42; \
    __m128i tmp16_1, tmp16_2, tmp2, deltaFract; \
    __m128i deltaPos = _mm_set1_epi16(0); \
    __m128i ipAngle  = _mm_set1_epi16(0); \
    __m128i thirty1  = _mm_set1_epi16(31); \
    __m128i thirty2  = _mm_set1_epi16(32); \
    bool modeHor     = (dirMode < 18);

#define PRED_INTRA_ANGLE_4_END() \
    deltaFract = _mm_and_si128(deltaPos, thirty1); \
    __m128i mullo = _mm_mullo_epi16(row11, _mm_sub_epi16(thirty2, deltaFract)); \
    __m128i sum = _mm_add_epi16(_mm_set1_epi16(16), _mm_mullo_epi16(deltaFract, row12)); \
    row11 = _mm_sra_epi16(_mm_add_epi16(mullo, sum), _mm_cvtsi32_si128(5)); \
    \
    deltaPos = _mm_add_epi16(deltaPos, ipAngle); \
    deltaFract = _mm_and_si128(deltaPos, thirty1); \
    mullo = _mm_mullo_epi16(row21, _mm_sub_epi16(thirty2, deltaFract)); \
    sum = _mm_add_epi16(_mm_set1_epi16(16), _mm_mullo_epi16(deltaFract, row22)); \
    row21 = _mm_sra_epi16(_mm_add_epi16(mullo, sum), _mm_cvtsi32_si128(5)); \
    \
    deltaPos = _mm_add_epi16(deltaPos, ipAngle); \
    deltaFract = _mm_and_si128(deltaPos, thirty1); \
    mullo = _mm_mullo_epi16(row31, _mm_sub_epi16(thirty2, deltaFract)); \
    sum = _mm_add_epi16(_mm_set1_epi16(16), _mm_mullo_epi16(deltaFract, row32)); \
    row31 = _mm_sra_epi16(_mm_add_epi16(mullo, sum), _mm_cvtsi32_si128(5)); \
    \
    deltaPos = _mm_add_epi16(deltaPos, ipAngle); \
    deltaFract = _mm_and_si128(deltaPos, thirty1); \
    mullo = _mm_mullo_epi16(row41, _mm_sub_epi16(thirty2, deltaFract)); \
    sum = _mm_add_epi16(_mm_set1_epi16(16), _mm_mullo_epi16(deltaFract, row42)); \
    row41 = _mm_sra_epi16(_mm_add_epi16(mullo, sum), _mm_cvtsi32_si128(5)); \
    if (modeHor) \
    { \
        __m128i _tmp1, _tmp2, _tmp3, _tmp4; \
        \
        _tmp1 = _mm_unpacklo_epi16(row11, row31); \
        _tmp2 = _mm_unpacklo_epi16(row21, row41); \
        _tmp3 = _mm_unpacklo_epi16(_tmp1, _tmp2); \
        _tmp4 = _mm_unpackhi_epi16(_tmp1, _tmp2); \
        \
        tmp16_1 = _mm_packus_epi16(_tmp3, _tmp3); \
        *(uint32_t*)(dst) = _mm_cvtsi128_si32(tmp16_1); \
        _tmp2 = tmp16_1; \
        _tmp2 = _mm_srl_epi64(_tmp2, _mm_cvtsi32_si128(32)); \
        *(uint32_t*)(dst + dstStride) = _mm_cvtsi128_si32(_tmp2); \
        tmp16_1 = _mm_packus_epi16(_tmp4, _tmp4); \
        *(uint32_t*)(dst + (2 * dstStride)) = _mm_cvtsi128_si32(tmp16_1); \
        _tmp2 = tmp16_1; \
        _tmp2 = _mm_srl_epi64(_tmp2, _mm_cvtsi32_si128(32)); \
        *(uint32_t*)(dst + (3 * dstStride)) = _mm_cvtsi128_si32(_tmp2); \
    } \
    else \
    { \
        *(uint32_t*)(dst) = _mm_cvtsi128_si32(_mm_packus_epi16(row11, row11)); \
        *(uint32_t*)(dst + dstStride) = _mm_cvtsi128_si32(_mm_packus_epi16(row21, row21)); \
        *(uint32_t*)(dst + (2 * dstStride)) = _mm_cvtsi128_si32(_mm_packus_epi16(row31, row31)); \
        *(uint32_t*)(dst + (3 * dstStride)) = _mm_cvtsi128_si32(_mm_packus_epi16(row41, row41)); \
    }

void predIntraAng4_32(pixel* dst, int dstStride, pixel *refMain, int /*dirMode*/)
{
    __m128i tmp16_1;

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 2));
    *(uint32_t*)(dst) = _mm_cvtsi128_si32(tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 3));
    *(uint32_t*)(dst + dstStride) = _mm_cvtsi128_si32(tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 4));
    *(uint32_t*)(dst + 2 * dstStride) = _mm_cvtsi128_si32(tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 5));
    *(uint32_t*)(dst + 3 * dstStride) = _mm_cvtsi128_si32(tmp16_1);
}

void predIntraAng4_26(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row12;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row22 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row31 = row22;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(24));

    tmp16_2 = tmp2;
    row32 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row41 = row32;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(32));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(26);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_21(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row12;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row22 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row31 = row21;
    row32 = row22;

    row41 = row22;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(24));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(21);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_17(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row12;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row22 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row31 = row21;
    row32 = row22;

    row41 = row22;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(24));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(17);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_13(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row11;
    row22 = row12;
    row31 = row12;

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row32 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row41 = row31;
    row42 = row32;

    deltaPos = ipAngle = _mm_set1_epi16(13);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_9(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row11;
    row22 = row12;
    row31 = row11;
    row32 = row12;
    row41 = row12;

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(9);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_5(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row11;
    row22 = row12;
    row31 = row11;
    row32 = row12;
    row41 = row11;
    row42 = row12;

    deltaPos = ipAngle = _mm_set1_epi16(5);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_2(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 1));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row11;
    row22 = row12;
    row31 = row11;
    row32 = row12;
    row41 = row11;
    row42 = row12;

    deltaPos = ipAngle = _mm_set1_epi16(2);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_2(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain));
    row11   = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row11;
    row22 = row12;
    row31 = row11;
    row32 = row12;
    row41 = row11;
    row42 = row12;

    deltaPos = ipAngle = _mm_set1_epi16(-2);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_5(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain));
    row11 = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row11;
    row22 = row12;
    row31 = row11;
    row32 = row12;
    row41 = row11;
    row42 = row12;

    deltaPos = ipAngle = _mm_set1_epi16(-5);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_9(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 1));
    row41 = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row11 = row42;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row42;
    row22 = row12;
    row31 = row42;
    row32 = row12;

    deltaPos = ipAngle = _mm_set1_epi16(-9);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_13(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 1));

    row41 = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row11 = row42;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row42;
    row22 = row12;
    row31 = row41;
    row32 = row42;

    deltaPos = ipAngle = _mm_set1_epi16(-13);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_17(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 2));
    row41 = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row31 = row42;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row32 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row31;
    row22 = row32;
    row11 = row32;

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(24));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(-17);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_21(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 2));
    row41 = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row31 = row42;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row32 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row31;
    row22 = row32;
    row11 = row32;

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(24));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(-21);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_26(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_4_START();

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 3));
    row41 = _mm_unpacklo_epi8(tmp16_1, _mm_setzero_si128());

    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(8));

    tmp16_2 = tmp2;
    row42 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row31 = row42;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(16));

    tmp16_2 = tmp2;
    row32 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row21 = row32;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(24));

    tmp16_2 = tmp2;
    row22 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    row11 = row22;
    tmp2 = tmp16_1;
    tmp2 = _mm_srl_epi64(tmp2, _mm_cvtsi32_si128(32));

    tmp16_2 = tmp2;
    row12 = _mm_unpacklo_epi8(tmp16_2, _mm_setzero_si128());

    deltaPos = ipAngle = _mm_set1_epi16(-26);

    PRED_INTRA_ANGLE_4_END();
}

void predIntraAng4_m_32(pixel* dst, int dstStride, pixel *refMain, int /*dirMode*/)
{
    __m128i tmp16_1;

    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain));
    *(uint32_t*)(dst) = _mm_cvtsi128_si32(tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 1));
    *(uint32_t*)(dst + dstStride) = _mm_cvtsi128_si32(tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 2));
    *(uint32_t*)(dst + 2 * dstStride) = _mm_cvtsi128_si32(tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain - 3));
    *(uint32_t*)(dst + 3 * dstStride) = _mm_cvtsi128_si32(tmp16_1);
}

typedef void (*predIntraAng4x4_func)(pixel* dst, int dstStride, pixel *refMain, int dirMode);
predIntraAng4x4_func predIntraAng4[] =
{
    /* PredIntraAng4_0 is replaced with PredIntraAng4_2. For PredIntraAng4_0 we are going through default path in the
     * xPredIntraAng4x4 because we cannot afford to pass large number arguments for this function. */
    predIntraAng4_32,
    predIntraAng4_26,
    predIntraAng4_21,
    predIntraAng4_17,
    predIntraAng4_13,
    predIntraAng4_9,
    predIntraAng4_5,
    predIntraAng4_2,
    predIntraAng4_2,    // Intentionally wrong! It should be "PredIntraAng4_0" here.
    predIntraAng4_m_2,
    predIntraAng4_m_5,
    predIntraAng4_m_9,
    predIntraAng4_m_13,
    predIntraAng4_m_17,
    predIntraAng4_m_21,
    predIntraAng4_m_26,
    predIntraAng4_m_32,
    predIntraAng4_m_26,
    predIntraAng4_m_21,
    predIntraAng4_m_17,
    predIntraAng4_m_13,
    predIntraAng4_m_9,
    predIntraAng4_m_5,
    predIntraAng4_m_2,
    predIntraAng4_2,    // Intentionally wrong! It should be "PredIntraAng4_0" here.
    predIntraAng4_2,
    predIntraAng4_5,
    predIntraAng4_9,
    predIntraAng4_13,
    predIntraAng4_17,
    predIntraAng4_21,
    predIntraAng4_26,
    predIntraAng4_32
};

void intraPredAng4x4(pixel* dst, int dstStride, int width, int dirMode, pixel *refLeft, pixel *refAbove, bool bFilter = true)
{
    assert(dirMode > 1); //no planar and dc
    static const int mode_to_angle_table[] = { 32, 26, 21, 17, 13, 9, 5, 2, 0, -2, -5, -9, -13, -17, -21, -26, -32, -26, -21, -17, -13, -9, -5, -2, 0, 2, 5, 9, 13, 17, 21, 26, 32 };
    static const int mode_to_invAng_table[] = { 256, 315, 390, 482, 630, 910, 1638, 4096, 0, 4096, 1638, 910, 630, 482, 390, 315, 256, 315, 390, 482, 630, 910, 1638, 4096, 0, 4096, 1638, 910, 630, 482, 390, 315, 256 };
    int intraPredAngle = mode_to_angle_table[dirMode - 2];
    int invAngle       = mode_to_invAng_table[dirMode - 2];

    bool modeHor       = (dirMode < 18);
    bool modeVer       = !modeHor;

    // Do angular predictions
    pixel* refMain;
    pixel* refSide;

    // Initialize the Main and Left reference array.
    if (intraPredAngle < 0)
    {
        int blkSize = width;
        refMain = (modeVer ? refAbove : refLeft);     // + (blkSize - 1);
        refSide = (modeVer ? refLeft : refAbove);     // + (blkSize - 1);

        // Extend the Main reference to the left.
        int invAngleSum    = 128;     // rounding for (shift by 8)
        for (int k = -1; k > blkSize * intraPredAngle >> 5; k--)
        {
            invAngleSum += invAngle;
            refMain[k] = refSide[invAngleSum >> 8];
        }
    }
    else
    {
        refMain = modeVer ? refAbove : refLeft;
        refSide = modeVer ? refLeft  : refAbove;
    }

    // bfilter will always be true for exactly vertical/horizontal modes
    if (intraPredAngle == 0)  // Exactly horizontal/vertical angles
    {
        if (modeHor)
        {
            __m128i main, temp16;
            main = _mm_loadl_epi64((const __m128i*)(refMain + 1));
            temp16 = _mm_unpacklo_epi8(main, main);
            temp16 = _mm_unpacklo_epi8(temp16, temp16);

            if (bFilter)
            {
                __m128i temp, side;
                temp = _mm_loadl_epi64((__m128i*)refSide);
                side = _mm_shufflelo_epi16(temp, 0);
                side = _mm_unpacklo_epi64(side, side);
                side = _mm_and_si128(side, _mm_set1_epi16(0x00FF));
                __m128i tempshift = temp;
                tempshift = _mm_srl_epi64(tempshift, _mm_cvtsi32_si128(8));
                temp = tempshift;
                __m128i side1 = _mm_unpacklo_epi8(temp, _mm_setzero_si128());
                __m128i row = _mm_unpacklo_epi8(temp16, _mm_setzero_si128());
                side1 = _mm_sub_epi16(side1, side);
                side1 = _mm_sra_epi16(side1, _mm_cvtsi32_si128(1));
                row = _mm_add_epi16(row, side1);
                row = _mm_min_epi16(_mm_max_epi16(_mm_set1_epi16(0), row), _mm_set1_epi16(255));
                __m128i res = _mm_packus_epi16(row, _mm_set1_epi16(0));
                *(uint32_t*)dst = _mm_cvtsi128_si32(res);
            }
            else
            {
                *(uint32_t*)dst = _mm_cvtsi128_si32(temp16);
            }

            __m128i temp = temp16;
            temp = _mm_srl_epi64(temp, _mm_cvtsi32_si128(32));
            *(uint32_t*)(dst + dstStride) = _mm_cvtsi128_si32(temp);
            temp = _mm_unpackhi_epi64(temp16, temp16);
            *(uint32_t*)(dst + 2 * dstStride) = _mm_cvtsi128_si32(temp);
            temp = _mm_srl_epi64(temp, _mm_cvtsi32_si128(32));
            *(uint32_t*)(dst + 3 * dstStride) = _mm_cvtsi128_si32(temp);
        }
        else
        {
            __m128i main = _mm_loadl_epi64((const __m128i*)(refMain + 1));
            *(uint32_t*)(dst) = _mm_cvtsi128_si32(main);
            *(uint32_t*)(dst + dstStride) = _mm_cvtsi128_si32(main);
            *(uint32_t*)(dst + 2 * dstStride) = _mm_cvtsi128_si32(main);
            *(uint32_t*)(dst + 3 * dstStride) = _mm_cvtsi128_si32(main);

            if (bFilter)
            {
                for (int k = 0; k < 4; k++)
                {
                    dst[k * dstStride] = (pixel)Clip3((int16_t)0, (int16_t)((1 << 8) - 1), static_cast<int16_t>((dst[k * dstStride]) + ((refSide[k + 1] - refSide[0]) >> 1)));
                }
            }
        }
    }
    else
    {
        predIntraAng4[dirMode - 2](dst, dstStride, refMain, dirMode);
    }
}

#define PRED_INTRA_ANGLE_8_START() \
    /* Map the mode index to main prediction direction and angle*/ \
    bool modeHor       = (dirMode < 18); \
    bool modeVer       = !modeHor; \
    int intraPredAngle = modeVer ? (int)dirMode - VER_IDX : modeHor ? -((int)dirMode - HOR_IDX) : 0; \
    int absAng         = abs(intraPredAngle); \
    int signAng        = intraPredAngle < 0 ? -1 : 1; \
    /* Set bitshifts and scale the angle parameter to block size*/ \
    int angTable[9]    = { 0,    2,    5,   9,  13,  17,  21,  26,  32 }; \
    absAng             = angTable[absAng]; \
    intraPredAngle     = signAng * absAng; \
    if (modeHor) /* Near horizontal modes*/ \
    { \
        __m128i row11, row12, row1, row2, row3, row4; \
        __m128i tmp16_1, tmp16_2, tmp, tmp1, tmp2, deltaFract; \
        __m128i deltaPos = _mm_set1_epi16(0); \
        __m128i ipAngle  = _mm_set1_epi16(intraPredAngle); \
        __m128i thirty1  = _mm_set1_epi16(31); \
        __m128i thirty2  = _mm_set1_epi16(32); \
        __m128i lowm, highm; \
        __m128i mask = _mm_set1_epi32(0x00FF00FF); \
        __m128i mullo, sum;

#define LOAD_ROW(ROW, X) \
    tmp = _mm_loadl_epi64((__m128i*)(refMain + 1 + X)); \
    ROW = _mm_unpacklo_epi8(tmp, _mm_setzero_si128());

#define CALC_ROW(RES, ROW1, ROW2) \
    deltaPos = _mm_add_epi16(deltaPos, ipAngle); \
    deltaFract = _mm_and_si128(deltaPos, thirty1); \
    mullo = _mm_mullo_epi16(ROW1, _mm_sub_epi16(thirty2, deltaFract)); \
    sum = _mm_add_epi16(_mm_set1_epi16(16), _mm_mullo_epi16(deltaFract, ROW2)); \
    RES = _mm_sra_epi16(_mm_add_epi16(mullo, sum), _mm_cvtsi32_si128(5));

#define PREDANG_CALC_ROW_VER(X) \
    LOAD_ROW(row11, GETAP(lookIdx, X)); \
    LOAD_ROW(row12, GETAP(lookIdx, X) + 1); \
    CALC_ROW(row11, row11, row12); \
    lowm  = _mm_and_si128(row11, mask); \
    highm = _mm_and_si128(row11, mask); \
    _mm_storel_epi64((__m128i*)(dst + (X * dstStride)), _mm_packus_epi16(lowm, highm));

#define PREDANG_CALC_ROW_HOR(X, rowx) \
    LOAD_ROW(row11, GETAP(lookIdx, X)); \
    LOAD_ROW(row12, GETAP(lookIdx, X) + 1); \
    CALC_ROW(rowx, row11, row12);

#define PRED_INTRA_ANGLE_8_MIDDLE() \
    tmp16_1 = _mm_unpacklo_epi8(row1, row2); \
    tmp16_2 = _mm_unpackhi_epi8(row1, row2); \
    row1 = tmp16_1; \
    row2 = tmp16_2; \
    tmp16_1 = _mm_unpacklo_epi8(row3, row4); \
    tmp16_2 = _mm_unpackhi_epi8(row3, row4); \
    row3 = tmp16_1; \
    row4 = tmp16_2; \
    tmp16_1 = _mm_unpacklo_epi8(row1, row2); \
    tmp16_2 = _mm_unpackhi_epi8(row1, row2); \
    row1 = tmp16_1; \
    row2 = tmp16_2; \
    tmp16_1 = _mm_unpacklo_epi8(row3, row4); \
    tmp16_2 = _mm_unpackhi_epi8(row3, row4); \
    row3 = tmp16_1; \
    row4 = tmp16_2; \
    tmp16_1 = _mm_unpacklo_epi32(row1, row3); \
    tmp16_2 = _mm_unpackhi_epi32(row1, row3); \
    row1 = tmp16_1; \
    row3 = tmp16_2; \
    tmp16_1 = _mm_unpacklo_epi32(row2, row4); \
    tmp16_2 = _mm_unpackhi_epi32(row2, row4); \
    row2 = tmp16_1; \
    row4 = tmp16_2; \
    _mm_storel_epi64((__m128i*)(dst), row1); \
    _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), row3); \
    _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), row2); \
    _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), row4); \
        \
    row1 = _mm_unpackhi_epi64(row1, row1); \
    _mm_storel_epi64((__m128i*)(dst + 1 * dstStride), row1); \
    row1 = _mm_unpackhi_epi64(row3, row3); \
    _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), row1); \
    row1 = _mm_unpackhi_epi64(row2, row2); \
    _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), row1); \
    row1 = _mm_unpackhi_epi64(row4, row4); \
    _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), row1); \
    } \
    else /* Vertical modes*/ \
    { \
        __m128i row11, row12; \
        __m128i tmp, deltaFract; \
        __m128i deltaPos = _mm_set1_epi16(0); \
        __m128i ipAngle  = _mm_set1_epi16(intraPredAngle); \
        __m128i thirty1  = _mm_set1_epi16(31); \
        __m128i thirty2  = _mm_set1_epi16(32); \
        __m128i mullo, sum; \
        __m128i lowm, highm; \
        __m128i mask = _mm_set1_epi32(0x00FF00FF);

void predIntraAng8_32(pixel* dst, int dstStride, pixel *refMain, int /*dirMode*/)
{
    __m128i tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 2));

    _mm_storel_epi64((__m128i*)(dst), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 3));
    _mm_storel_epi64((__m128i*)(dst + dstStride), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 4));
    _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 5));
    _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 6));
    _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 7));
    _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 8));
    _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), tmp16_1);
    tmp16_1 = _mm_loadl_epi64((__m128i*)(refMain + 9));
    _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), tmp16_1);
}

void predIntraAng8_26(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    // Map the mode index to main prediction direction and angle
    bool modeHor       = (dirMode < 18);
    bool modeVer       = !modeHor;
    int intraPredAngle = modeVer ? (int)dirMode - VER_IDX : modeHor ? -((int)dirMode - HOR_IDX) : 0;
    int lookIdx = intraPredAngle;
    int absAng         = abs(intraPredAngle);
    int signAng        = intraPredAngle < 0 ? -1 : 1;

    // Set bitshifts and scale the angle parameter to block size
    int angTable[9]    = { 0,    2,    5,   9,  13,  17,  21,  26,  32 };

    absAng             = angTable[absAng];
    intraPredAngle     = signAng * absAng;

    if (modeHor) // Near horizontal modes
    {
        __m128i row11, row12, row1, row2, row3, row4; \
        __m128i tmp16_1, tmp16_2, tmp, tmp1, tmp2, deltaFract; \
        __m128i deltaPos = _mm_set1_epi16(0); \
        __m128i ipAngle  = _mm_set1_epi16(intraPredAngle); \
        __m128i thirty1  = _mm_set1_epi16(31); \
        __m128i thirty2  = _mm_set1_epi16(32); \
        __m128i lowm, highm; \
        __m128i mask = _mm_set1_epi32(0x00FF00FF); \
        __m128i mullo, sum; \


        PREDANG_CALC_ROW_HOR(0, tmp1);
        PREDANG_CALC_ROW_HOR(1, tmp2);

        lowm  = _mm_and_si128(tmp1, mask);
        highm = _mm_and_si128(tmp2, mask);
        row1 = _mm_packus_epi16(lowm, highm);

        PREDANG_CALC_ROW_HOR(2, tmp1);
        PREDANG_CALC_ROW_HOR(3, tmp2);

        lowm  = _mm_and_si128(tmp1, mask);
        highm = _mm_and_si128(tmp2, mask);
        row2 = _mm_packus_epi16(lowm, highm);

        PREDANG_CALC_ROW_HOR(4, tmp1);
        PREDANG_CALC_ROW_HOR(5, tmp2);

        lowm  = _mm_and_si128(tmp1, mask);
        highm = _mm_and_si128(tmp2, mask);
        row3 = _mm_packus_epi16(lowm, highm);

        PREDANG_CALC_ROW_HOR(6, tmp1);
        PREDANG_CALC_ROW_HOR(7, tmp2);

        lowm  = _mm_and_si128(tmp1, mask);
        highm = _mm_and_si128(tmp2, mask);
        row4 = _mm_packus_epi16(lowm, highm);

        PRED_INTRA_ANGLE_8_MIDDLE();

        PREDANG_CALC_ROW_VER(0);
        PREDANG_CALC_ROW_VER(1);
        PREDANG_CALC_ROW_VER(2);
        PREDANG_CALC_ROW_VER(3);
        PREDANG_CALC_ROW_VER(4);
        PREDANG_CALC_ROW_VER(5);
        PREDANG_CALC_ROW_VER(6);
        PREDANG_CALC_ROW_VER(7);
    }
}

void predIntraAng8_5(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_8_START();

    LOAD_ROW(row11, 0);
    LOAD_ROW(row12, 1);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row1 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row2 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row3 = _mm_packus_epi16(lowm, highm);

    row11 = row12;
    LOAD_ROW(row12, 2);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row4 = _mm_packus_epi16(lowm, highm);

    PRED_INTRA_ANGLE_8_MIDDLE();

    __m128i tmp1, tmp2;

    LOAD_ROW(row11, 0);
    LOAD_ROW(row12, 1);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), _mm_packus_epi16(lowm, highm));

    row11 = row12;
    LOAD_ROW(row12, 2);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), _mm_packus_epi16(lowm, highm));
}
}

void predIntraAng8_2(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_8_START();

    LOAD_ROW(row11, 0);
    LOAD_ROW(row12, 1);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row1 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row2 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row3 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row4 = _mm_packus_epi16(lowm, highm);

    PRED_INTRA_ANGLE_8_MIDDLE();

    __m128i tmp1, tmp2;

    LOAD_ROW(row11, 0);
    LOAD_ROW(row12, 1);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), _mm_packus_epi16(lowm, highm));
}
}

void predIntraAng8_m_2(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_8_START();

    LOAD_ROW(row11, -1);
    LOAD_ROW(row12, 0);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row1 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row2 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row3 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row4 = _mm_packus_epi16(lowm, highm);

    PRED_INTRA_ANGLE_8_MIDDLE();

    __m128i tmp1, tmp2;

    LOAD_ROW(row11, -1);
    LOAD_ROW(row12, 0);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), _mm_packus_epi16(lowm, highm));
}
}

void predIntraAng8_m_5(pixel* dst, int dstStride, pixel *refMain, int dirMode)
{
    PRED_INTRA_ANGLE_8_START();

    LOAD_ROW(row11, -1);
    LOAD_ROW(row12, 0);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row1 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row2 = _mm_packus_epi16(lowm, highm);

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row3 = _mm_packus_epi16(lowm, highm);

    row12 = row11;
    LOAD_ROW(row11, -2);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp2, mask);
    row4 = _mm_packus_epi16(lowm, highm);

    PRED_INTRA_ANGLE_8_MIDDLE();

    __m128i tmp1, tmp2;

    LOAD_ROW(row11, -1);
    LOAD_ROW(row12, 0);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);

    _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), _mm_packus_epi16(lowm, highm));

    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), _mm_packus_epi16(lowm, highm));

    row12 = row11;
    LOAD_ROW(row11, -2);
    CALC_ROW(tmp1, row11, row12);
    CALC_ROW(tmp2, row11, row12);

    lowm  = _mm_and_si128(tmp1, mask);
    highm = _mm_and_si128(tmp1, mask);
    _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), _mm_packus_epi16(lowm, highm));
    lowm  = _mm_and_si128(tmp2, mask);
    highm = _mm_and_si128(tmp2, mask);
    _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), _mm_packus_epi16(lowm, highm));
}
}

typedef void (*predIntraAng8x8_func)(pixel* dst, int dstStride, pixel *refMain, int dirMode);
predIntraAng8x8_func predIntraAng8[] =
{
    /* PredIntraAng8_0 is replaced with PredIntraAng8_2. For PredIntraAng8_0 we are going through default path
     * in the xPredIntraAng8x8 because we cannot afford to pass large number arguments for this function. */
    predIntraAng8_32,
    predIntraAng8_26,
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_21" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_17" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_13" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_9" here.
    predIntraAng8_5,
    predIntraAng8_2,
    predIntraAng8_2,        //Intentionally wrong! It should be "PredIntraAng8_0" here.
    predIntraAng8_m_2,
    predIntraAng8_m_5,
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_9" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_13" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_17" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_21" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_26" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_32" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_26" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_21" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_17" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_13" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_m_9" here.
    predIntraAng8_m_5,
    predIntraAng8_m_2,
    predIntraAng8_2,        //Intentionally wrong! It should be "PredIntraAng8_0" here.
    predIntraAng8_2,
    predIntraAng8_5,
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_9" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_13" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_17" here.
    predIntraAng8_26,       //Intentionally wrong! It should be "PredIntraAng8_21" here.
    predIntraAng8_26,
    predIntraAng8_32
};

void intraPredAng8x8(pixel* dst, int dstStride, int width, int dirMode, pixel *refLeft, pixel *refAbove, bool bFilter = true)
{
    int k;
    int blkSize = width;

    assert(dirMode > 1); // not planar or dc
    static const int mode_to_angle_table[] = { 32, 26, 21, 17, 13, 9, 5, 2, 0, -2, -5, -9, -13, -17, -21, -26, -32, -26, -21, -17, -13, -9, -5, -2, 0, 2, 5, 9, 13, 17, 21, 26, 32 };
    static const int mode_to_invAng_table[] = { 256, 315, 390, 482, 630, 910, 1638, 4096, 0, 4096, 1638, 910, 630, 482, 390, 315, 256, 315, 390, 482, 630, 910, 1638, 4096, 0, 4096, 1638, 910, 630, 482, 390, 315, 256 };
    int intraPredAngle = mode_to_angle_table[dirMode - 2];
    int invAngle       = mode_to_invAng_table[dirMode - 2];
    bool modeHor       = (dirMode < 18);
    bool modeVer       = !modeHor;
    pixel* refMain;
    pixel* refSide;

    // Initialize the Main and Left reference array.
    if (intraPredAngle < 0)
    {
        refMain = (modeVer ? refAbove : refLeft); // + (blkSize - 1);
        refSide = (modeVer ? refLeft : refAbove); // + (blkSize - 1);

        // Extend the Main reference to the left.
        int invAngleSum    = 128;     // rounding for (shift by 8)
        for (k = -1; k > blkSize * intraPredAngle >> 5; k--)
        {
            invAngleSum += invAngle;
            refMain[k] = refSide[invAngleSum >> 8];
        }
    }
    else
    {
        refMain = modeVer ? refAbove : refLeft;
        refSide = modeVer ? refLeft  : refAbove;
    }

    // bfilter will always be true for blocksize 8
    if (intraPredAngle == 0)  // Exactly horizontal/vertical angles
    {
        if (modeHor)
        {
            __m128i temp, temp1;
            temp = _mm_loadu_si128((__m128i const*)(refMain + 1));
            __m128i main = _mm_unpacklo_epi8(temp, _mm_setzero_si128());

            if (bFilter)
            {
                __m128i side0 = _mm_set1_epi16(refSide[0]);
                __m128i temp16;

                temp16 = _mm_loadu_si128((__m128i const*)(refSide + 1));
                __m128i side = _mm_unpacklo_epi8(temp16, _mm_setzero_si128());

                __m128i row = _mm_shufflelo_epi16(main, 0);
                row = _mm_unpacklo_epi64(row, row);

                side = _mm_sub_epi16(side, side0);
                side = _mm_sra_epi16(side, _mm_cvtsi32_si128(1));
                row = _mm_add_epi16(row, side);
                row = _mm_min_epi16(_mm_max_epi16(_mm_set1_epi16(0), row), _mm_set1_epi16((1 << X265_DEPTH) - 1));

                __m128i mask  = _mm_set1_epi32(0x00FF00FF);
                __m128i lowm  = _mm_and_si128(row, mask);
                __m128i highm = _mm_and_si128(row, mask);
                temp1 = _mm_packus_epi16(lowm, highm);

                _mm_storel_epi64((__m128i*)(dst), temp1);
            }
            else
            {
                temp1 = _mm_shuffle_epi8(temp, _mm_setzero_si128());
                _mm_storel_epi64((__m128i*)(dst), temp1);
            }

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(1));
            _mm_storel_epi64((__m128i*)(dst + 1 * dstStride), temp1);

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(2));
            _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), temp1);

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(3));
            _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), temp1);

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(4));
            _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), temp1);

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(5));
            _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), temp1);

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(6));
            _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), temp1);

            temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(7));
            _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), temp1);
        }
        else
        {
            __m128i main = _mm_loadl_epi64((__m128i*)(refMain + 1));

            _mm_storel_epi64((__m128i*)(dst + 0 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 1 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 2 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 3 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 4 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 5 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 6 * dstStride), main);
            _mm_storel_epi64((__m128i*)(dst + 7 * dstStride), main);

            if (bFilter)
            {
                __m128i temp;
                __m128i side0 = _mm_set1_epi16(refSide[0]);

                temp =  _mm_loadu_si128((__m128i const*)(refSide + 1));
                __m128i side = _mm_unpacklo_epi8(temp, _mm_setzero_si128());

                temp = _mm_loadu_si128((const __m128i*)(refMain + 1));
                __m128i mask = _mm_setr_epi8(0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1);
                __m128i row = _mm_shuffle_epi8(temp, mask);
                side = _mm_sub_epi16(side, side0);
                side = _mm_sra_epi16(side, _mm_cvtsi32_si128(1));
                row = _mm_add_epi16(row, side);
                row = _mm_min_epi16(_mm_max_epi16(_mm_set1_epi16(0), row), _mm_set1_epi16((1 << X265_DEPTH) - 1));

                uint8_t tmp[16];
                _mm_storeu_si128((__m128i*)tmp, row);

                dst[0 * dstStride] = tmp[0];
                dst[1 * dstStride] = tmp[2];
                dst[2 * dstStride] = tmp[4];
                dst[3 * dstStride] = tmp[6];
                dst[4 * dstStride] = tmp[8];
                dst[5 * dstStride] = tmp[10];
                dst[6 * dstStride] = tmp[12];
                dst[7 * dstStride] = tmp[14];
            }
        }
    }
    else
    {
        predIntraAng8[dirMode - 2](dst, dstStride, refMain, dirMode);
    }
}

// 16x16
#define PREDANG_CALCROW_VER(X) \
    LOADROW(row11L, row11H, GETAP(lookIdx, X)); \
    LOADROW(row12L, row12H, GETAP(lookIdx, X) + 1); \
    CALCROW(row11L, row11H, row11L, row11H, row12L, row12H); \
    itmp = _mm_packus_epi16(row11L, row11H); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride)), itmp);

#define PREDANG_CALCROW_HOR(X, rowx) \
    LOADROW(row11L, row11H, GETAP(lookIdx, (X))); \
    LOADROW(row12L, row12H, GETAP(lookIdx, (X)) + 1); \
    CALCROW(row11L, row11H, row11L, row11H, row12L, row12H); \
    rowx = _mm_packus_epi16(row11L, row11H);

#define LOADROW(ROWL, ROWH, X) \
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (X))); \
    ROWL = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    ROWH = _mm_unpackhi_epi8(itmp, _mm_setzero_si128());

#define CALCROW(RESL, RESH, ROW1L, ROW1H, ROW2L, ROW2H) \
    v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle); \
    v_deltaFract = _mm_and_si128(v_deltaPos, thirty1); \
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, ROW1L); \
    it3 = _mm_mullo_epi16(v_deltaFract, ROW2L); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    RESL = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, ROW1H); \
    it3 = _mm_mullo_epi16(v_deltaFract, ROW2H); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    RESH = _mm_srai_epi16(it2, 5);

#define BLND2_16(R1, R2) \
    itmp1 = _mm_unpacklo_epi8(R1, R2); \
    itmp2 = _mm_unpackhi_epi8(R1, R2); \
    R1 = itmp1; \
    R2 = itmp2;

#define MB4(R1, R2, R3, R4) \
    BLND2_16(R1, R2) \
    BLND2_16(R3, R4) \
    itmp1 = _mm_unpacklo_epi16(R1, R3); \
    itmp2 = _mm_unpackhi_epi16(R1, R3); \
    R1 = itmp1; \
    R3 = itmp2; \
    itmp1 = _mm_unpacklo_epi16(R2, R4); \
    itmp2 = _mm_unpackhi_epi16(R2, R4); \
    R2 = itmp1; \
    R4 = itmp2;

#define BLND2_4(R1, R2) \
    itmp1 = _mm_unpacklo_epi32(R1, R2); \
    itmp2 = _mm_unpackhi_epi32(R1, R2); \
    R1 = itmp1; \
    R2 = itmp2;

#define BLND2_2(R1, R2) \
    itmp1 = _mm_unpacklo_epi64(R1, R2); \
    itmp2 = _mm_unpackhi_epi64(R1, R2); \
    _mm_storeu_si128((__m128i*)dst, itmp1); \
    dst += dstStride; \
    _mm_storeu_si128((__m128i*)dst, itmp2); \
    dst += dstStride;

#define CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, X) \
    PREDANG_CALCROW_HOR(0 + X, R1) \
    PREDANG_CALCROW_HOR(1 + X, R2) \
    PREDANG_CALCROW_HOR(2 + X, R3) \
    PREDANG_CALCROW_HOR(3 + X, R4) \
    PREDANG_CALCROW_HOR(4 + X, R5) \
    PREDANG_CALCROW_HOR(5 + X, R6) \
    PREDANG_CALCROW_HOR(6 + X, R7) \
    PREDANG_CALCROW_HOR(7 + X, R8) \
    MB4(R1, R2, R3, R4) \
    MB4(R5, R6, R7, R8) \
    BLND2_4(R1, R5); \
    BLND2_4(R2, R6); \
    BLND2_4(R3, R7); \
    BLND2_4(R4, R8);

void intraPredAng16x16(pixel* dst, int dstStride, int width, int dirMode, pixel *refLeft, pixel *refAbove, bool bFilter = true)
{
    int k;
    int blkSize        = width;

    // Map the mode index to main prediction direction and angle
    assert(dirMode > 1); //no planar and dc
    bool modeHor       = (dirMode < 18);
    bool modeVer       = !modeHor;
    int intraPredAngle = modeVer ? (int)dirMode - VER_IDX : modeHor ? -((int)dirMode - HOR_IDX) : 0;
    int lookIdx = intraPredAngle;
    int absAng         = abs(intraPredAngle);
    int signAng        = intraPredAngle < 0 ? -1 : 1;

    // Set bitshifts and scale the angle parameter to block size
    int angTable[9]    = { 0,    2,    5,   9,  13,  17,  21,  26,  32 };
    int invAngTable[9] = { 0, 4096, 1638, 910, 630, 482, 390, 315, 256 }; // (256 * 32) / Angle
    int invAngle       = invAngTable[absAng];
    absAng             = angTable[absAng];
    intraPredAngle     = signAng * absAng;

    // Do angular predictions

    pixel* refMain;
    pixel* refSide;

    // Initialise the Main and Left reference array.
    if (intraPredAngle < 0)
    {
        refMain = (modeVer ? refAbove : refLeft);     // + (blkSize - 1);
        refSide = (modeVer ? refLeft : refAbove);     // + (blkSize - 1);

        // Extend the Main reference to the left.
        int invAngleSum    = 128;     // rounding for (shift by 8)
        if (intraPredAngle != -32)
            for (k = -1; k > blkSize * intraPredAngle >> 5; k--)
            {
                invAngleSum += invAngle;
                refMain[k] = refSide[invAngleSum >> 8];
            }
    }
    else
    {
        refMain = modeVer ? refAbove : refLeft;
        refSide = modeVer ? refLeft  : refAbove;
    }

    // bfilter will always be true for blocksize 8
    if (intraPredAngle == 0)  // Exactly hotizontal/vertical angles
    {
        if (modeHor)
        {
            __m128i v_temp;
            __m128i tmp1;
            v_temp = _mm_loadu_si128((__m128i*)(refMain + 1));

            if (bFilter)
            {
                __m128i v_side_0 = _mm_set1_epi16(refSide[0]); // refSide[0] value in a vector
                __m128i v_temp16;
                v_temp16 = _mm_loadu_si128((__m128i*)(refSide + 1));
                __m128i v_side;
                v_side = _mm_unpacklo_epi8(v_temp16, _mm_setzero_si128());

                __m128i row01, row02, ref;
                ref = _mm_set1_epi16(refMain[1]);
                v_side = _mm_sub_epi16(v_side, v_side_0);
                v_side = _mm_srai_epi16(v_side, 1);
                row01 = _mm_add_epi16(ref, v_side);
                row01 = _mm_min_epi16(_mm_max_epi16(_mm_setzero_si128(), row01), _mm_set1_epi16((1 << X265_DEPTH) - 1));

                v_side = _mm_unpackhi_epi8(v_temp16, _mm_setzero_si128());
                v_side = _mm_sub_epi16(v_side, v_side_0);
                v_side = _mm_srai_epi16(v_side, 1);
                row02 = _mm_add_epi16(ref, v_side);
                row02 = _mm_min_epi16(_mm_max_epi16(_mm_setzero_si128(), row02), _mm_set1_epi16((1 << X265_DEPTH) - 1));

                tmp1 = _mm_packus_epi16(row01, row02);
                _mm_storeu_si128((__m128i*)dst, tmp1);            //row0
            }
            else
            {
                tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(0));
                _mm_storeu_si128((__m128i*)dst, tmp1); //row0
            }

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(1));
            _mm_storeu_si128((__m128i*)(dst + (1 * dstStride)), tmp1); //row1

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(2));
            _mm_storeu_si128((__m128i*)(dst + (2 * dstStride)), tmp1); //row2

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(3));
            _mm_storeu_si128((__m128i*)(dst + (3 * dstStride)), tmp1); //row3

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(4));
            _mm_storeu_si128((__m128i*)(dst + (4 * dstStride)), tmp1); //row4

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(5));
            _mm_storeu_si128((__m128i*)(dst + (5 * dstStride)), tmp1); //row5

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(6));
            _mm_storeu_si128((__m128i*)(dst + (6 * dstStride)), tmp1); //row6

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(7));
            _mm_storeu_si128((__m128i*)(dst + (7 * dstStride)), tmp1); //row7

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(8));
            _mm_storeu_si128((__m128i*)(dst + (8 * dstStride)), tmp1); //row8

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(9));
            _mm_storeu_si128((__m128i*)(dst + (9 * dstStride)), tmp1); //row9

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(10));
            _mm_storeu_si128((__m128i*)(dst + (10 * dstStride)), tmp1); //row10

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(11));
            _mm_storeu_si128((__m128i*)(dst + (11 * dstStride)), tmp1); //row11

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(12));
            _mm_storeu_si128((__m128i*)(dst + (12 * dstStride)), tmp1); //row12

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(13));
            _mm_storeu_si128((__m128i*)(dst + (13 * dstStride)), tmp1); //row13

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(14));
            _mm_storeu_si128((__m128i*)(dst + (14 * dstStride)), tmp1); //row14

            tmp1 = _mm_shuffle_epi8(v_temp, _mm_set1_epi8(15));
            _mm_storeu_si128((__m128i*)(dst + (15 * dstStride)), tmp1); //row15
        }
        else
        {
            __m128i v_main;
            v_main = _mm_loadu_si128((__m128i const*)(refMain + 1));

            _mm_storeu_si128((__m128i*)dst, v_main);
            _mm_storeu_si128((__m128i*)(dst + dstStride), v_main);
            _mm_storeu_si128((__m128i*)(dst + (2 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (3 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (4 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (5 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (6 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (7 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (8 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (9 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (10 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (11 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (12 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (13 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (14 * dstStride)), v_main);
            _mm_storeu_si128((__m128i*)(dst + (15 * dstStride)), v_main);

            if (bFilter)
            {
                __m128i v_temp;
                __m128i v_side_0 = _mm_set1_epi16(refSide[0]); // refSide[0] value in a vector

                v_temp = _mm_loadu_si128((__m128i*)(refSide + 1));
                __m128i v_side;
                v_side = _mm_unpacklo_epi8(v_temp, _mm_setzero_si128());

                __m128i row0, ref;
                ref = _mm_set1_epi16(refMain[1]);
                v_side = _mm_sub_epi16(v_side, v_side_0);
                v_side = _mm_srai_epi16(v_side, 1);
                row0 = _mm_add_epi16(ref, v_side);
                row0 = _mm_min_epi16(_mm_max_epi16(_mm_setzero_si128(), row0), _mm_set1_epi16((1 << X265_DEPTH) - 1));

                uint16_t x[8];
                _mm_storeu_si128((__m128i*)x, row0);
                dst[0 * dstStride] = x[0];
                dst[1 * dstStride] = x[1];
                dst[2 * dstStride] = x[2];
                dst[3 * dstStride] = x[3];
                dst[4 * dstStride] = x[4];
                dst[5 * dstStride] = x[5];
                dst[6 * dstStride] = x[6];
                dst[7 * dstStride] = x[7];

                v_side = _mm_unpackhi_epi8(v_temp, _mm_setzero_si128());
                v_side = _mm_sub_epi16(v_side, v_side_0);
                v_side = _mm_srai_epi16(v_side, 1);
                row0 = _mm_add_epi16(ref, v_side);
                row0 = _mm_min_epi16(_mm_max_epi16(_mm_setzero_si128(), row0), _mm_set1_epi16((1 << X265_DEPTH) - 1));

                _mm_storeu_si128((__m128i*)x, row0);
                dst[8 * dstStride] = x[0];
                dst[9 * dstStride] = x[1];
                dst[10 * dstStride] = x[2];
                dst[11 * dstStride] = x[3];
                dst[12 * dstStride] = x[4];
                dst[13 * dstStride] = x[5];
                dst[14 * dstStride] = x[6];
                dst[15 * dstStride] = x[7];
            }
        }
    }
    else if (intraPredAngle == -32)
    {
        __m128i v_refSide;
        v_refSide = _mm_loadu_si128((__m128i*)refSide);
        __m128i temp = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        v_refSide = _mm_shuffle_epi8(v_refSide, temp);
        pixel refMain0 = refMain[0];

        _mm_storeu_si128((__m128i*)(refMain - 15), v_refSide);
        refMain[0] = refMain0;

        __m128i itmp;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);

        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)--refMain);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);

        return;
    }
    else if (intraPredAngle == 32)
    {
        __m128i itmp;
        refMain += 2;

        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        _mm_storeu_si128((__m128i*)dst, itmp);

        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);
        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        dst += dstStride;
        _mm_storeu_si128((__m128i*)dst, itmp);

        return;
    }
    else
    {
        if (modeHor)
        {
            __m128i row11L, row12L, row11H, row12H;
            __m128i v_deltaFract;
            __m128i v_deltaPos = _mm_setzero_si128();
            __m128i thirty2 = _mm_set1_epi16(32);
            __m128i thirty1 = _mm_set1_epi16(31);
            __m128i v_ipAngle = _mm_set1_epi16(intraPredAngle);
            __m128i R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16;
            __m128i itmp, itmp1, itmp2, it1, it2, it3, i16;
//            MB16;
            CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, 0)
            CALC_BLND_8ROWS(R9, R10, R11, R12, R13, R14, R15, R16, 8)
            BLND2_2(R1, R9)
            BLND2_2(R5, R13)
            BLND2_2(R3, R11)
            BLND2_2(R7, R15)
            BLND2_2(R2, R10)
            BLND2_2(R6, R14)
            BLND2_2(R4, R12)
            BLND2_2(R8, R16)
        }
        else
        {
            __m128i row11L, row12L, row11H, row12H;
            __m128i v_deltaFract;
            __m128i v_deltaPos = _mm_setzero_si128();
            __m128i thirty2 = _mm_set1_epi16(32);
            __m128i thirty1 = _mm_set1_epi16(31);
            __m128i v_ipAngle = _mm_set1_epi16(intraPredAngle);
            __m128i itmp, it1, it2, it3, i16;

            PREDANG_CALCROW_VER(0);
            PREDANG_CALCROW_VER(1);
            PREDANG_CALCROW_VER(2);
            PREDANG_CALCROW_VER(3);
            PREDANG_CALCROW_VER(4);
            PREDANG_CALCROW_VER(5);
            PREDANG_CALCROW_VER(6);
            PREDANG_CALCROW_VER(7);
            PREDANG_CALCROW_VER(8);
            PREDANG_CALCROW_VER(9);
            PREDANG_CALCROW_VER(10);
            PREDANG_CALCROW_VER(11);
            PREDANG_CALCROW_VER(12);
            PREDANG_CALCROW_VER(13);
            PREDANG_CALCROW_VER(14);
            PREDANG_CALCROW_VER(15);
        }
    }
}

#undef PREDANG_CALCROW_VER
#undef PREDANG_CALCROW_HOR
#undef LOADROW
#undef CALCROW
#undef BLND2_16
#undef BLND2_2
#undef BLND2_4
#undef MB4
#undef CALC_BLND_8ROWS

//32x32
#define PREDANG_CALCROW_VER(X) \
    v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle); \
    v_deltaFract = _mm_and_si128(v_deltaPos, thirty1); \
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (angAP[8 - (lookIdx)][(X)]))); \
    row11L = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    row11H = _mm_unpackhi_epi8(itmp, _mm_setzero_si128()); \
\
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (angAP[8 - (lookIdx)][(X)] + 1))); \
    row12L = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    row12H = _mm_unpackhi_epi8(itmp, _mm_setzero_si128()); \
\
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, row11L); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12L); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    row11L = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, row11H); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12H); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    row11H = _mm_srai_epi16(it2, 5); \
\
    itmp = _mm_packus_epi16(row11L, row11H); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride)), itmp); \
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (angAP[8 - (lookIdx)][(X)] + 16))); \
    row11L = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    row11H = _mm_unpackhi_epi8(itmp, _mm_setzero_si128()); \
\
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (angAP[8 - (lookIdx)][(X)] + 17))); \
    row12L = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    row12H = _mm_unpackhi_epi8(itmp, _mm_setzero_si128()); \
\
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, row11L); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12L); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    row11L = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, row11H); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12H); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    row11H = _mm_srai_epi16(it2, 5); \
\
    itmp = _mm_packus_epi16(row11L, row11H); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride) + 16), itmp);

#define PREDANG_CALCROW_VER_MODE2(X) \
    v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle); \
    v_deltaFract = _mm_and_si128(v_deltaPos, thirty1); \
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, row11); \
    it3 = _mm_mullo_epi16(v_deltaFract, row21); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    res1 = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, row12); \
    it3 = _mm_mullo_epi16(v_deltaFract, row22); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    res2 = _mm_srai_epi16(it2, 5); \
\
    itmp = _mm_packus_epi16(res1, res2); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride)), itmp); \
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, row13); \
    it3 = _mm_mullo_epi16(v_deltaFract, row23); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    res1 = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, row14); \
    it3 = _mm_mullo_epi16(v_deltaFract, row24); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    res2 = _mm_srai_epi16(it2, 5); \
\
    itmp = _mm_packus_epi16(res1, res2); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride) + 16), itmp);

#define PREDANG_CALCROW_HOR(X, rowx) \
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (angAP[8 - (lookIdx)][((X))]))); \
    row11L = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    row11H = _mm_unpackhi_epi8(itmp, _mm_setzero_si128()); \
\
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (angAP[8 - (lookIdx)][((X))] + 1))); \
    row12L = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    row12H = _mm_unpackhi_epi8(itmp, _mm_setzero_si128()); \
\
    v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle); \
    v_deltaFract = _mm_and_si128(v_deltaPos, thirty1); \
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, row11L); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12L); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    row11L = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, row11H); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12H); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    row11H = _mm_srai_epi16(it2, 5); \
    rowx = _mm_packus_epi16(row11L, row11H);

#define PREDANG_CALCROW_HOR_MODE2(rowx) \
    v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle); \
    v_deltaFract = _mm_and_si128(v_deltaPos, thirty1); \
    it1 = _mm_sub_epi16(thirty2, v_deltaFract); \
    it2 = _mm_mullo_epi16(it1, row11L); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12L); \
    it2 = _mm_add_epi16(it2, it3); \
    i16 = _mm_set1_epi16(16); \
    it2 = _mm_add_epi16(it2, i16); \
    res1 = _mm_srai_epi16(it2, 5); \
    it2 = _mm_mullo_epi16(it1, row11H); \
    it3 = _mm_mullo_epi16(v_deltaFract, row12H); \
    it2 = _mm_add_epi16(it2, it3); \
    it2 = _mm_add_epi16(it2, i16); \
    res2 = _mm_srai_epi16(it2, 5); \
    rowx = _mm_packus_epi16(res1, res2);

#define LOADROW(ROWL, ROWH, X) \
    itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + (X))); \
    ROWL = _mm_unpacklo_epi8(itmp, _mm_setzero_si128()); \
    ROWH = _mm_unpackhi_epi8(itmp, _mm_setzero_si128());

#define BLND2_2(R1, R2) \
    itmp1 = _mm_unpacklo_epi64(R1, R2); \
    itmp2 = _mm_unpackhi_epi64(R1, R2); \
    _mm_storeu_si128((__m128i*)dst, itmp1); \
    dst += dstStride; \
    _mm_storeu_si128((__m128i*)dst, itmp2); \
    dst += dstStride;

#define MB8(R1, R2, R3, R4, R5, R6, R7, R8) \
    itmp1 = _mm_unpacklo_epi8(R1, R2); \
    itmp2 = _mm_unpackhi_epi8(R1, R2); \
    R1 = itmp1; \
    R2 = itmp2; \
    itmp1 = _mm_unpacklo_epi8(R3, R4); \
    itmp2 = _mm_unpackhi_epi8(R3, R4); \
    R3 = itmp1; \
    R4 = itmp2; \
    itmp1 = _mm_unpacklo_epi16(R1, R3); \
    itmp2 = _mm_unpackhi_epi16(R1, R3); \
    R1 = itmp1; \
    R3 = itmp2; \
    itmp1 = _mm_unpacklo_epi16(R2, R4); \
    itmp2 = _mm_unpackhi_epi16(R2, R4); \
    R2 = itmp1; \
    R4 = itmp2; \
    itmp1 = _mm_unpacklo_epi8(R5, R6); \
    itmp2 = _mm_unpackhi_epi8(R5, R6); \
    R5 = itmp1; \
    R6 = itmp2; \
    itmp1 = _mm_unpacklo_epi8(R7, R8); \
    itmp2 = _mm_unpackhi_epi8(R7, R8); \
    R7 = itmp1; \
    R8 = itmp2; \
    itmp1 = _mm_unpacklo_epi16(R5, R7); \
    itmp2 = _mm_unpackhi_epi16(R5, R7); \
    R5 = itmp1; \
    R7 = itmp2; \
    itmp1 = _mm_unpacklo_epi16(R6, R8); \
    itmp2 = _mm_unpackhi_epi16(R6, R8); \
    R6 = itmp1; \
    R8 = itmp2; \
    itmp1 = _mm_unpacklo_epi32(R1, R5); \
    itmp2 = _mm_unpackhi_epi32(R1, R5); \
    R1 = itmp1; \
    R5 = itmp2; \
\
    itmp1 = _mm_unpacklo_epi32(R2, R6); \
    itmp2 = _mm_unpackhi_epi32(R2, R6); \
    R2 = itmp1; \
    R6 = itmp2; \
\
    itmp1 = _mm_unpacklo_epi32(R3, R7); \
    itmp2 = _mm_unpackhi_epi32(R3, R7); \
    R3 = itmp1; \
    R7 = itmp2; \
\
    itmp1 = _mm_unpacklo_epi32(R4, R8); \
    itmp2 = _mm_unpackhi_epi32(R4, R8); \
    R4 = itmp1; \
    R8 = itmp2;

#define CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, X) \
    PREDANG_CALCROW_HOR(0 + X, R1) \
    PREDANG_CALCROW_HOR(1 + X, R2) \
    PREDANG_CALCROW_HOR(2 + X, R3) \
    PREDANG_CALCROW_HOR(3 + X, R4) \
    PREDANG_CALCROW_HOR(4 + X, R5) \
    PREDANG_CALCROW_HOR(5 + X, R6) \
    PREDANG_CALCROW_HOR(6 + X, R7)

#define CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8) \
    PREDANG_CALCROW_HOR_MODE2(R1) \
    PREDANG_CALCROW_HOR_MODE2(R2) \
    PREDANG_CALCROW_HOR_MODE2(R3) \
    PREDANG_CALCROW_HOR_MODE2(R4) \
    PREDANG_CALCROW_HOR_MODE2(R5) \
    PREDANG_CALCROW_HOR_MODE2(R6) \
    PREDANG_CALCROW_HOR_MODE2(R7) \

void intraPredAng32x32(pixel* dst, int dstStride, int width, int dirMode, pixel *refLeft, pixel *refAbove)
{
    int k;
    int blkSize = width;

    // Map the mode index to main prediction direction and angle
    assert(dirMode > 1); //no planar and dc
    bool modeHor       = (dirMode < 18);
    bool modeVer       = !modeHor;
    int intraPredAngle = modeVer ? (int)dirMode - VER_IDX : modeHor ? -((int)dirMode - HOR_IDX) : 0;
    int lookIdx = intraPredAngle;
    int absAng         = abs(intraPredAngle);
    int signAng        = intraPredAngle < 0 ? -1 : 1;

    // Set bitshifts and scale the angle parameter to block size
    int angTable[9]    = { 0,    2,    5,   9,  13,  17,  21,  26,  32 };
    int invAngTable[9] = { 0, 4096, 1638, 910, 630, 482, 390, 315, 256 }; // (256 * 32) / Angle
    int invAngle       = invAngTable[absAng];
    absAng             = angTable[absAng];
    intraPredAngle     = signAng * absAng;

    // Do angular predictions

    pixel* refMain;
    pixel* refSide;

    // Initialize the Main and Left reference array.
    if (intraPredAngle < 0)
    {
        refMain = (modeVer ? refAbove : refLeft);     // + (blkSize - 1);
        refSide = (modeVer ? refLeft : refAbove);     // + (blkSize - 1);

        // Extend the Main reference to the left.
        int invAngleSum    = 128;     // rounding for (shift by 8)
        if (intraPredAngle != -32)
            for (k = -1; k > blkSize * intraPredAngle >> 5; k--)
            {
                invAngleSum += invAngle;
                refMain[k] = refSide[invAngleSum >> 8];
            }
    }
    else
    {
        refMain = modeVer ? refAbove : refLeft;
        refSide = modeVer ? refLeft  : refAbove;
    }

    // bfilter will always be true for blocksize 8
    if (intraPredAngle == 0)  // Exactly horizontal/vertical angles
    {
        if (modeHor)
        {
            __m128i temp, temp1;

#define BROADCAST_STORE(X) \
    temp1 = _mm_shuffle_epi8(temp, _mm_set1_epi8(X)); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride)), temp1); \
    _mm_storeu_si128((__m128i*)(dst + ((X)*dstStride) + 16), temp1); \

            temp = _mm_loadu_si128((__m128i const*)(refMain + 1));

            for (int i = 0; i < 16; i += 4) // BROADCAST & STORE 16 ROWS
            {
                BROADCAST_STORE(i)
                BROADCAST_STORE(i + 1)
                BROADCAST_STORE(i + 2)
                BROADCAST_STORE(i + 3)
            }

            dst += 16 * dstStride;
            temp = _mm_loadu_si128((__m128i const*)(refMain + 1 + 16));

            for (int i = 0; i < 16; i += 4) // BROADCAST & STORE 16 ROWS
            {
                BROADCAST_STORE(i)
                BROADCAST_STORE(i + 1)
                BROADCAST_STORE(i + 2)
                BROADCAST_STORE(i + 3)
            }
        }
        else
        {
            __m128i v_main;
            Pel *dstOriginal = dst;
            v_main = _mm_loadu_si128((__m128i const*)(refMain + 1));
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);

            dst = dstOriginal + 16;
            v_main = _mm_loadu_si128((__m128i const*)(refMain + 17));

            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
            dst += dstStride;
            _mm_storeu_si128((__m128i*)(dst), v_main);
        }
    }
    else if (intraPredAngle == -32)
    {
        __m128i ref_side;
        __m128i mask = _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        pixel refMain0 = refMain[0];

        ref_side = _mm_loadu_si128((__m128i const*)(refSide));
        ref_side = _mm_shuffle_epi8(ref_side, mask);
        _mm_storeu_si128((__m128i*)(refMain - 15), ref_side);

        ref_side = _mm_loadu_si128((__m128i const*)(refSide + 16));
        ref_side = _mm_shuffle_epi8(ref_side, mask);
        _mm_storeu_si128((__m128i*)(refMain - 31), ref_side);

        refMain[0] = refMain0;

        __m128i itmp;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst + (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 16));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain--;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);

        return;
    }
    else if (intraPredAngle == 32)
    {
        __m128i itmp;
        refMain += 2;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain++);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        _mm_storeu_si128((__m128i*)dst, itmp);
        dst += dstStride;
        refMain++;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        itmp = _mm_loadu_si128((__m128i const*)refMain);
        refMain++;
        _mm_storeu_si128((__m128i*)dst, itmp);
        _mm_storeu_si128((__m128i*)(dst - (16 * dstStride) + 16), itmp);
        itmp = _mm_loadu_si128((__m128i const*)(refMain + 15));
        _mm_storeu_si128((__m128i*)(dst + 16), itmp);
        dst += dstStride;

        return;
    }
    else
    {
        if (modeHor)
        {
            __m128i row11L, row12L, row11H, row12H, res1, res2;
            __m128i v_deltaFract, v_deltaPos, thirty2, thirty1, v_ipAngle;
            __m128i R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16;

            Pel * original_pDst = dst;
            v_deltaPos = _mm_setzero_si128(); //v_deltaPos = 0;
            v_ipAngle = _mm_set1_epi16(intraPredAngle);
            thirty2 = _mm_set1_epi16(32);
            thirty1 = _mm_set1_epi16(31);
            __m128i itmp, itmp1, itmp2, it1, it2, it3, i16;

            switch (intraPredAngle)
            {
            case -2:
                LOADROW(row11L, row11H, -1)
                LOADROW(row12L, row12H,  0)
                R16 = _mm_packus_epi16(row11L, row11H); //R16 = compress(row11L, row11H);

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)

                v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle);
                v_deltaFract = _mm_and_si128(v_deltaPos, thirty1);
                row12L = row11L;
                row12H = row11H;
                LOADROW(row11L, row11H, -2)
                R16 = _mm_packus_epi16(row11L, row11H);
                dst = original_pDst + 16;

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)

                dst = original_pDst + (16 * dstStride);
                refMain += 16;

                v_deltaPos = _mm_setzero_si128();
                v_ipAngle = _mm_set1_epi16(intraPredAngle);
                LOADROW(row11L, row11H, -1)
                LOADROW(row12L, row12H,  0)
                R16 = _mm_packus_epi16(row11L, row11H);

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)

                v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle);
                v_deltaFract = _mm_and_si128(v_deltaPos, thirty1);
                row12L = row11L;
                row12H = row11H;
                LOADROW(row11L, row11H, -2)
                R16 = _mm_packus_epi16(row11L, row11H);
                dst = original_pDst + (16 * dstStride) + 16;

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)
                return;

            case  2:
                LOADROW(row11L, row11H, 0)
                LOADROW(row12L, row12H, 1)
                R16 = _mm_packus_epi16(row12L, row12H);

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)

                v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle);
                v_deltaFract = _mm_and_si128(v_deltaPos, thirty1);
                row11L = row12L;
                row11H = row12H;
                LOADROW(row12L, row12H, 2)
                R16 = _mm_packus_epi16(row12L, row12H);
                dst = original_pDst + 16;

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)

                dst = original_pDst + (16 * dstStride);
                refMain += 16;
                v_deltaPos = _mm_setzero_si128();

                v_ipAngle = _mm_set1_epi16(intraPredAngle);
                LOADROW(row11L, row11H, 0)
                LOADROW(row12L, row12H, 1)
                R16 = _mm_packus_epi16(row12L, row12H);

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)

                v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle);
                v_deltaFract = _mm_and_si128(v_deltaPos, thirty1);
                row11L = row12L;
                row11H = row12H;
                LOADROW(row12L, row12H, 2)
                R16 = _mm_packus_epi16(row12L, row12H);
                dst = original_pDst + (16 * dstStride) + 16;

                CALC_BLND_8ROWS_MODE2(R1, R2, R3, R4, R5, R6, R7, R8)
                PREDANG_CALCROW_HOR_MODE2(R8)
                MB8(R1, R2, R3, R4, R5, R6, R7, R8)
                CALC_BLND_8ROWS_MODE2(R9, R10, R11, R12, R13, R14, R15, R16)
                MB8(R9, R10, R11, R12, R13, R14, R15, R16)
                BLND2_2(R1, R9)
                BLND2_2(R5, R13)
                BLND2_2(R3, R11)
                BLND2_2(R7, R15)
                BLND2_2(R2, R10)
                BLND2_2(R6, R14)
                BLND2_2(R4, R12)
                BLND2_2(R8, R16)
                return;
            }

            CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, 0)
            PREDANG_CALCROW_HOR(7 + 0, R8)
            MB8(R1, R2, R3, R4, R5, R6, R7, R8)
            CALC_BLND_8ROWS(R9, R10, R11, R12, R13, R14, R15, R16, 8)
            PREDANG_CALCROW_HOR(7 + 8, R16)
            MB8(R9, R10, R11, R12, R13, R14, R15, R16)
            BLND2_2(R1, R9)
            BLND2_2(R5, R13)
            BLND2_2(R3, R11)
            BLND2_2(R7, R15)
            BLND2_2(R2, R10)
            BLND2_2(R6, R14)
            BLND2_2(R4, R12)
            BLND2_2(R8, R16)

            dst = original_pDst + 16;

            CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, 16)
            PREDANG_CALCROW_HOR(7 + 16, R8)
            MB8(R1, R2, R3, R4, R5, R6, R7, R8)
            CALC_BLND_8ROWS(R9, R10, R11, R12, R13, R14, R15, R16, 24)
            R16 = _mm_loadu_si128((__m128i const*)(refMain + 1 + GETAP(lookIdx, 31)));
            MB8(R9, R10, R11, R12, R13, R14, R15, R16)
            BLND2_2(R1, R9)
            BLND2_2(R5, R13)
            BLND2_2(R3, R11)
            BLND2_2(R7, R15)
            BLND2_2(R2, R10)
            BLND2_2(R6, R14)
            BLND2_2(R4, R12)
            BLND2_2(R8, R16)

            dst = original_pDst + (16 * dstStride);
            refMain += 16;
            v_deltaPos = _mm_setzero_si128();
            v_ipAngle = _mm_set1_epi16(intraPredAngle);

            CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, 0)
            PREDANG_CALCROW_HOR(7 + 0, R8)
            MB8(R1, R2, R3, R4, R5, R6, R7, R8)
            CALC_BLND_8ROWS(R9, R10, R11, R12, R13, R14, R15, R16, 8)
            PREDANG_CALCROW_HOR(7 + 8, R16)
            MB8(R9, R10, R11, R12, R13, R14, R15, R16)
            BLND2_2(R1, R9)
            BLND2_2(R5, R13)
            BLND2_2(R3, R11)
            BLND2_2(R7, R15)
            BLND2_2(R2, R10)
            BLND2_2(R6, R14)
            BLND2_2(R4, R12)
            BLND2_2(R8, R16)
            dst = original_pDst + (16 * dstStride) + 16;

            CALC_BLND_8ROWS(R1, R2, R3, R4, R5, R6, R7, R8, 16)
            PREDANG_CALCROW_HOR(7 + 16, R8)
            MB8(R1, R2, R3, R4, R5, R6, R7, R8)
            CALC_BLND_8ROWS(R9, R10, R11, R12, R13, R14, R15, R16, 24)
            R16 = _mm_loadu_si128((__m128i const*)(refMain + 1 + GETAP(lookIdx, 31)));
            MB8(R9, R10, R11, R12, R13, R14, R15, R16)
            BLND2_2(R1, R9)
            BLND2_2(R5, R13)
            BLND2_2(R3, R11)
            BLND2_2(R7, R15)
            BLND2_2(R2, R10)
            BLND2_2(R6, R14)
            BLND2_2(R4, R12)
            BLND2_2(R8, R16)
        }
        else
        {
            __m128i row11L, row12L, row11H, row12H;
            __m128i v_deltaFract, v_deltaPos, thirty2, thirty1, v_ipAngle;
            __m128i row11, row12, row13, row14, row21, row22, row23, row24;
            __m128i res1, res2;

            v_deltaPos = _mm_setzero_si128(); //v_deltaPos = 0;
            v_ipAngle = _mm_set1_epi16(intraPredAngle);
            thirty2 = _mm_set1_epi16(32);
            thirty1 = _mm_set1_epi16(31);
            __m128i itmp, it1, it2, it3, i16;

            switch (intraPredAngle)
            {
            case -2:
                LOADROW(row11, row12, -1)
                LOADROW(row21, row22,  0)
                LOADROW(row13, row14, 15)
                LOADROW(row23, row24, 16)
                for (int i = 0; i <= 14; i++)
                {
                    PREDANG_CALCROW_VER_MODE2(i);
                }

                //deltaFract == 0 for 16th row
                v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle);
                v_deltaFract = _mm_and_si128(v_deltaPos, thirty1);
                itmp = _mm_packus_epi16(row11, row12);
                _mm_storeu_si128((__m128i*)(dst + ((15) * dstStride)), itmp);
                itmp = _mm_packus_epi16(row13, row14);
                _mm_storeu_si128((__m128i*)(dst + ((15) * dstStride) + 16), itmp);

                row21 = row11;
                row22 = row12;
                row23 = row13;
                row24 = row14;

                LOADROW(row11, row12, -2)
                LOADROW(row13, row14, 14)
                for (int i = 16; i <= 30; i++)
                {
                    PREDANG_CALCROW_VER_MODE2(i);
                }

                itmp = _mm_packus_epi16(row11, row12);
                _mm_storeu_si128((__m128i*)(dst + ((31) * dstStride)), itmp);
                itmp = _mm_packus_epi16(row13, row14);
                _mm_storeu_si128((__m128i*)(dst + ((31) * dstStride) + 16), itmp);

                return;

            case  2:

                LOADROW(row11, row12, 0)
                LOADROW(row21, row22, 1)
                LOADROW(row13, row14, 16)
                LOADROW(row23, row24, 17)
                for (int i = 0; i <= 14; i++)
                {
                    PREDANG_CALCROW_VER_MODE2(i);
                }

                //deltaFract == 0 for 16th row

                v_deltaPos = _mm_add_epi16(v_deltaPos, v_ipAngle);
                v_deltaFract = _mm_and_si128(v_deltaPos, thirty1);
                itmp = _mm_packus_epi16(row21, row22);
                _mm_storeu_si128((__m128i*)(dst + ((15) * dstStride)), itmp);
                itmp = _mm_packus_epi16(row23, row24);
                _mm_storeu_si128((__m128i*)(dst + ((15) * dstStride) + 16), itmp);

                row11 = row21;
                row12 = row22;
                row13 = row23;
                row14 = row24;

                LOADROW(row21, row22, 2)
                LOADROW(row23, row24, 18)
                for (int i = 16; i <= 30; i++)
                {
                    PREDANG_CALCROW_VER_MODE2(i);
                }

                itmp = _mm_packus_epi16(row21, row22);
                _mm_storeu_si128((__m128i*)(dst + ((31) * dstStride)), itmp);
                itmp = _mm_packus_epi16(row23, row24);
                _mm_storeu_si128((__m128i*)(dst + ((31) * dstStride) + 16), itmp);

                return;
            }

            for (int i = 0; i <= 30; i++)
            {
                PREDANG_CALCROW_VER(i);
            }

            itmp = _mm_loadu_si128((__m128i const*)(refMain + 1 + GETAP(lookIdx, 31)));
            _mm_storeu_si128((__m128i*)(dst + ((31) * dstStride)), itmp);
            itmp = _mm_loadu_si128((__m128i const*)(refMain + 17 + GETAP(lookIdx, 31)));
            _mm_storeu_si128((__m128i*)(dst + ((31) * dstStride) + 16), itmp);
        }
    }
}

#undef PREDANG_CALCROW_VER
#undef PREDANG_CALCROW_HOR
#undef LOADROW
#undef CALCROW
#undef BLND2_16
#undef BLND2_2
#undef BLND2_4
#undef MB4
#undef CALC_BLND_8ROWS

void intra_pred_ang(pixel* dst, int dstStride, int width, int dirMode, bool bFilter, pixel *refLeft, pixel *refAbove)
{
    switch (width)
    {
    case 4:
        intraPredAng4x4(dst, dstStride, width, dirMode, refLeft, refAbove, bFilter);
        return;
    case 8:
        intraPredAng8x8(dst, dstStride, width, dirMode, refLeft, refAbove, bFilter);
        return;
    case 16:
        intraPredAng16x16(dst, dstStride, width, dirMode, refLeft, refAbove, bFilter);
        return;
    case 32:
        intraPredAng32x32(dst, dstStride, width, dirMode, refLeft, refAbove);
        return;
    }
}

#endif // !HIGH_BIT_DEPTH
}

#if HIGH_BIT_DEPTH

#if defined(_MSC_VER)
#define ALWAYSINLINE  __forceinline
#endif

#define INSTRSET 3
#include "vectorclass.h"

namespace {
inline void predDCFiltering(pixel* above, pixel* left, pixel* dst, intptr_t dstStride, int width)
{
    int y;
    pixel pixDC = *dst;
    int pixDCx3 = pixDC * 3 + 2;

    // boundary pixels processing
    dst[0] = (pixel)((above[0] + left[0] + 2 * pixDC + 2) >> 2);

    Vec8us im1(pixDCx3);
    Vec8us im2, im3;
    switch (width)
    {
    case 4:
        im2 = load_partial(const_int(8), &above[1]);
        im2 = (im1 + im2) >> const_int(2);
        store_partial(const_int(8), &dst[1], im2);
        break;

    case 8:
        im2.load(&above[1]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1]);
        break;

    case 16:
        im2.load(&above[1]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1]);

        im2.load(&above[1 + 8]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 8]);
        break;

    case 32:
        im2.load(&above[1]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1]);

        im2.load(&above[1 + 8]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 8]);

        im2.load(&above[1 + 16]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 16]);

        im2.load(&above[1 + 24]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 24]);
        break;

    //case 64:
    default:
        im2.load(&above[1]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1]);

        im2.load(&above[1 + 8]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 8]);

        im2.load(&above[1 + 16]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 16]);

        im2.load(&above[1 + 24]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 24]);

        im2.load(&above[1 + 32]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 32]);

        im2.load(&above[1 + 40]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 40]);

        im2.load(&above[1 + 48]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 48]);

        im2.load(&above[1 + 56]);
        im2 = (im1 + im2) >> const_int(2);
        im2.store(&dst[1 + 56]);
        break;
    }

    for (y = 1; y < width; y++)
    {
        dst[dstStride] = (pixel)((left[y] + pixDCx3) >> 2);
        dst += dstStride;
    }
}

void intra_pred_dc(pixel* above, pixel* left, pixel* dst, intptr_t dstStride, int width, int filter)
{
    int sum;
    int logSize = g_convertToBit[width] + 2;

    Vec8s sumLeft(0);
    Vec8s sumAbove(0);
    Vec8s m0;

    switch (width)
    {
    case 4:
        sumLeft  = load_partial(const_int(8), left);
        sumAbove = load_partial(const_int(8), above);
        break;
    case 8:
        m0.load(left);
        sumLeft = m0;
        m0.load(above);
        sumAbove = m0;
        break;
    case 16:
        m0.load(left);
        sumLeft  = m0;
        m0.load(left + 8);
        sumLeft += m0;

        m0.load(above);
        sumAbove  = m0;
        m0.load(above + 8);
        sumAbove += m0;
        break;
    case 32:
        m0.load(left);
        sumLeft  = m0;
        m0.load(left + 8);
        sumLeft += m0;
        m0.load(left + 16);
        sumLeft += m0;
        m0.load(left + 24);
        sumLeft += m0;

        m0.load(above);
        sumAbove  = m0;
        m0.load(above + 8);
        sumAbove += m0;
        m0.load(above + 16);
        sumAbove += m0;
        m0.load(above + 24);
        sumAbove += m0;
        break;
    //case 64:
    default:
        // CHECK_ME: the max support bit_depth is 13-bits
        m0.load(left);
        sumLeft  = m0;
        m0.load(left + 8);
        sumLeft += m0;
        m0.load(left + 16);
        sumLeft += m0;
        m0.load(left + 24);
        sumLeft += m0;
        m0.load(left + 32);
        sumLeft += m0;
        m0.load(left + 40);
        sumLeft += m0;
        m0.load(left + 48);
        sumLeft += m0;
        m0.load(left + 56);
        sumLeft += m0;

        m0.load(above);
        sumAbove  = m0;
        m0.load(above + 8);
        sumAbove += m0;
        m0.load(above + 16);
        sumAbove += m0;
        m0.load(above + 24);
        sumAbove += m0;
        m0.load(above + 32);
        sumAbove += m0;
        m0.load(above + 40);
        sumAbove += m0;
        m0.load(above + 48);
        sumAbove += m0;
        m0.load(above + 56);
        sumAbove += m0;
        break;
    }

    sum = horizontal_add_x(sumAbove) + horizontal_add_x(sumLeft);

    logSize += 1;
    pixel dcVal = (sum + (1 << (logSize - 1))) >> logSize;
    Vec8us dcValN(dcVal);
    int k;

    pixel *dst1 = dst;
    switch (width)
    {
    case 4:
        store_partial(const_int(8), dst1, dcValN);
        dst1 += dstStride;
        store_partial(const_int(8), dst1, dcValN);
        dst1 += dstStride;
        store_partial(const_int(8), dst1, dcValN);
        dst1 += dstStride;
        store_partial(const_int(8), dst1, dcValN);
        dst1 += dstStride;
        break;

    case 8:
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        dcValN.store(dst1);
        dst1 += dstStride;
        break;

    case 16:
        for (k = 0; k < 16; k += 2)
        {
            dcValN.store(dst1);
            dcValN.store(dst1 + 8);
            dst1 += dstStride;
            dcValN.store(dst1);
            dcValN.store(dst1 + 8);
            dst1 += dstStride;
        }

        break;

    case 32:
        for (k = 0; k < 32; k++)
        {
            dcValN.store(dst1);
            dcValN.store(dst1 +  8);
            dcValN.store(dst1 + 16);
            dcValN.store(dst1 + 24);
            dst1 += dstStride;
        }

        break;

    //case 64:
    default:
        for (k = 0; k < 64; k++)
        {
            dcValN.store(dst1);
            dcValN.store(dst1 +  8);
            dcValN.store(dst1 + 16);
            dcValN.store(dst1 + 24);
            dcValN.store(dst1 + 32);
            dcValN.store(dst1 + 40);
            dcValN.store(dst1 + 48);
            dcValN.store(dst1 + 56);
            dst1 += dstStride;
        }

        break;
    }

    if (filter)
    {
        predDCFiltering(above, left, dst, dstStride, width);
    }
}

// CHECK_ME: I am not sure the v_rightColumnN will be overflow when input is 12bpp
void intra_pred_planar4(pixel* above, pixel* left, pixel* dst, intptr_t dstStride)
{
    int bottomLeft, topRight;
    // NOTE: I use 16-bits is enough here, because we have least than 13-bits as input, and shift left by 2, it is 15-bits

    // Get left and above reference column and row
    Vec8s v_topRow = (Vec8s)load_partial(const_int(8), above); // topRow

    Vec8s v_leftColumn = (Vec8s)load_partial(const_int(8), left);   // leftColumn

    // Prepare intermediate variables used in interpolation
    bottomLeft = left[4];
    topRight   = above[4];

    Vec8s v_bottomLeft(bottomLeft);
    Vec8s v_topRight(topRight);

    Vec8s v_bottomRow = v_bottomLeft - v_topRow;
    Vec8s v_rightColumn = v_topRight - v_leftColumn;

    v_topRow = v_topRow << const_int(2);
    v_leftColumn = v_leftColumn << const_int(2);

    // Generate prediction signal
    Vec8s v_horPred4 = v_leftColumn + Vec8s(4);
    const Vec8s v_multi(1, 2, 3, 4, 5, 6, 7, 8);
    Vec8s v_horPred, v_rightColumnN;
    Vec8s v_im4;
    Vec16uc v_im5;

    // line0
    v_horPred = broadcast(const_int(0), v_horPred4);
    v_rightColumnN = broadcast(const_int(0), v_rightColumn) * v_multi;
    v_horPred = v_horPred + v_rightColumnN;
    v_topRow = v_topRow + v_bottomRow;
    // CHECK_ME: the HM don't clip the pixel, so I assume there is biggest 12+3=15(bits)
    v_im4 = (Vec8s)(v_horPred + v_topRow) >> const_int(3);
    store_partial(const_int(8), &dst[0 * dstStride], v_im4);

    // line1
    v_horPred = broadcast(const_int(1), v_horPred4);
    v_rightColumnN = broadcast(const_int(1), v_rightColumn) * v_multi;
    v_horPred = v_horPred + v_rightColumnN;
    v_topRow = v_topRow + v_bottomRow;
    v_im4 = (Vec8s)(v_horPred + v_topRow) >> const_int(3);
    store_partial(const_int(8), &dst[1 * dstStride], v_im4);

    // line2
    v_horPred = broadcast(const_int(2), v_horPred4);
    v_rightColumnN = broadcast(const_int(2), v_rightColumn) * v_multi;
    v_horPred = v_horPred + v_rightColumnN;
    v_topRow = v_topRow + v_bottomRow;
    v_im4 = (Vec8s)(v_horPred + v_topRow) >> const_int(3);
    store_partial(const_int(8), &dst[2 * dstStride], v_im4);

    // line3
    v_horPred = broadcast(const_int(3), v_horPred4);
    v_rightColumnN = broadcast(const_int(3), v_rightColumn) * v_multi;
    v_horPred = v_horPred + v_rightColumnN;
    v_topRow = v_topRow + v_bottomRow;
    v_im4 = (Vec8s)(v_horPred + v_topRow) >> const_int(3);
    store_partial(const_int(8), &dst[3 * dstStride], v_im4);
}

void intra_pred_planar8(pixel* above, pixel* left, pixel* dst, intptr_t dstStride)
{
    int bottomLeft, topRight;

    // Get left and above reference column and row
    __m128i v_topRow = _mm_loadu_si128((__m128i*)above); // topRow
    __m128i v_leftColumn = _mm_loadu_si128((__m128i*)left); // leftColumn

    // Prepare intermediate variables used in interpolation
    bottomLeft = left[8];
    topRight   = above[8];

    __m128i v_bottomLeft = _mm_set1_epi16(bottomLeft);
    __m128i v_topRight = _mm_set1_epi16(topRight);

    __m128i v_bottomRow = _mm_sub_epi16(v_bottomLeft, v_topRow);
    __m128i v_rightColumn = _mm_sub_epi16(v_topRight, v_leftColumn);

    v_topRow = _mm_slli_epi16(v_topRow, (2 + 1));
    v_leftColumn = _mm_slli_epi16(v_leftColumn, (2 + 1));

    // Generate prediction signal
    __m128i v_horPred4 = _mm_add_epi16(v_leftColumn, _mm_set1_epi16(8));
    const __m128i v_multi = _mm_setr_epi16(1, 2, 3, 4, 5, 6, 7, 8);
    __m128i v_horPred, v_rightColumnN;
    __m128i v_im4;

#define COMP_PRED_PLANAR_ROW(X) \
    v_horPred = permute8s<X, X, X, X, X, X, X, X>(v_horPred4); \
    v_rightColumnN = permute8s<X, X, X, X, X, X, X, X>(v_rightColumn) * v_multi; \
    v_horPred = _mm_add_epi16(v_horPred, v_rightColumnN); \
    v_topRow = _mm_add_epi16(v_topRow, v_bottomRow); \
    v_im4 = _mm_srai_epi16(_mm_add_epi16(v_horPred, v_topRow), (3 + 1)); \
    _mm_storeu_si128((__m128i*)&dst[X * dstStride], v_im4);

    COMP_PRED_PLANAR_ROW(0);     // row 0
    COMP_PRED_PLANAR_ROW(1);
    COMP_PRED_PLANAR_ROW(2);
    COMP_PRED_PLANAR_ROW(3);
    COMP_PRED_PLANAR_ROW(4);
    COMP_PRED_PLANAR_ROW(5);
    COMP_PRED_PLANAR_ROW(6);
    COMP_PRED_PLANAR_ROW(7);     // row 7
#undef COMP_PRED_PLANAR_ROW
}

void intra_pred_planar16(pixel* above, pixel* left, pixel* dst, intptr_t dstStride)
{
    pixel bottomLeft, topRight;

    // Get left and above reference column and row
    Vec8s v_topRow_lo, v_topRow_hi;

    v_topRow_lo.load(&above[0]);
    v_topRow_hi.load(&above[8]);

    Vec8s v_leftColumn;
    v_leftColumn.load(left);   // leftColumn

    // Prepare intermediate variables used in interpolation
    bottomLeft = left[16];
    topRight   = above[16];

    Vec8s v_bottomLeft(bottomLeft);
    Vec8s v_topRight(topRight);

    Vec8s v_bottomRow_lo = v_bottomLeft - v_topRow_lo;
    Vec8s v_bottomRow_hi = v_bottomLeft - v_topRow_hi;
    Vec8s v_rightColumn = v_topRight - v_leftColumn;

    int shift = g_convertToBit[16];         // Using value corresponding to width = 8
    v_topRow_lo = v_topRow_lo << (2 + shift);
    v_topRow_hi = v_topRow_hi << (2 + shift);
    v_leftColumn = v_leftColumn << (2 + shift);

    Vec8s v_horPred4 = v_leftColumn + Vec8s(16);
    const Vec8s v_multi_lo(1, 2, 3, 4, 5, 6, 7, 8);
    const Vec8s v_multi_hi(9, 10, 11, 12, 13, 14, 15, 16);
    Vec8s v_horPred_lo, v_horPred_hi, v_rightColumnN_lo, v_rightColumnN_hi;
    Vec8s v_im4_lo, v_im4_hi;
    Vec16uc v_im5;

#define COMP_PRED_PLANAR_ROW(X) \
    v_horPred_lo = permute8s<X, X, X, X, X, X, X, X>(v_horPred4); \
    v_horPred_hi = v_horPred_lo; \
    v_rightColumnN_lo = permute8s<X, X, X, X, X, X, X, X>(v_rightColumn); \
    v_rightColumnN_hi = v_rightColumnN_lo; \
    v_rightColumnN_lo *= v_multi_lo; \
    v_rightColumnN_hi *= v_multi_hi; \
    v_horPred_lo = v_horPred_lo + v_rightColumnN_lo; \
    v_horPred_hi = v_horPred_hi + v_rightColumnN_hi; \
    v_topRow_lo = v_topRow_lo + v_bottomRow_lo; \
    v_topRow_hi = v_topRow_hi + v_bottomRow_hi; \
    v_im4_lo = (Vec8s)(v_horPred_lo + v_topRow_lo) >> (3 + shift); \
    v_im4_hi = (Vec8s)(v_horPred_hi + v_topRow_hi) >> (3 + shift); \
    v_im4_lo.store(&dst[X * dstStride]); \
    v_im4_hi.store(&dst[X * dstStride + 8])

    COMP_PRED_PLANAR_ROW(0);     // row 0
    COMP_PRED_PLANAR_ROW(1);
    COMP_PRED_PLANAR_ROW(2);
    COMP_PRED_PLANAR_ROW(3);
    COMP_PRED_PLANAR_ROW(4);
    COMP_PRED_PLANAR_ROW(5);
    COMP_PRED_PLANAR_ROW(6);
    COMP_PRED_PLANAR_ROW(7);     // row 7

    v_leftColumn.load(left + 8);   // leftColumn lower 8 rows
    v_rightColumn = v_topRight - v_leftColumn;
    v_leftColumn = v_leftColumn << (2 + shift);
    v_horPred4 = v_leftColumn + Vec8s(16);

    COMP_PRED_PLANAR_ROW(8);     // row 0
    COMP_PRED_PLANAR_ROW(9);
    COMP_PRED_PLANAR_ROW(10);
    COMP_PRED_PLANAR_ROW(11);
    COMP_PRED_PLANAR_ROW(12);
    COMP_PRED_PLANAR_ROW(13);
    COMP_PRED_PLANAR_ROW(14);
    COMP_PRED_PLANAR_ROW(15);
#undef COMP_PRED_PLANAR_ROW
}

typedef void intra_pred_planar_t (pixel* above, pixel* left, pixel* dst, intptr_t dstStride);
intra_pred_planar_t *intraPlanarN[] =
{
    intra_pred_planar4,
    intra_pred_planar8,
    intra_pred_planar16,
};

void intra_pred_planar(pixel* above, pixel* left, pixel* dst, intptr_t dstStride, int width)
{
    int nLog2Size = g_convertToBit[width] + 2;

    int k, l, bottomLeft, topRight;
    int horPred;
    // OPT_ME: when width is 64, the shift1D is 8, then the dynamic range is [-65280, 65280], so we have to use 32 bits here
    int32_t leftColumn[MAX_CU_SIZE], topRow[MAX_CU_SIZE];
    // CHECK_ME: dynamic range is 9 bits or 15 bits(I assume max input bit_depth is 14 bits)
    int16_t bottomRow[MAX_CU_SIZE], rightColumn[MAX_CU_SIZE];
    int blkSize = width;
    int offset2D = width;
    int shift1D = nLog2Size;
    int shift2D = shift1D + 1;

    if (width < 32)
    {
        intraPlanarN[nLog2Size - 2](above, left, dst, dstStride);
        return;
    }

    // Get left and above reference column and row
    for (k = 0; k < blkSize; k++)
    {
        topRow[k] = above[k];
        leftColumn[k] = left[k];
    }

    // Prepare intermediate variables used in interpolation
    bottomLeft = left[blkSize];
    topRight   = above[blkSize];
    for (k = 0; k < blkSize; k++)
    {
        bottomRow[k]   = bottomLeft - topRow[k];
        rightColumn[k] = topRight   - leftColumn[k];
        topRow[k]      <<= shift1D;
        leftColumn[k]  <<= shift1D;
    }

    // Generate prediction signal
    for (k = 0; k < blkSize; k++)
    {
        horPred = leftColumn[k] + offset2D;
        for (l = 0; l < blkSize; l++)
        {
            horPred += rightColumn[k];
            topRow[l] += bottomRow[l];
            dst[k * dstStride + l] = ((horPred + topRow[l]) >> shift2D);
        }
    }
}

void intraPredAng4x4(pixel* dst, int dstStride, int width, int dirMode, pixel *refLeft, pixel *refAbove)
{
    int blkSize        = width;

    // Map the mode index to main prediction direction and angle
    assert(dirMode > 1); //no planar and dc
    bool modeHor       = (dirMode < 18);
    bool modeVer       = !modeHor;
    int intraPredAngle = modeVer ? (int)dirMode - VER_IDX : modeHor ? -((int)dirMode - HOR_IDX) : 0;
    int lookIdx = intraPredAngle;
    int absAng         = abs(intraPredAngle);
    int signAng        = intraPredAngle < 0 ? -1 : 1;

    // Set bitshifts and scale the angle parameter to block size
    int angTable[9]    = { 0,    2,    5,   9,  13,  17,  21,  26,  32 };
    int invAngTable[9] = { 0, 4096, 1638, 910, 630, 482, 390, 315, 256 }; // (256 * 32) / Angle
    int invAngle       = invAngTable[absAng];
    absAng             = angTable[absAng];
    intraPredAngle     = signAng * absAng;

    // Do angular predictions

    pixel* refMain;
    pixel* refSide;

    // Initialize the Main and Left reference array.
    if (intraPredAngle < 0)
    {
        refMain = (modeVer ? refAbove : refLeft);     // + (blkSize - 1);
        refSide = (modeVer ? refLeft : refAbove);     // + (blkSize - 1);

        // Extend the Main reference to the left.
        int invAngleSum    = 128;     // rounding for (shift by 8)
        for (int k = -1; k > blkSize * intraPredAngle >> 5; k--)
        {
            invAngleSum += invAngle;
            refMain[k] = refSide[invAngleSum >> 8];
        }
    }
    else
    {
        refMain = modeVer ? refAbove : refLeft;
        refSide = modeVer ? refLeft  : refAbove;
    }

    // bfilter will always be true for blocksize 4
    if (intraPredAngle == 0)  // Exactly horizontal/vertical angles
    {
        if (modeHor)
        {
            Vec8s v_temp;
            Vec8s v_side_0; // refSide[0] value in a vector
            v_temp.load((void*)refSide);
            v_side_0 = broadcast(const_int(0), (Vec8s)v_temp);

            Vec8s v_side;
            v_side.load(refSide + 1);

            Vec8s v_main;
            v_main = load_partial(const_int(8), (void*)(refMain + 1));

            Vec8s tmp1, tmp2;
            tmp1 = blend8s<0, 8, 1, 9, 2, 10, 3, 11>(v_main, v_main);
            tmp2 = blend8s<0, 8, 1, 9, 2, 10, 3, 11>(tmp1, tmp1);
            tmp1 = blend8s<4, 12, 5, 13, 6, 14, 7, 15>(tmp1, tmp1);

            Vec8s row0;
            v_side -= v_side_0;
            v_side = v_side >> 1;
            row0 = tmp2 + v_side;
            row0 = min(max(0, row0), (1 << X265_DEPTH) - 1);

            store_partial(const_int(8), dst, row0);                //row0
            store_partial(const_int(8), dst + (2 * dstStride), tmp1); //row2

            tmp2 = blend8s<4, 12, 5, 13, 6, 14, 7, 15>(tmp2, tmp2);
            tmp1 = blend8s<4, 12, 5, 13, 6, 14, 7, 15>(tmp1, tmp1);

            store_partial(const_int(8), dst + (3 * dstStride), tmp1); //row3
            store_partial(const_int(8), dst + (dstStride), tmp2);    //row1
        }
        else
        {
            Vec16uc v_main;
            v_main = load_partial(const_int(8), refMain + 1);
            store_partial(const_int(8), dst, v_main);
            store_partial(const_int(8), dst + dstStride, v_main);
            store_partial(const_int(8), dst + (2 * dstStride), v_main);
            store_partial(const_int(8), dst + (3 * dstStride), v_main);

            for (int k = 0; k < 4; k++)
            {
                dst[k * dstStride] = (pixel)Clip3((int16_t)0, (int16_t)((1 << X265_DEPTH) - 1), static_cast<int16_t>((dst[k * dstStride]) + ((refSide[k + 1] - refSide[0]) >> 1)));
            }
        }
    }
    else if (intraPredAngle == -32)
    {
        Vec8s tmp;
        tmp = load_partial(const_int(8), refMain);        //-1,0,1,2
        store_partial(const_int(8), dst, tmp);
        tmp = load_partial(const_int(8), refMain - 1);     //-2,-1,0,1
        store_partial(const_int(8), dst + dstStride, tmp);
        tmp = load_partial(const_int(8), refMain - 2);
        store_partial(const_int(8), dst + 2 * dstStride, tmp);
        tmp = load_partial(const_int(8), refMain - 3);
        store_partial(const_int(8), dst + 3 * dstStride, tmp);
        return;
    }
    else if (intraPredAngle == 32)
    {
        Vec8s tmp;
        tmp = load_partial(const_int(8), refMain + 2);        //-1,0,1,2
        store_partial(const_int(8), dst, tmp);
        tmp = load_partial(const_int(8), refMain + 3);     //-2,-1,0,1
        store_partial(const_int(8), dst + dstStride, tmp);
        tmp = load_partial(const_int(8), refMain + 4);
        store_partial(const_int(8), dst + 2 * dstStride, tmp);
        tmp = load_partial(const_int(8), refMain + 5);
        store_partial(const_int(8), dst + 3 * dstStride, tmp);
        return;
    }
    else
    {
        Vec8s row11, row12, row21, row22, row31, row32, row41, row42;
        Vec8s v_deltaFract, v_deltaPos, thirty2(32), thirty1(31), v_ipAngle;

        row11 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 0));
        row12 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 0) + 1);

        row21 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 1));
        row22 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 1) + 1);

        row31 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 2));
        row32 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 2) + 1);

        row41 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 3));
        row42 = (Vec8s)load_partial(const_int(8), refMain + 1 + GETAP(lookIdx, 3) + 1);

        v_deltaPos = v_ipAngle = intraPredAngle;

        //row1
        v_deltaFract = v_deltaPos & thirty1;
        row11 = ((thirty2 - v_deltaFract) * row11 + (v_deltaFract * row12) + 16) >> 5;

        //row2
        v_deltaPos += v_ipAngle;
        v_deltaFract = v_deltaPos & thirty1;
        row21 = ((thirty2 - v_deltaFract) * row21 + (v_deltaFract * row22) + 16) >> 5;

        //row3
        v_deltaPos += v_ipAngle;
        v_deltaFract = v_deltaPos & thirty1;
        row31 = ((thirty2 - v_deltaFract) * row31 + (v_deltaFract * row32) + 16) >> 5;

        //row4
        v_deltaPos += v_ipAngle;
        v_deltaFract = v_deltaPos & thirty1;
        row41 = ((thirty2 - v_deltaFract) * row41 + (v_deltaFract * row42) + 16) >> 5;

        // Flip the block

        if (modeHor)
        {
            Vec8s tmp1, tmp2, tmp3, tmp4;

            tmp1 = blend8s<0, 8, 1, 9, 2, 10, 3, 11>(row11, row31);
            tmp2 = blend8s<0, 8, 1, 9, 2, 10, 3, 11>(row21, row41);

            tmp3 = blend8s<0, 8, 1, 9, 2, 10, 3, 11>(tmp1, tmp2);
            tmp4 = blend8s<4, 12, 5, 13, 6, 14, 7, 15>(tmp1, tmp2);

            //tmp16_1 = compress(tmp3, tmp3);
            store_partial(const_int(8), dst, tmp3);

            store_partial(const_int(8), dst + (2 * dstStride), tmp4);  //row2

            tmp3 = blend2q<1, 3>((Vec2q)tmp3, (Vec2q)tmp3);
            tmp4 = blend2q<1, 3>((Vec2q)tmp4, (Vec2q)tmp4);

            store_partial(const_int(8), dst + (3 * dstStride), tmp4);   //row3
            store_partial(const_int(8), dst + (dstStride), tmp3);       //row1
        }
        else
        {
            store_partial(const_int(8), dst, row11);
            store_partial(const_int(8), dst + (dstStride), row21);
            store_partial(const_int(8), dst + (2 * dstStride), row31);
            store_partial(const_int(8), dst + (3 * dstStride), row41);
        }
    }
}
}
#endif // if HIGH_BIT_DEPTH

namespace x265 {
void Setup_Vec_IPredPrimitives_ssse3(EncoderPrimitives& p)
{
#if HIGH_BIT_DEPTH
    p.intra_pred_dc = intra_pred_dc;
#else
    p.intra_pred_ang = intra_pred_ang;
#endif
}
}
