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
 * For more information, contact us at licensing@multicorewareinc.com
 *****************************************************************************/

#include "TLibCommon/TComRom.h"
#include "primitives.h"
#include <xmmintrin.h> // SSE
#include <pmmintrin.h> // SSE3
#include <cstring>

namespace {
#if HIGH_BIT_DEPTH
void blockcopy_pp(int bx, int by, pixel *dst, intptr_t dstride, pixel *src, intptr_t sstride)
{
    if ((bx & 7) || (((size_t)dst | (size_t)src | sstride | dstride) & 15))
    {
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            memcpy(dst, src, bx * sizeof(pixel));
            src += sstride;
            dst += dstride;
        }
    }
    else
    {
        // fast path, multiples of 8 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 8)
            {
                __m128i word = _mm_load_si128((__m128i const*)(src + x));
                _mm_store_si128((__m128i*)&dst[x], word);
            }

            src += sstride;
            dst += dstride;
        }
    }
}
#else
void blockcopy_pp(int bx, int by, pixel *dst, intptr_t dstride, pixel *src, intptr_t sstride)
{
    size_t aligncheck = (size_t)dst | (size_t)src | bx | sstride | dstride;

    if (!(aligncheck & 15))
    {
        // fast path, multiples of 16 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 16)
            {
                __m128i word0 = _mm_load_si128((__m128i const*)(src + x)); // load block of 16 byte from src
                _mm_store_si128((__m128i*)&dst[x], word0); // store block into dst
            }

            src += sstride;
            dst += dstride;
        }
    }
    else
    {
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            memcpy(dst, src, bx * sizeof(pixel));
            src += sstride;
            dst += dstride;
        }
    }
}

void blockcopy_ps(int bx, int by, pixel *dst, intptr_t dstride, short *src, intptr_t sstride)
{
    size_t aligncheck = (size_t)dst | (size_t)src | bx | sstride | dstride;
    if (!(aligncheck & 15))
    {
        // fast path, multiples of 16 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 16)
            {
                __m128i word0 = _mm_load_si128((__m128i const*)(src + x));       // load block of 16 byte from src
                __m128i word1 = _mm_load_si128((__m128i const*)(src + x + 8));

                __m128i mask = _mm_set1_epi32(0x00FF00FF);                  // mask for low bytes
                __m128i low_mask = _mm_and_si128(word0, mask);              // bytes of low
                __m128i high_mask = _mm_and_si128(word1, mask);             // bytes of high
                __m128i word01 = _mm_packus_epi16(low_mask, high_mask);     // unsigned pack
                _mm_store_si128((__m128i*)&dst[x], word01);                 // store block into dst
            }

            src += sstride;
            dst += dstride;
        }
    }
    else
    {
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x++)
            {
                dst[x] = (pixel)src[x];
            }

            src += sstride;
            dst += dstride;
        }
    }
}

void pixeladd_pp(int bx, int by, pixel *dst, intptr_t dstride, pixel *src0, pixel *src1, intptr_t sstride0, intptr_t sstride1)
{
    size_t aligncheck = (size_t)dst | (size_t)src0 | bx | sstride0 | sstride1 | dstride;
    int i = 1;
    if (!(aligncheck & 15))
    {
        __m128i maxval = _mm_set1_epi8((i << X265_DEPTH) - 1);
        __m128i zero = _mm_setzero_si128();

        // fast path, multiples of 16 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 16)
            {
                __m128i word0, word1, sum;
                word0 = _mm_load_si128((__m128i const*)(src0 + x));
                word1 = _mm_load_si128((__m128i const*)(src1 + x));
                sum = _mm_adds_epu8(word0, word1);
                sum = _mm_max_epu8(sum, zero);
                sum = _mm_min_epu8(sum, maxval);
                _mm_storeu_si128((__m128i*)&dst[x], sum);
            }
            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
    else if (!(bx & 15))
    {
        __m128i maxval = _mm_set1_epi8((i << X265_DEPTH) - 1);
        __m128i zero = _mm_setzero_si128();

        // fast path, multiples of 16 pixel wide blocks but pointers/strides require unaligned accesses
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 16)
            {
                __m128i word0, word1, sum;
                word0 = _mm_load_si128((__m128i const*)(src0 + x));
                word1 = _mm_load_si128((__m128i const*)(src1 + x));
                sum = _mm_adds_epu8(word0, word1);
                sum = _mm_max_epu8(sum, zero);
                sum = _mm_min_epu8(sum, maxval);
                _mm_storeu_si128((__m128i*)&dst[x], sum);
            }
            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
    else
    {
        int tmp;
        int max = (1 << X265_DEPTH) - 1;
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x++)
            {
                tmp = src0[x] + src1[x];
                tmp = tmp < 0 ? 0 : tmp;
                tmp = tmp > max ? max : tmp;
                dst[x] = (pixel)tmp;
            }

            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
}
#endif /* if HIGH_BIT_DEPTH */

void blockcopy_sp(int bx, int by, short *dst, intptr_t dstride, uint8_t *src, intptr_t sstride)
{
    size_t aligncheck = (size_t)dst | (size_t)src | bx | sstride | dstride;
    if (!(aligncheck & 15))
    {
        // fast path, multiples of 16 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 16)
            {
                __m128i word0 = _mm_load_si128((__m128i const*)(src + x));        // load block of 16 byte from src
                __m128i word1 = _mm_unpacklo_epi8(word0, _mm_setzero_si128());    // interleave with zero extensions
                _mm_store_si128((__m128i*)&dst[x], word1);                        // store block into dst
                __m128i word2 = _mm_unpackhi_epi8(word0, _mm_setzero_si128());    // interleave with zero extensions
                _mm_store_si128((__m128i*)&dst[x + 8], word2);                    // store block into dst
            }

            src += sstride;
            dst += dstride;
        }
    }
    else
    {
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x++)
            {
                dst[x] = (short)src[x];
            }

            src += sstride;
            dst += dstride;
        }
    }
}

void pixelsub_sp(int bx, int by, short *dst, intptr_t dstride, uint8_t *src0, uint8_t *src1, intptr_t sstride0, intptr_t sstride1)
{
    size_t aligncheck = (size_t)dst | (size_t)src0 | bx | sstride0 | sstride1 | dstride;

    if (!(aligncheck & 15))
    {
        // fast path, multiples of 16 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 16)
            {
                __m128i word0, word1;
                __m128i word3, word4;
                __m128i mask = _mm_setzero_si128();

                word0 = _mm_load_si128((__m128i const*)(src0 + x));    // load 16 bytes from src1
                word1 = _mm_load_si128((__m128i const*)(src1 + x));    // load 16 bytes from src2

                word3 = _mm_unpacklo_epi8(word0, mask);    // interleave with zero extensions
                word4 = _mm_unpacklo_epi8(word1, mask);
                _mm_store_si128((__m128i*)&dst[x], _mm_subs_epi16(word3, word4));    // store block into dst

                word3 = _mm_unpackhi_epi8(word0, mask);    // interleave with zero extensions
                word4 = _mm_unpackhi_epi8(word1, mask);
                _mm_store_si128((__m128i*)&dst[x + 8], _mm_subs_epi16(word3, word4));    // store block into dst
            }

            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
    else
    {
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x++)
            {
                dst[x] = (short)(src0[x] - src1[x]);
            }

            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
}

void pixeladd_ss(int bx, int by, short *dst, intptr_t dstride, short *src0, short *src1, intptr_t sstride0, intptr_t sstride1)
{
    size_t aligncheck = (size_t)dst | (size_t)src0 | sstride0 | sstride1 | dstride;

    if ( !(aligncheck & 15) && !(bx & 7))
    {
        __m128i maxval = _mm_set1_epi16((1 << X265_DEPTH) - 1);
        __m128i zero = _mm_setzero_si128();

        // fast path, multiples of 8 pixel wide blocks
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 8)
            {
                __m128i word0, word1, sum;

                word0 = _mm_load_si128((__m128i*)(src0 + x));    // load 16 bytes from src1
                word1 = _mm_load_si128((__m128i*)(src1 + x));    // load 16 bytes from src2

                sum = _mm_adds_epi16(word0, word1);
                sum = _mm_max_epi16(sum, zero);
                sum = _mm_min_epi16(sum, maxval);

                _mm_store_si128((__m128i*)&dst[x], sum);    // store block into dst
            }

            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
    else if (!(bx & 7))
    {
        __m128i maxval = _mm_set1_epi16((1 << X265_DEPTH) - 1);
        __m128i zero = _mm_setzero_si128();

        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x += 8)
            {
                __m128i word0, word1, sum;

                word0 = _mm_load_si128((__m128i*)(src0 + x));    // load 16 bytes from src1
                word1 = _mm_load_si128((__m128i*)(src1 + x));    // load 16 bytes from src2

                sum = _mm_adds_epi16(word0, word1);
                sum = _mm_max_epi16(sum, zero);
                sum = _mm_min_epi16(sum, maxval);

                _mm_store_si128((__m128i*)&dst[x], sum);    // store block into dst
            }

            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
    else
    {
        int tmp;
        int max = (1 << X265_DEPTH) - 1;
        // slow path, irregular memory alignments or sizes
        for (int y = 0; y < by; y++)
        {
            for (int x = 0; x < bx; x++)
            {
                tmp = src0[x] + src1[x];
                tmp = tmp < 0 ? 0 : tmp;
                tmp = tmp > max ? max : tmp;
                dst[x] = (short)tmp;
            }

            src0 += sstride0;
            src1 += sstride1;
            dst += dstride;
        }
    }
}
}

namespace x265 {
void Setup_Vec_BlockCopyPrimitives_sse3(EncoderPrimitives &p)
{
#if HIGH_BIT_DEPTH
    p.blockcpy_pp = blockcopy_pp;
    p.blockcpy_ps = (blockcpy_ps_t)blockcopy_pp;
    p.blockcpy_sp = (blockcpy_sp_t)blockcopy_pp;
#else
    p.pixeladd_pp = pixeladd_pp;
#endif

#if HIGH_BIT_DEPTH
    // At high bit depth, a pixel is a short
    p.blockcpy_sc = (blockcpy_sc_t)blockcopy_sp;
    p.pixeladd_pp = (pixeladd_pp_t)pixeladd_ss;
    p.pixeladd_ss = pixeladd_ss;
#else
    p.blockcpy_pp = blockcopy_pp;
    p.blockcpy_ps = blockcopy_ps;
    p.blockcpy_sp = blockcopy_sp;
    p.blockcpy_sc = blockcopy_sp;
    p.pixelsub_sp = pixelsub_sp;
    p.pixeladd_ss = pixeladd_ss;
#endif
}
}
