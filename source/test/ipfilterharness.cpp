/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Deepthi Devaki <deepthidevaki@multicorewareinc.com>,
 *          Rajesh Paulraj <rajesh@multicorewareinc.com>
 *          Praveen Kumar Tiwari <praveen@multicorewareinc.com>
 *          Min Chen <chenm003@163.com> <min.chen@multicorewareinc.com>
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

#include "TLibCommon/TComRom.h"
#include "ipfilterharness.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

using namespace x265;

IPFilterHarness::IPFilterHarness()
{
    ipf_t_size = 200 * 200;
    pixel_buff = (pixel*)malloc(ipf_t_size * sizeof(pixel));     // Assuming max_height = max_width = max_srcStride = max_dstStride = 100
    short_buff = (int16_t*)X265_MALLOC(int16_t, ipf_t_size);
    IPF_vec_output_s = (int16_t*)malloc(ipf_t_size * sizeof(int16_t)); // Output Buffer1
    IPF_C_output_s = (int16_t*)malloc(ipf_t_size * sizeof(int16_t));   // Output Buffer2
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
        short_buff[i] = (int16_t)(isPositive) * (rand() &  SHRT_MAX);
    }
}

IPFilterHarness::~IPFilterHarness()
{
    free(IPF_vec_output_s);
    free(IPF_C_output_s);
    free(IPF_vec_output_p);
    free(IPF_C_output_p);
    X265_FREE(short_buff);
    free(pixel_buff);
}

bool IPFilterHarness::check_IPFilter_primitive(ipfilter_ps_t ref, ipfilter_ps_t opt)
{
    int rand_height = rand() % 100;                 // Randomly generated Height
    int rand_width = rand() % 100;                  // Randomly generated Width
    int16_t rand_val, rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_s, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_s, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_val = rand() % 4;                      // Random offset in the filter
        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        opt(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_width,
            rand_height, g_lumaFilter[rand_val]);
        ref(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_width,
            rand_height, g_lumaFilter[rand_val]);

        if (memcmp(IPF_vec_output_s, IPF_C_output_s, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(ipfilter_sp_t ref, ipfilter_sp_t opt)
{
    int rand_val, rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 1000; i++)
    {
        int rand_height = rand() % 100;                 // Randomly generated Height
        int rand_width = rand() % 100;                  // Randomly generated Width

        memset(IPF_vec_output_p, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_p, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_val = rand() % 4;                     // Random offset in the filter
        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        rand_width &= ~3;
        if (rand_width < 4)
            rand_width = 4;

        if (rand_height <= 0)
            rand_height = 1;

        if (rand_dstStride < rand_width)
            rand_dstStride = rand_width;

        ref(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_width,
            rand_height, rand_val);
        opt(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_width,
            rand_height, rand_val);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(ipfilter_p2s_t ref, ipfilter_p2s_t opt)
{
    int16_t rand_height = (int16_t)rand() % 100;                 // Randomly generated Height
    int16_t rand_width = (int16_t)rand() % 100;                  // Randomly generated Width
    int16_t rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_s, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_s, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_srcStride = rand_width + rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand_width + rand() % 100;              // Randomly generated dstStride

        opt(pixel_buff,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_width,
            rand_height);
        ref(pixel_buff,
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

bool IPFilterHarness::check_IPFilter_primitive(filter_p2s_t ref, filter_p2s_t opt, int isChroma)
{
    intptr_t rand_srcStride;
    const int min_size = isChroma ? 2 : 4;
    const int max_size = isChroma ? (MAX_CU_SIZE >> 1) : MAX_CU_SIZE;

    for (int i = 0; i <= 1000; i++)
    {
        int rand_height = (int16_t)rand() % 100;                 // Randomly generated Height
        int rand_width = (int16_t)rand() % 100;                  // Randomly generated Width

        memset(IPF_vec_output_s, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_s, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_srcStride = rand_width + rand() % 100;              // Randomly generated srcStride
        if (rand_srcStride < rand_width)
            rand_srcStride = rand_width;

        rand_width &= ~(min_size - 1);
        rand_width = Clip3(min_size, max_size, rand_width);

        rand_height &= ~(min_size - 1);
        rand_height = Clip3(min_size, max_size, rand_height);

        ref(pixel_buff,
            rand_srcStride,
            IPF_C_output_s,
            rand_width,
            rand_height);
        opt(pixel_buff,
            rand_srcStride,
            IPF_vec_output_s,
            rand_width,
            rand_height);

        if (memcmp(IPF_vec_output_s, IPF_C_output_s, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilter_primitive(ipfilter_s2p_t ref, ipfilter_s2p_t opt)
{
    int16_t rand_height = (int16_t)rand() % 100;                 // Randomly generated Height
    int16_t rand_width = (int16_t)rand() % 100;                  // Randomly generated Width
    int16_t rand_srcStride, rand_dstStride;

    for (int i = 0; i <= 100; i++)
    {
        memset(IPF_vec_output_p, 0, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_p, 0, ipf_t_size);        // Initialize output buffer to zero

        rand_srcStride = rand_width + rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand_width + rand() % 100;              // Randomly generated dstStride

        opt(short_buff,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_width,
            rand_height);
        ref(short_buff,
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

bool IPFilterHarness::check_IPFilter_primitive(ipfilter_ss_t ref, ipfilter_ss_t opt, int isChroma)
{
    int rand_val, rand_srcStride, rand_dstStride;
    const int min_size = isChroma ? 2 : 4;

    // NOTE: refill data to avoid overflow
    const int max_filter_val = 64 * (1 << 8);
    for (int i = 0; i < ipf_t_size; i++)
    {
        short_buff[i] = rand() % (2 * max_filter_val) - max_filter_val;
    }

    for (int i = 0; i <= 1000; i++)
    {
        int rand_height = rand() % 100;                 // Randomly generated Height
        int rand_width = rand() % 100;                  // Randomly generated Width

        memset(IPF_vec_output_s, 0xCD, ipf_t_size);      // Initialize output buffer to zero
        memset(IPF_C_output_s, 0xCD, ipf_t_size);        // Initialize output buffer to zero

        rand_val = rand() % 4;                      // Random offset in the filter
        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        rand_width &= ~(min_size - 1);
        if (rand_width < min_size)
            rand_width = min_size;

        rand_height &= ~(min_size - 1);
        if (rand_height < min_size)
            rand_height = min_size;

        if (rand_srcStride < rand_width)
            rand_srcStride = rand_width;

        if (rand_dstStride < rand_width)
            rand_dstStride = rand_width;

        ref(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_width,
            rand_height, rand_val
            );
        opt(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_width,
            rand_height, rand_val
            );

        if (memcmp(IPF_C_output_s, IPF_vec_output_s, ipf_t_size * sizeof(int16_t)))
        {
            return false;
        }
    }

    return true;
}

bool IPFilterHarness::check_IPFilterChroma_primitive(filter_pp_t ref, filter_pp_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 8;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        opt(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_coeffIdx);
        ref(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterChroma_ps_primitive(filter_ps_t ref, filter_ps_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 8;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100;              // Randomly generated dstStride

        ref(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_coeffIdx);

        opt(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_vec_output_s, IPF_C_output_s, ipf_t_size * sizeof(int16_t)))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterChroma_sp_primitive(filter_sp_t ref, filter_sp_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 8;                 // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100 + 32;         // Randomly generated dstStride

        ref(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_coeffIdx);

        opt(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterChroma_ss_primitive(filter_ss_t ref, filter_ss_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 8;                 // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;              // Randomly generated srcStride
        rand_dstStride = rand() % 100 + 32;         // Randomly generated dstStride

        ref(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_coeffIdx);

        opt(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_C_output_s, IPF_vec_output_s, ipf_t_size * sizeof(int16_t)))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterLuma_primitive(filter_pp_t ref, filter_pp_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 3;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;             // Randomly generated srcStride
        rand_dstStride = rand() % 100 + 64;        // Randomly generated dstStride

        opt(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_coeffIdx);
        ref(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterLuma_ps_primitive(filter_ps_t ref, filter_ps_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 1000; i++)
    {
        rand_coeffIdx = rand() % 3;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;             // Randomly generated srcStride
        rand_dstStride = rand() % 100 + 64;        // Randomly generated dstStride

        ref(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_coeffIdx);
        opt(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_vec_output_s, IPF_C_output_s, ipf_t_size * sizeof(int16_t)))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterLuma_sp_primitive(filter_sp_t ref, filter_sp_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 3;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;             // Randomly generated srcStride
        rand_dstStride = rand() % 100 + 64;        // Randomly generated dstStride

        ref(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_coeffIdx);

        opt(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterLuma_ss_primitive(filter_ss_t ref, filter_ss_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdx;

    for (int i = 0; i <= 100; i++)
    {
        rand_coeffIdx = rand() % 3;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;             // Randomly generated srcStride
        rand_dstStride = rand() % 100 + 64;        // Randomly generated dstStride

        ref(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_s,
            rand_dstStride,
            rand_coeffIdx);

        opt(short_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_s,
            rand_dstStride,
            rand_coeffIdx);

        if (memcmp(IPF_C_output_s, IPF_vec_output_s, ipf_t_size * sizeof(int16_t)))
            return false;
    }

    return true;
}

bool IPFilterHarness::check_IPFilterLumaHV_primitive(filter_hv_pp_t ref, filter_hv_pp_t opt)
{
    int rand_srcStride, rand_dstStride, rand_coeffIdxX, rand_coeffIdxY;

    for (int i = 0; i <= 1000; i++)
    {
        rand_coeffIdxX = rand() % 3;                // Random coeffIdex in the filter
        rand_coeffIdxY = rand() % 3;                // Random coeffIdex in the filter

        rand_srcStride = rand() % 100;             // Randomly generated srcStride
        rand_dstStride = rand() % 100;             // Randomly generated dstStride

        ref(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_C_output_p,
            rand_dstStride,
            rand_coeffIdxX,
            rand_coeffIdxY);
        opt(pixel_buff + 3 * rand_srcStride,
            rand_srcStride,
            IPF_vec_output_p,
            rand_dstStride,
            rand_coeffIdxX,
            rand_coeffIdxY);

        if (memcmp(IPF_vec_output_p, IPF_C_output_p, ipf_t_size))
            return false;
    }

    return true;
}

bool IPFilterHarness::testCorrectness(const EncoderPrimitives& ref, const EncoderPrimitives& opt)
{
    for (int value = 0; value < NUM_IPFILTER_P_S; value++)
    {
        if (opt.ipfilter_ps[value])
        {
            if (!check_IPFilter_primitive(ref.ipfilter_ps[value], opt.ipfilter_ps[value]))
            {
                printf("ipfilter_ps %d failed\n", 8 / (value + 1));
                return false;
            }
        }
    }

    for (int value = 0; value < NUM_IPFILTER_S_P; value++)
    {
        if (opt.ipfilter_sp[value])
        {
            if (!check_IPFilter_primitive(ref.ipfilter_sp[value], opt.ipfilter_sp[value]))
            {
                printf("ipfilter_sp %d failed\n", 8 / (value + 1));
                return false;
            }
        }
    }

    for (int value = 0; value < NUM_IPFILTER_S_S; value++)
    {
        if (opt.ipfilter_ss[value])
        {
            if (!check_IPFilter_primitive(ref.ipfilter_ss[value], opt.ipfilter_ss[value], (value == FILTER_V_S_S_4)))
            {
                printf("ipfilter_ss %d failed\n", 8 / (value + 1));
                return false;
            }
        }
    }

    if (opt.ipfilter_p2s)
    {
        if (!check_IPFilter_primitive(ref.ipfilter_p2s, opt.ipfilter_p2s))
        {
            printf("ipfilter_p2s failed\n");
            return false;
        }
    }

    if (opt.luma_p2s)
    {
        if (!check_IPFilter_primitive(ref.luma_p2s, opt.luma_p2s, 0))
        {
            printf("ipfilter_p2s failed\n");
            return false;
        }
    }

    if (opt.chroma_p2s)
    {
        if (!check_IPFilter_primitive(ref.chroma_p2s, opt.chroma_p2s, 1))
        {
            printf("ipfilter_p2s failed\n");
            return false;
        }
    }

    if (opt.ipfilter_s2p)
    {
        if (!check_IPFilter_primitive(ref.ipfilter_s2p, opt.ipfilter_s2p))
        {
            printf("\nfilterConvertShorttoPel failed\n");
            return false;
        }
    }

    for (int value = 0; value < NUM_LUMA_PARTITIONS; value++)
    {
        if (opt.luma_hpp[value])
        {
            if (!check_IPFilterLuma_primitive(ref.luma_hpp[value], opt.luma_hpp[value]))
            {
                printf("luma_hpp[%s]", lumaPartStr[value]);
                return false;
            }
        }
        if (opt.luma_hps[value])
        {
            if (!check_IPFilterLuma_ps_primitive(ref.luma_hps[value], opt.luma_hps[value]))
            {
                printf("luma_hps[%s]", lumaPartStr[value]);
                return false;
            }
        }
        if (opt.luma_vpp[value])
        {
            if (!check_IPFilterLuma_primitive(ref.luma_vpp[value], opt.luma_vpp[value]))
            {
                printf("luma_vpp[%s]", lumaPartStr[value]);
                return false;
            }
        }
        if (opt.luma_vps[value])
        {
            if (!check_IPFilterLuma_ps_primitive(ref.luma_vps[value], opt.luma_vps[value]))
            {
                printf("luma_vps[%s]", lumaPartStr[value]);
                return false;
            }
        }
        if (opt.luma_vsp[value])
        {
            if (!check_IPFilterLuma_sp_primitive(ref.luma_vsp[value], opt.luma_vsp[value]))
            {
                printf("luma_vsp[%s]", lumaPartStr[value]);
                return false;
            }
        }
        if (opt.luma_vss[value])
        {
            if (!check_IPFilterLuma_ss_primitive(ref.luma_vss[value], opt.luma_vss[value]))
            {
                printf("luma_vss[%s]", lumaPartStr[value]);
                return false;
            }
        }
    }

    for (int value = 0; value < NUM_CHROMA_PARTITIONS; value++)
    {
        if (opt.chroma_hpp[value])
        {
            if (!check_IPFilterChroma_primitive(ref.chroma_hpp[value], opt.chroma_hpp[value]))
            {
                printf("chroma_hpp[%s]", chromaPartStr[value]);
                return false;
            }
        }
        if (opt.chroma_hps[value])
        {
            if (!check_IPFilterChroma_ps_primitive(ref.chroma_hps[value], opt.chroma_hps[value]))
            {
                printf("chroma_hps[%s]", chromaPartStr[value]);
                return false;
            }
        }
        if (opt.chroma_vpp[value])
        {
            if (!check_IPFilterChroma_primitive(ref.chroma_vpp[value], opt.chroma_vpp[value]))
            {
                printf("chroma_vpp[%s]", chromaPartStr[value]);
                return false;
            }
        }
        if (opt.chroma_vps[value])
        {
            if (!check_IPFilterChroma_ps_primitive(ref.chroma_vps[value], opt.chroma_vps[value]))
            {
                printf("chroma_vps[%s]", chromaPartStr[value]);
                return false;
            }
        }
        if (opt.chroma_vsp[value])
        {
            if (!check_IPFilterChroma_sp_primitive(ref.chroma_vsp[value], opt.chroma_vsp[value]))
            {
                printf("chroma_vsp[%s]", chromaPartStr[value]);
                return false;
            }
        }
        if (opt.chroma_vss[value])
        {
            if (!check_IPFilterChroma_ss_primitive(ref.chroma_vss[value], opt.chroma_vss[value]))
            {
                printf("chroma_vss[%s]", chromaPartStr[value]);
                return false;
            }
        }
    }

    for (int value = 0; value < NUM_LUMA_PARTITIONS; value++)
    {
        if (opt.luma_hvpp[value])
        {
            if (!check_IPFilterLumaHV_primitive(ref.luma_hvpp[value], opt.luma_hvpp[value]))
            {
                printf("luma_hvpp[%s]", lumaPartStr[value]);
                return false;
            }
        }
    }

    return true;
}

void IPFilterHarness::measureSpeed(const EncoderPrimitives& ref, const EncoderPrimitives& opt)
{
    int height = 64;
    int width = 64;
    int16_t val = 2;
    int16_t srcStride = 96;
    int16_t dstStride = 96;
    int maxVerticalfilterHalfDistance = 3;

    for (int value = 0; value < NUM_IPFILTER_P_S; value++)
    {
        if (opt.ipfilter_ps[value])
        {
            printf("ipfilter_ps %d\t", 8 / (value + 1));
            REPORT_SPEEDUP(opt.ipfilter_ps[value], ref.ipfilter_ps[value],
                           pixel_buff + maxVerticalfilterHalfDistance * srcStride, srcStride, IPF_vec_output_s, dstStride, width, height, g_lumaFilter[val]);
        }
    }

    for (int value = 0; value < NUM_IPFILTER_S_P; value++)
    {
        if (opt.ipfilter_sp[value])
        {
            printf("ipfilter_sp %d\t", 8 / (value + 1));
            REPORT_SPEEDUP(opt.ipfilter_sp[value], ref.ipfilter_sp[value],
                           short_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_p, dstStride, width, height, val);
        }
    }

    for (int value = 0; value < NUM_IPFILTER_S_S; value++)
    {
        if (opt.ipfilter_ss[value])
        {
            printf("ipfilter_ss %d\t", 8 / (value + 1));
            REPORT_SPEEDUP(opt.ipfilter_ss[value], ref.ipfilter_ss[value],
                           short_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_s, dstStride, width, height, val);
        }
    }

    if (opt.ipfilter_p2s)
    {
        printf("ipfilter_p2s\t");
        REPORT_SPEEDUP(opt.ipfilter_p2s, ref.ipfilter_p2s,
                       pixel_buff, srcStride, IPF_vec_output_s, dstStride, width, height);
    }

    if (opt.luma_p2s)
    {
        printf("luma_p2s\t");
        REPORT_SPEEDUP(opt.luma_p2s, ref.luma_p2s,
                       pixel_buff, srcStride, IPF_vec_output_s, width, height);
    }

    if (opt.chroma_p2s)
    {
        printf("chroma_p2s\t");
        REPORT_SPEEDUP(opt.chroma_p2s, ref.chroma_p2s,
                       pixel_buff, srcStride, IPF_vec_output_s, width, height);
    }

    if (opt.ipfilter_s2p)
    {
        printf("ipfilter_s2p\t");
        REPORT_SPEEDUP(opt.ipfilter_s2p, ref.ipfilter_s2p,
                       short_buff, srcStride, IPF_vec_output_p, dstStride, width, height);
    }

    for (int value = 0; value < NUM_LUMA_PARTITIONS; value++)
    {
        if (opt.luma_hpp[value])
        {
            printf("luma_hpp[%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_hpp[value], ref.luma_hpp[value],
                           pixel_buff + srcStride, srcStride, IPF_vec_output_p, dstStride, 1);
        }

        if (opt.luma_hps[value])
        {
            printf("luma_hps[%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_hps[value], ref.luma_hps[value],
                           pixel_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_s, dstStride, 1);
        }

        if (opt.luma_vpp[value])
        {
            printf("luma_vpp[%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_vpp[value], ref.luma_vpp[value],
                           pixel_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_p, dstStride, 1);
        }

        if (opt.luma_vps[value])
        {
            printf("luma_vps[%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_vps[value], ref.luma_vps[value],
                           pixel_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_s, dstStride, 1);
        }

        if (opt.luma_vsp[value])
        {
            printf("luma_vsp[%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_vsp[value], ref.luma_vsp[value],
                           short_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_p, dstStride, 1);
        }

        if (opt.luma_vss[value])
        {
            printf("luma_vss[%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_vss[value], ref.luma_vss[value],
                           short_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_s, dstStride, 1);
        }

        if (opt.luma_hvpp[value])
        {
            printf("luma_hv [%s]\t", lumaPartStr[value]);
            REPORT_SPEEDUP(opt.luma_hvpp[value], ref.luma_hvpp[value],
                           pixel_buff + srcStride, srcStride, IPF_vec_output_p, dstStride, 1, 3);
        }
    }

    for (int value = 0; value < NUM_CHROMA_PARTITIONS; value++)
    {
        if (opt.chroma_hpp[value])
        {
            printf("chroma_hpp[%s]", chromaPartStr[value]);
            REPORT_SPEEDUP(opt.chroma_hpp[value], ref.chroma_hpp[value],
                           pixel_buff + srcStride, srcStride, IPF_vec_output_p, dstStride, 1);
        }
        if (opt.chroma_hps[value])
        {
            printf("chroma_hps[%s]", chromaPartStr[value]);
            REPORT_SPEEDUP(opt.chroma_hps[value], ref.chroma_hps[value],
                           pixel_buff + srcStride, srcStride, IPF_vec_output_s, dstStride, 1);
        }
        if (opt.chroma_vpp[value])
        {
            printf("chroma_vpp[%s]", chromaPartStr[value]);
            REPORT_SPEEDUP(opt.chroma_vpp[value], ref.chroma_vpp[value],
                           pixel_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_p, dstStride, 1);
        }
        if (opt.chroma_vps[value])
        {
            printf("chroma_vps[%s]", chromaPartStr[value]);
            REPORT_SPEEDUP(opt.chroma_vps[value], ref.chroma_vps[value],
                           pixel_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_s, dstStride, 1);
        }
        if (opt.chroma_vsp[value])
        {
            printf("chroma_vsp[%s]", chromaPartStr[value]);
            REPORT_SPEEDUP(opt.chroma_vsp[value], ref.chroma_vsp[value],
                           short_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_p, dstStride, 1);
        }
        if (opt.chroma_vss[value])
        {
            printf("chroma_vss[%s]", chromaPartStr[value]);
            REPORT_SPEEDUP(opt.chroma_vss[value], ref.chroma_vss[value],
                           short_buff + maxVerticalfilterHalfDistance * srcStride, srcStride,
                           IPF_vec_output_s, dstStride, 1);
        }
    }
}
