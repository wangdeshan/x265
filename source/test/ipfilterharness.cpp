/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Deepthi Devaki <deepthidevaki@multicorewareinc.com>,
 *          Rajesh Paulraj <rajesh@multicorewareinc.com>
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

#include "ipfilterharness.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

using namespace x265;

const short m_lumaFilter[4][8] =
{
    { 0, 0,   0, 64,  0,   0, 0,  0 },
    { -1, 4, -10, 58, 17,  -5, 1,  0 },
    { -1, 4, -11, 40, 40, -11, 4, -1 },
    { 0, 1,  -5, 17, 58, -10, 4, -1 }
};

const char* IPFilterPPNames[] =
{
    "FilterHorizonal_Pel_Pel<8>",
    "FilterHorizonal_Pel_Pel<4>",
    "FilterVertical_Pel_Pel<8>",
    "FilterVertical_Pel_Pel<4>"
};

IPFilterHarness::IPFilterHarness()
{
    ipf_t_size = 200 * 200;
    pixel_buff = (pixel*)malloc(ipf_t_size * sizeof(pixel));     // Assuming max_height = max_width = max_srcStride = max_dstStride = 100
    short_buff = (short*)TestHarness::alignedMalloc(sizeof(short), ipf_t_size, 32);
    IPF_vec_output_s = (short*)malloc(ipf_t_size * sizeof(short)); // Output Buffer1
    IPF_C_output_s = (short*)malloc(ipf_t_size * sizeof(short));   // Output Buffer2
    IPF_vec_output_p = (pixel*)malloc(ipf_t_size * sizeof(pixel)); // Output Buffer1
    IPF_C_output_p = (pixel*)malloc(ipf_t_size * sizeof(pixel));   // Output Buffer2

    if (!pixel_buff || !short_buff || !IPF_vec_output_s || !IPF_vec_output_p || !IPF_C_output_s || !IPF_C_output_p)
    {
        fprintf(stderr, "init_IPFilter_buffers: malloc failed, unable to initiate tests!\n");
        exit(-1);
    }

    for (int i = 0; i < ipf_t_size; i++)                         // Initialize input buffer
    {
        int isPositive = rand() & 1;                             // To randomly generate Positive and Negative values
        isPositive = (isPositive) ? 1 : -1;
        pixel_buff[i] = (pixel)(rand() &  ((1 << 8) - 1));
        short_buff[i] = (short)(isPositive) * (rand() &  SHRT_MAX);
    }
}

IPFilterHarness::~IPFilterHarness()
{
    free(IPF_vec_output_s);
    free(IPF_C_output_s);
    free(IPF_vec_output_p);
    free(IPF_C_output_p);
    TestHarness::alignedFree(short_buff);
    free(pixel_buff);
}

bool IPFilterHarness::check_IPFilter_primitive(x265::IPFilter_p_p ref, x265::IPFilter_p_p opt)
{
    int rand_height = rand() % 100;                 // Randomly generated Height
    int rand_width = rand() % 100;                  // Randomly generated Width
    short rand_val, rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_p, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_p, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_val = rand() % 4;                     // Random offset in the filter
        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        opt(8, pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_width,
            rand_height,  m_lumaFilter[rand_val]
            );
        ref(8, pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_width,
            rand_height,  m_lumaFilter[rand_val]
            );

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(x265::IPFilter_p_s ref, x265::IPFilter_p_s opt)
{
    int rand_height = rand() % 100;                 // Randomly generated Height
    int rand_width = rand() % 100;                  // Randomly generated Width
    short rand_val, rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_s, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_s, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_val = rand() % 4;                     // Random offset in the filter
        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        opt(8, pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_width,
            rand_height,  m_lumaFilter[rand_val]
            );
        ref(8, pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_width,
            rand_height,  m_lumaFilter[rand_val]
            );

        if (memcmp(IPF_vec_output_s, IPF_C_output_s, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(x265::IPFilter_s_p ref, x265::IPFilter_s_p opt)
{
    int rand_height = rand() % 100;                 // Randomly generated Height
    int rand_width = rand() % 100;                  // Randomly generated Width
    short rand_val, rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_p, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_p, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_val = rand() % 4;                     // Random offset in the filter
        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        opt(8, short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_width,
            rand_height,  m_lumaFilter[rand_val]
            );
        ref(8, short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_width,
            rand_height,  m_lumaFilter[rand_val]
            );

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(x265::IPFilterConvert_p_s ref, x265::IPFilterConvert_p_s opt)
{
    short rand_height = (short)rand() % 100;                 // Randomly generated Height
    short rand_width = (short)rand() % 100;                  // Randomly generated Width
    short rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_s, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_s, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_srcStride = rand_width + rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand_width + rand() % 100;              // Randomly generated dstStride

        opt(8, pixel_buff,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_width,
            rand_height);
        ref(8, pixel_buff,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_width,
            rand_height);

        if (memcmp(IPF_vec_output_s, IPF_C_output_s, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(x265::IPFilterConvert_s_p ref, x265::IPFilterConvert_s_p opt)
{
    short rand_height = (short)rand() % 100;                 // Randomly generated Height
    short rand_width = (short)rand() % 100;                  // Randomly generated Width
    short rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_p, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_p, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_srcStride = rand_width + rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand_width + rand() % 100;              // Randomly generated dstStride

        opt(8, short_buff,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_width,
            rand_height);
        ref(8, short_buff,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_width,
            rand_height);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_filterVMultiplane(x265::filterVmulti_t ref, x265::filterVmulti_t opt)
{
    short rand_height = 32;                 // Can be randomly generated Height
    short rand_width = 32;                  // Can be randomly generated Width
    int marginX = 64;
    int marginY = 64;
    short rand_srcStride, rand_dstStride;

    pixel dstEvec[200 * 200];
    pixel dstIvec[200 * 200];
    pixel dstPvec[200 * 200];

    pixel dstEref[200 * 200];
    pixel dstIref[200 * 200];
    pixel dstPref[200 * 200];

    memset(dstEref, 0, 40000 * sizeof(pixel));
    memset(dstIref, 0, 40000 * sizeof(pixel));
    memset(dstPref, 0, 40000 * sizeof(pixel));

    memset(dstEvec, 0, 40000 * sizeof(pixel));
    memset(dstIvec, 0, 40000 * sizeof(pixel));
    memset(dstPvec, 0, 40000 * sizeof(pixel));
    for (int i = 0; i <= 100; i++)
    {
        rand_srcStride = 200;               // Can be randomly generated
        rand_dstStride = 200;

        opt(8, short_buff + 8 * rand_srcStride,
            rand_srcStride,
            dstEvec + marginY * rand_dstStride + marginX, dstIvec + marginY * rand_dstStride + marginX, dstPvec + marginY * rand_dstStride + marginX,
            rand_dstStride,
            rand_width,
            rand_height, marginX, marginY);
        ref(8, short_buff + 8 * rand_srcStride,
            rand_srcStride,
            dstEref + marginY * rand_dstStride + marginX, dstIref + marginY * rand_dstStride + marginX, dstPref + marginY * rand_dstStride + marginX,
            rand_dstStride,
            rand_width,
            rand_height, marginX, marginY);

        if (memcmp(dstEvec, dstEref, 200 * 200 * sizeof(pixel))
            || memcmp(dstIvec, dstIref, 200 * 200 * sizeof(pixel)) || memcmp(dstPvec, dstPref, 200 * 200 * sizeof(pixel)))
        {
            return false;
        }
    }

    return true;
}

bool IPFilterHarness::check_filterHMultiplane(x265::filterHmulti_t ref, x265::filterHmulti_t opt)
{
    short rand_height = 32 + 9;                 // Can be randomly generated Height
    short rand_width = 32 + 15;                  // Can be randomly generated Width
    short rand_srcStride, rand_dstStride;

    short dstAvec[100 * 100];
    short dstEvec[100 * 100];
    short dstIvec[100 * 100];
    short dstPvec[100 * 100];
    short dstAref[100 * 100];
    short dstEref[100 * 100];
    short dstIref[100 * 100];
    short dstPref[100 * 100];
    pixel pDstAvec[100 * 100];
    pixel pDstAref[100 * 100];
    pixel pDstBvec[100 * 100];
    pixel pDstBref[100 * 100];
    pixel pDstCvec[100 * 100];
    pixel pDstCref[100 * 100];

    memset(dstAref, 0, 10000 * sizeof(short));
    memset(dstEref, 0, 10000 * sizeof(short));
    memset(dstIref, 0, 10000 * sizeof(short));
    memset(dstPref, 0, 10000 * sizeof(short));
    memset(dstAvec, 0, 10000 * sizeof(short));
    memset(dstEvec, 0, 10000 * sizeof(short));
    memset(dstIvec, 0, 10000 * sizeof(short));
    memset(dstPvec, 0, 10000 * sizeof(short));
    memset(pDstAvec, 0, 10000 * sizeof(pixel));
    memset(pDstAref, 0, 10000 * sizeof(pixel));
    memset(pDstBvec, 0, 10000 * sizeof(pixel));
    memset(pDstBref, 0, 10000 * sizeof(pixel));
    memset(pDstCvec, 0, 10000 * sizeof(pixel));
    memset(pDstCref, 0, 10000 * sizeof(pixel));

    for (int i = 0; i <= 100; i++)
    {
        rand_srcStride = 64;               // Can be randomly generated
        rand_dstStride = 64;
        opt(8, pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            dstAvec, dstEvec, dstIvec, dstPvec,
            rand_dstStride, pDstAvec, pDstBvec, pDstCvec, rand_dstStride,
            rand_width,
            rand_height);
        ref(8, pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            dstAref, dstEref, dstIref, dstPref,
            rand_dstStride, pDstAref, pDstBref, pDstCref, rand_dstStride,
            rand_width,
            rand_height);

        if (memcmp(dstAvec, dstAref, 100 * 100 * sizeof(short)) || memcmp(dstEvec, dstEref, 100 * 100 * sizeof(short))
            || memcmp(dstIvec, dstIref, 100 * 100 * sizeof(short)) || memcmp(dstPvec, dstPref, 100 * 100 * sizeof(short))
            || memcmp(pDstAvec, pDstAref, 100 * 100 * sizeof(pixel)) || memcmp(pDstBvec, pDstBref, 100 * 100 * sizeof(pixel))
            || memcmp(pDstCvec, pDstCref, 100 * 100 * sizeof(pixel))
            )
        {
            return false;
        }
    }

    return true;
}

bool IPFilterHarness::testCorrectness(const EncoderPrimitives& ref, const EncoderPrimitives& opt)
{
    for (int value = 0; value < NUM_IPFILTER_P_P; value++)
    {
        if (opt.ipFilter_p_p[value])
        {
            if (!check_IPFilter_primitive(ref.ipFilter_p_p[value], opt.ipFilter_p_p[value]))
            {
                printf("\n %s failed\n", IPFilterPPNames[value]);
                return false;
            }
        }
    }

    for (int value = 0; value < NUM_IPFILTER_P_S; value++)
    {
        if (opt.ipFilter_p_s[value])
        {
            if (!check_IPFilter_primitive(ref.ipFilter_p_s[value], opt.ipFilter_p_s[value]))
            {
                printf("\nfilterHorizontal_pel_short_%d failed\n", 8 / (value + 1));
                return false;
            }
        }
    }

    for (int value = 0; value < NUM_IPFILTER_S_P; value++)
    {
        if (opt.ipFilter_s_p[value])
        {
            if (!check_IPFilter_primitive(ref.ipFilter_s_p[value], opt.ipFilter_s_p[value]))
            {
                printf("\nfilterVertical_short_pel_%d failed\n", 8 / (value + 1));
                return false;
            }
        }
    }

    if (opt.ipfilterConvert_p_s)
    {
        if (!check_IPFilter_primitive(ref.ipfilterConvert_p_s, opt.ipfilterConvert_p_s))
        {
            printf("\nfilterConvertPeltoShort failed\n");
            return false;
        }
    }

    if (opt.ipfilterConvert_s_p)
    {
        if (!check_IPFilter_primitive(ref.ipfilterConvert_s_p, opt.ipfilterConvert_s_p))
        {
            printf("\nfilterConvertShorttoPel failed\n");
            return false;
        }
    }

    if (opt.filterVmulti)
    {
        if (!check_filterVMultiplane(ref.filterVmulti, opt.filterVmulti))
        {
            printf("\nFilter-V-multiplane failed\n");
            return false;
        }
    }

    if (opt.filterHmulti)
    {
        if (!check_filterHMultiplane(ref.filterHmulti, opt.filterHmulti))
        {
            printf("\nFilter-H-multiplane failed\n");
            return false;
        }
    }

    return true;
}

void IPFilterHarness::measureSpeed(const EncoderPrimitives& ref, const EncoderPrimitives& opt)
{
    int height = 64;
    int width = 64;
    short val = 2;
    short srcStride = 96;
    short dstStride = 96;

    for (int value = 0; value < NUM_IPFILTER_P_P; value++)
    {
        if (opt.ipFilter_p_p[value])
        {
            printf("%s", IPFilterPPNames[value]);
            REPORT_SPEEDUP(opt.ipFilter_p_p[value], ref.ipFilter_p_p[value],
                           8, pixel_buff + 3 * srcStride, srcStride, IPF_vec_output_p, dstStride, width, height, m_lumaFilter[val]);
        }
    }

    for (int value = 0; value < NUM_IPFILTER_P_S; value++)
    {
        if (opt.ipFilter_p_s[value])
        {
            printf("filterHorizontal_pel_short_%d", 8 / (value + 1));
            REPORT_SPEEDUP(opt.ipFilter_p_s[value], ref.ipFilter_p_s[value],
                           8, pixel_buff + 3 * srcStride, srcStride, IPF_vec_output_s, dstStride, width, height, m_lumaFilter[val]);
        }
    }

    for (int value = 0; value < NUM_IPFILTER_S_P; value++)
    {
        if (opt.ipFilter_s_p[value])
        {
            printf("filterVertical_short_pel_%d", 8 / (value + 1));
            REPORT_SPEEDUP(opt.ipFilter_s_p[value], ref.ipFilter_s_p[value],
                           8, short_buff + 3 * srcStride, srcStride, IPF_vec_output_p, dstStride, width, height, m_lumaFilter[val]);
        }
    }

    if (opt.ipfilterConvert_p_s)
    {
        printf("filterConvertPeltoShort\t");
        REPORT_SPEEDUP(opt.ipfilterConvert_p_s, ref.ipfilterConvert_p_s,
                       8, pixel_buff, srcStride, IPF_vec_output_s, dstStride, width, height);
    }

    if (opt.ipfilterConvert_s_p)
    {
        printf("filterConvertShorttoPel\t");
        REPORT_SPEEDUP(opt.ipfilterConvert_s_p, ref.ipfilterConvert_s_p,
                       8, short_buff, srcStride, IPF_vec_output_p, dstStride, width, height);
    }

    if (opt.filterVmulti)
    {
        printf("Filter-V-multiplane\t");
        REPORT_SPEEDUP(opt.filterVmulti, ref.filterVmulti,
                       8, short_buff + 8 * srcStride, srcStride, IPF_C_output_p + 64 * 200 + 64, IPF_vec_output_p + 64 * 200 + 64, IPF_C_output_p + 64 * 200 + 64, dstStride, width, height, 64, 64);
    }

    if (opt.filterHmulti)
    {
        printf("Filter-H-multiplane\t");
        REPORT_SPEEDUP(opt.filterHmulti, ref.filterHmulti,
                       8, pixel_buff + 8 * srcStride, srcStride, IPF_vec_output_s, IPF_C_output_s, IPF_vec_output_s, IPF_C_output_s, dstStride, IPF_vec_output_p, IPF_C_output_p, IPF_vec_output_p, dstStride, width, height);
    }
}
