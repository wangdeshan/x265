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

#ifndef X265_X265_H
#define X265_X265_H

#include <stdint.h>
#include "x265_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* x265_encoder:
 *      opaque handler for encoder */
typedef struct x265_encoder x265_encoder;

// TODO: Existing names used for the different NAL unit types can be altered to better reflect the names in the spec.
//       However, the names in the spec are not yet stable at this point. Once the names are stable, a cleanup
//       effort can be done without use of macros to alter the names used to indicate the different NAL unit types.
typedef enum
{
    NAL_UNIT_CODED_SLICE_TRAIL_N = 0, // 0
    NAL_UNIT_CODED_SLICE_TRAIL_R,   // 1

    NAL_UNIT_CODED_SLICE_TSA_N,     // 2
    NAL_UNIT_CODED_SLICE_TLA_R,     // 3

    NAL_UNIT_CODED_SLICE_STSA_N,    // 4
    NAL_UNIT_CODED_SLICE_STSA_R,    // 5

    NAL_UNIT_CODED_SLICE_RADL_N,    // 6
    NAL_UNIT_CODED_SLICE_RADL_R,    // 7

    NAL_UNIT_CODED_SLICE_RASL_N,    // 8
    NAL_UNIT_CODED_SLICE_RASL_R,    // 9

    NAL_UNIT_RESERVED_VCL_N10,
    NAL_UNIT_RESERVED_VCL_R11,
    NAL_UNIT_RESERVED_VCL_N12,
    NAL_UNIT_RESERVED_VCL_R13,
    NAL_UNIT_RESERVED_VCL_N14,
    NAL_UNIT_RESERVED_VCL_R15,

    NAL_UNIT_CODED_SLICE_BLA_W_LP,  // 16
    NAL_UNIT_CODED_SLICE_BLA_W_RADL, // 17
    NAL_UNIT_CODED_SLICE_BLA_N_LP,  // 18
    NAL_UNIT_CODED_SLICE_IDR_W_RADL, // 19
    NAL_UNIT_CODED_SLICE_IDR_N_LP,  // 20
    NAL_UNIT_CODED_SLICE_CRA,       // 21
    NAL_UNIT_RESERVED_IRAP_VCL22,
    NAL_UNIT_RESERVED_IRAP_VCL23,

    NAL_UNIT_RESERVED_VCL24,
    NAL_UNIT_RESERVED_VCL25,
    NAL_UNIT_RESERVED_VCL26,
    NAL_UNIT_RESERVED_VCL27,
    NAL_UNIT_RESERVED_VCL28,
    NAL_UNIT_RESERVED_VCL29,
    NAL_UNIT_RESERVED_VCL30,
    NAL_UNIT_RESERVED_VCL31,

    NAL_UNIT_VPS,                   // 32
    NAL_UNIT_SPS,                   // 33
    NAL_UNIT_PPS,                   // 34
    NAL_UNIT_ACCESS_UNIT_DELIMITER, // 35
    NAL_UNIT_EOS,                   // 36
    NAL_UNIT_EOB,                   // 37
    NAL_UNIT_FILLER_DATA,           // 38
    NAL_UNIT_PREFIX_SEI,            // 39
    NAL_UNIT_SUFFIX_SEI,            // 40
    NAL_UNIT_RESERVED_NVCL41,
    NAL_UNIT_RESERVED_NVCL42,
    NAL_UNIT_RESERVED_NVCL43,
    NAL_UNIT_RESERVED_NVCL44,
    NAL_UNIT_RESERVED_NVCL45,
    NAL_UNIT_RESERVED_NVCL46,
    NAL_UNIT_RESERVED_NVCL47,
    NAL_UNIT_UNSPECIFIED_48,
    NAL_UNIT_UNSPECIFIED_49,
    NAL_UNIT_UNSPECIFIED_50,
    NAL_UNIT_UNSPECIFIED_51,
    NAL_UNIT_UNSPECIFIED_52,
    NAL_UNIT_UNSPECIFIED_53,
    NAL_UNIT_UNSPECIFIED_54,
    NAL_UNIT_UNSPECIFIED_55,
    NAL_UNIT_UNSPECIFIED_56,
    NAL_UNIT_UNSPECIFIED_57,
    NAL_UNIT_UNSPECIFIED_58,
    NAL_UNIT_UNSPECIFIED_59,
    NAL_UNIT_UNSPECIFIED_60,
    NAL_UNIT_UNSPECIFIED_61,
    NAL_UNIT_UNSPECIFIED_62,
    NAL_UNIT_UNSPECIFIED_63,
    NAL_UNIT_INVALID,
} NalUnitType;

/* The data within the payload is already NAL-encapsulated; the type
 * is merely in the struct for easy access by the calling application.
 * All data returned in an x265_nal_t, including the data in p_payload, is no longer
 * valid after the next call to x265_encoder_encode.  Thus it must be used or copied
 * before calling x265_encoder_encode again. */
typedef struct x265_nal
{
    uint32_t i_type;      /* NalUnitType */
    uint32_t i_payload;   /* size in bytes */
    uint8_t* p_payload;
} x265_nal;

typedef struct x265_picture
{
    void*   planes[3];
    int     stride[3];
    int     bitDepth;
    int     sliceType;
    int     poc;
    int64_t pts;
    void*   userData;
} x265_picture;

typedef enum
{
    X265_DIA_SEARCH,
    X265_HEX_SEARCH,
    X265_UMH_SEARCH,
    X265_STAR_SEARCH,
    X265_FULL_SEARCH
} X265_ME_METHODS;

/* CPU flags */

/* x86 */
#define X265_CPU_CMOV            0x0000001
#define X265_CPU_MMX             0x0000002
#define X265_CPU_MMX2            0x0000004  /* MMX2 aka MMXEXT aka ISSE */
#define X265_CPU_MMXEXT          X265_CPU_MMX2
#define X265_CPU_SSE             0x0000008
#define X265_CPU_SSE2            0x0000010
#define X265_CPU_SSE3            0x0000020
#define X265_CPU_SSSE3           0x0000040
#define X265_CPU_SSE4            0x0000080  /* SSE4.1 */
#define X265_CPU_SSE42           0x0000100  /* SSE4.2 */
#define X265_CPU_LZCNT           0x0000200  /* Phenom support for "leading zero count" instruction. */
#define X265_CPU_AVX             0x0000400  /* AVX support: requires OS support even if YMM registers aren't used. */
#define X265_CPU_XOP             0x0000800  /* AMD XOP */
#define X265_CPU_FMA4            0x0001000  /* AMD FMA4 */
#define X265_CPU_AVX2            0x0002000  /* AVX2 */
#define X265_CPU_FMA3            0x0004000  /* Intel FMA3 */
#define X265_CPU_BMI1            0x0008000  /* BMI1 */
#define X265_CPU_BMI2            0x0010000  /* BMI2 */
/* x86 modifiers */
#define X265_CPU_CACHELINE_32    0x0020000  /* avoid memory loads that span the border between two cachelines */
#define X265_CPU_CACHELINE_64    0x0040000  /* 32/64 is the size of a cacheline in bytes */
#define X265_CPU_SSE2_IS_SLOW    0x0080000  /* avoid most SSE2 functions on Athlon64 */
#define X265_CPU_SSE2_IS_FAST    0x0100000  /* a few functions are only faster on Core2 and Phenom */
#define X265_CPU_SLOW_SHUFFLE    0x0200000  /* The Conroe has a slow shuffle unit (relative to overall SSE performance) */
#define X265_CPU_STACK_MOD4      0x0400000  /* if stack is only mod4 and not mod16 */
#define X265_CPU_SLOW_CTZ        0x0800000  /* BSR/BSF x86 instructions are really slow on some CPUs */
#define X265_CPU_SLOW_ATOM       0x1000000  /* The Atom is terrible: slow SSE unaligned loads, slow
                                             * SIMD multiplies, slow SIMD variable shifts, slow pshufb,
                                             * cacheline split penalties -- gather everything here that
                                             * isn't shared by other CPUs to avoid making half a dozen
                                             * new SLOW flags. */
#define X265_CPU_SLOW_PSHUFB     0x2000000  /* such as on the Intel Atom */
#define X265_CPU_SLOW_PALIGNR    0x4000000  /* such as on the AMD Bobcat */

static const char * const x265_motion_est_names[] = { "dia", "hex", "umh", "star", "full", 0 };

#define X265_MAX_SUBPEL_LEVEL   7

/* Log level */
#define X265_LOG_NONE          (-1)
#define X265_LOG_ERROR          0
#define X265_LOG_WARNING        1
#define X265_LOG_INFO           2
#define X265_LOG_DEBUG          3

#define X265_B_ADAPT_NONE       0
#define X265_B_ADAPT_FAST       1
#define X265_B_ADAPT_TRELLIS    2

#define X265_TYPE_AUTO          0x0000  /* Let x265 choose the right type */
#define X265_TYPE_IDR           0x0001
#define X265_TYPE_I             0x0002
#define X265_TYPE_P             0x0003
#define X265_TYPE_BREF          0x0004  /* Non-disposable B-frame */
#define X265_TYPE_B             0x0005
#define X265_TYPE_KEYFRAME      0x0006  /* IDR or I depending on b_open_gop option */
#define X265_AQ_NONE                 0
#define X265_AQ_VARIANCE             1
#define IS_X265_TYPE_I(x) ((x) == X265_TYPE_I || (x) == X265_TYPE_IDR)
#define IS_X265_TYPE_B(x) ((x) == X265_TYPE_B || (x) == X265_TYPE_BREF)

/* Colorspace type */
#define X265_CSP_MASK           0x00ff  /* */
#define X265_CSP_NONE           0x0000  /* Invalid mode     */
#define X265_CSP_I420           0x0001  /* yuv 4:2:0 planar */
#define X265_CSP_YV12           0x0002  /* yvu 4:2:0 planar */
#define X265_CSP_NV12           0x0003  /* yuv 4:2:0, with one y plane and one packed u+v */
#define X265_CSP_I422           0x0004  /* yuv 4:2:2 planar */
#define X265_CSP_YV16           0x0005  /* yvu 4:2:2 planar */
#define X265_CSP_NV16           0x0006  /* yuv 4:2:2, with one y plane and one packed u+v */
#define X265_CSP_I444           0x0007  /* yuv 4:4:4 planar */
#define X265_CSP_YV24           0x0008  /* yvu 4:4:4 planar */
#define X265_CSP_BGR            0x0009  /* packed bgr 24bits   */
#define X265_CSP_BGRA           0x000a  /* packed bgr 32bits   */
#define X265_CSP_RGB            0x000b  /* packed rgb 24bits   */
#define X265_CSP_MAX            0x000c  /* end of list */
#define X265_CSP_VFLIP          0x1000  /* the csp is vertically flipped */
#define X265_CSP_HIGH_DEPTH     0x2000  /* the csp has a depth of 16 bits per pixel component */

typedef struct
{
    const char *name;
    int planes;
    int width[3];
    int height[3];
    int mod_width;
    int mod_height;
} x265_cli_csp_t;

const x265_cli_csp_t x265_cli_csps[] =
{
    { "none", 0, { 0, 0, 0 },   { 0, 0, 0 },   0, 0 },
    { "i420", 3, { 0, 1, 1 },   { 0, 1, 1 },   2, 2 },
    { "yv12", 3, { 0, 1, 1 },   { 0, 1, 1 },   2, 2 },
    { "nv12", 2, { 0,  0 },     { 0, 1 },      2, 2 },
    { "i422", 3, { 0, 1, 1 },   { 0,  0,  0 }, 2, 1 },
    { "yv16", 3, { 0, 1, 1 },   { 0,  0,  0 }, 2, 1 },
    { "nv16", 2, { 0,  0 },     { 0,  0 },     2, 1 },
    { "i444", 3, { 0,  0,  0 }, { 0,  0,  0 }, 1, 1 },
    { "yv24", 3, { 0,  0,  0 }, { 0,  0,  0 }, 1, 1 },
};

/* rate tolerance method */
typedef enum
{
    X265_RC_ABR,
    X265_RC_CQP,
    X265_RC_CRF
} X265_RC_METHODS;

/*Level of Rate Distortion Optimization Allowed */
typedef enum
{
    X265_NO_RDO_NO_RDOQ, /* Partial RDO during mode decision (only at each depth/mode), no RDO in quantization*/
    X265_NO_RDO,         /* Partial RDO during mode decision (only at each depth/mode), quantization RDO enabled */
    X265_FULL_RDO        /* Full RD-based mode decision */
} X265_RDO_LEVEL;

/* Output statistics from encoder */
typedef struct x265_stats
{
    double    globalPsnrY;
    double    globalPsnrU;
    double    globalPsnrV;
    double    globalPsnr;
    double    globalSsim;
    double    elapsedEncodeTime;    /* wall time since encoder was opened */
    double    elapsedVideoTime;     /* encoded picture count / frame rate */
    double    bitrate;              /* accBits / elapsed video time */
    uint32_t  encodedPictureCount;  /* number of output pictures thus far */
    uint32_t  totalWPFrames;        /* number of uni-directional weighted frames used */
    uint64_t  accBits;              /* total bits output thus far */
} x265_stats;

/* Input parameters to the encoder */
typedef struct x265_param
{
    int       logLevel;
    int       bEnableWavefront;                ///< enable wavefront parallel processing
    int       poolNumThreads;                  ///< number of threads to allocate for thread pool, 0 implies auto-detection (default)
    int       frameNumThreads;                 ///< number of concurrently encoded frames, 0 implies auto-detection (default)
    const char *csvfn;                         ///< csv log filename. logLevel >= 3 is frame logging, else one line per run

    // source specification
    int       inputBitDepth;                   ///< source pixel bit depth (and internal encoder bit depth)
    int       frameRate;                       ///< source frame-rate in Hz
    int       sourceWidth;                     ///< source width in pixels
    int       sourceHeight;                    ///< source height in pixels
    int       sourceCsp;                       ///< source Color Space Parameter

    // coding unit (CU) definition
    uint32_t  maxCUSize;                       ///< max. CU width and height in pixels
    uint32_t  tuQTMaxInterDepth;               ///< amount the TU is allow to recurse beyond the inter PU depth
    uint32_t  tuQTMaxIntraDepth;               ///< amount the TU is allow to recurse beyond the intra PU depth

    // coding structure
    int       decodingRefreshType;             ///< Intra refresh type (0:none, 1:CDR, 2:IDR) default: 1
    int       keyframeMin;                     ///< Minimum intra period in frames
    int       keyframeMax;                     ///< Maximum intra period in frames
    int       bOpenGOP;                        ///< Enable Open GOP referencing
    int       bframes;                         ///< Max number of consecutive B-frames
    int       lookaheadDepth;                  ///< Number of frames to use for lookahead, determines encoder latency
    int       bFrameAdaptive;                  ///< 0 - none, 1 - fast, 2 - full (trellis) adaptive B frame scheduling
    int       bFrameBias;
    int       scenecutThreshold;               ///< how aggressively to insert extra I frames

    // Intra coding tools
    int       bEnableConstrainedIntra;         ///< enable constrained intra prediction (ignore inter predicted reference samples)
    int       bEnableStrongIntraSmoothing;     ///< enable strong intra smoothing for 32x32 blocks where the reference samples are flat

    // Inter coding tools
    int       searchMethod;                    ///< ME search method (DIA, HEX, UMH, STAR, FULL)
    int       subpelRefine;                    ///< amount of subpel work to perform (0 .. X265_MAX_SUBPEL_LEVEL)
    int       searchRange;                     ///< ME search range
    uint32_t  maxNumMergeCand;                 ///< Max number of merge candidates
    int       bEnableWeightedPred;             ///< enable weighted prediction in P slices
    int       bEnableWeightedBiPred;           ///< enable bi-directional weighted prediction in B slices

    int       bEnableAMP;                      ///< enable asymmetrical motion predictions
    int       bEnableRectInter;                ///< enable rectangular inter modes 2NxN, Nx2N
    int       bEnableCbfFastMode;              ///< enable use of Cbf flags for fast mode decision
    int       bEnableEarlySkip;                ///< enable early skip (merge) detection
    int       rdLevel;                         ///< Configure RDO work level
    int       bEnableRDO;
    int       bEnableRDOQ;
    int       bEnableSignHiding;               ///< enable hiding one sign bit per TU via implicit signaling
    int       bEnableTransformSkip;            ///< enable intra transform skipping
    int       bEnableTSkipFast;                ///< enable fast intra transform skipping
    int       bEnableRDOQTS;                   ///< enable RD optimized quantization when transform skip is selected
    int       maxNumReferences;                ///< maximum number of references a frame can have in L0

    // loop filter
    int       bEnableLoopFilter;               ///< enable Loop Filter

    // SAO loop filter
    int       bEnableSAO;                      ///< enable SAO filter
    int       saoLcuBoundary;                  ///< SAO parameter estimation using non-deblocked pixels for LCU bottom and right boundary areas
    int       saoLcuBasedOptimization;         ///< SAO LCU-based optimization

    // coding quality
    int       cbQpOffset;                      ///< Chroma Cb QP Offset (0:default)
    int       crQpOffset;                      ///< Chroma Cr QP Offset (0:default)
    int       rdPenalty;                       ///< RD-penalty for 32x32 TU for intra in non-intra slices (0: no RD-penalty, 1: RD-penalty, 2: maximum RD-penalty)

    // debugging
    int       decodedPictureHashSEI;           ///< Checksum(3)/CRC(2)/MD5(1)/disable(0) acting on decoded picture hash SEI message

    // quality metrics
    int       bEnablePsnr;
    int       bEnableSsim;
    struct
    {
        int       bitrate;
        double    rateTolerance;
        double    qCompress;
        double    ipFactor;
        double    pbFactor;
        int       qpStep;
        int       rateControlMode;             ///<Values corresponding to RcMethod
        int       qp;                          ///< Constant QP base value
        double    rfConstant;                  ///< Constant rate factor (CRF)
        int       aqMode;                      ///< Adaptive QP (AQ)
        double    aqStrength;
    } rc;
} x265_param;

/***
 * If not called, first encoder allocated will auto-detect the CPU and
 * initialize performance primitives, which are process global */
void x265_setup_primitives(x265_param *param, int cpu);

/***
 * Initialize an x265_param_t structure to default values
 */
void x265_param_default(x265_param *param);

/* x265_param_parse:
 *  set one parameter by name.
 *  returns 0 on success, or returns one of the following errors.
 *  note: BAD_VALUE occurs only if it can't even parse the value,
 *  numerical range is not checked until x265_encoder_open() or
 *  x265_encoder_reconfig().
 *  value=NULL means "true" for boolean options, but is a BAD_VALUE for non-booleans. */
#define X265_PARAM_BAD_NAME  (-1)
#define X265_PARAM_BAD_VALUE (-2)
int x265_param_parse(x265_param *p, const char *name, const char *value);

/* x265_param_apply_profile:
 *      Applies the restrictions of the given profile. (one of below) */
static const char * const x265_profile_names[] = { "main", "main10", "mainstillpicture", 0 };

/*      (can be NULL, in which case the function will do nothing)
 *      returns 0 on success, negative on failure (e.g. invalid profile name). */
int x265_param_apply_profile(x265_param *, const char *profile);

/* x265_param_default_preset:
 *      The same as x265_param_default, but also use the passed preset and tune
 *      to modify the default settings.
 *      (either can be NULL, which implies no preset or no tune, respectively)
 *
 *      Currently available presets are, ordered from fastest to slowest: */
static const char * const x265_preset_names[] = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo", 0 };

/*      The presets can also be indexed numerically, as in:
 *      x265_param_default_preset( &param, "3", ... )
 *      with ultrafast mapping to "0" and placebo mapping to "9".  This mapping may
 *      of course change if new presets are added in between, but will always be
 *      ordered from fastest to slowest.
 *
 *      Warning: the speed of these presets scales dramatically.  Ultrafast is a full
 *      100 times faster than placebo!
 *
 *      Currently available tunings are: */
static const char * const x265_tune_names[] = { "psnr", "ssim", "zero-latency", 0 };

/*      returns 0 on success, negative on failure (e.g. invalid preset/tune name). */
int x265_param_default_preset(x265_param *, const char *preset, const char *tune);

/***
 * Initialize an x265_picture structure to default values
 */
void x265_picture_init(x265_param *param, x265_picture *pic);

/* x265_max_bit_depth:
 *      Specifies the maximum number of bits per pixel that x265 can input. This
 *      is also the max bit depth that x265 encodes in.  When x265_max_bit_depth
 *      is 8, the internal and input bit depths must be 8.  When
 *      x265_max_bit_depth is 12, the internal and input bit depths can be
 *      either 8, 10, or 12. Note that the internal bit depth must be the same
 *      for all encoders allocated in the same process. */
extern const int x265_max_bit_depth;

/* x265_version_str:
 *      A static string containing the version of this compiled x265 library */
extern const char *x265_version_str;

/* x265_build_info:
 *      A static string describing the compiler and target architecture */
extern const char *x265_build_info_str;

/* Force a link error in the case of linking against an incompatible API version.
 * Glue #defines exist to force correct macro expansion; the final output of the macro
 * is x265_encoder_open_##X264_BUILD (for purposes of dlopen). */
#define x265_encoder_glue1(x, y) x ## y
#define x265_encoder_glue2(x, y) x265_encoder_glue1(x, y)
#define x265_encoder_open x265_encoder_glue2(x265_encoder_open_, X265_BUILD)

/* x265_encoder_open:
 *      create a new encoder handler, all parameters from x265_param_t are copied */
x265_encoder* x265_encoder_open(x265_param *);

/* x265_encoder_headers:
 *      return the SPS and PPS that will be used for the whole stream.
 *      *pi_nal is the number of NAL units outputted in pp_nal.
 *      returns negative on error.
 *      the payloads of all output NALs are guaranteed to be sequential in memory. */
int x265_encoder_headers(x265_encoder *, x265_nal **pp_nal, uint32_t *pi_nal);

/* x265_encoder_encode:
 *      encode one picture.
 *      *pi_nal is the number of NAL units outputted in pp_nal.
 *      returns negative on error, zero if no NAL units returned.
 *      the payloads of all output NALs are guaranteed to be sequential in memory. */
int x265_encoder_encode(x265_encoder *encoder, x265_nal **pp_nal, uint32_t *pi_nal, x265_picture *pic_in, x265_picture *pic_out);

/* x265_encoder_get_stats:
 *       returns encoder statistics */
void x265_encoder_get_stats(x265_encoder *encoder, x265_stats *);

/* x265_encoder_log:
 *       write a line to the configured CSV file.  If a CSV filename was not
 *       configured, or file open failed, or the log level indicated frame level
 *       logging, this function will perform no write. */
void x265_encoder_log(x265_encoder *encoder, int argc, char **argv);

/* x265_encoder_close:
 *      close an encoder handler */
void x265_encoder_close(x265_encoder *);

/***
 * Release library static allocations
 */
void x265_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // X265_X265_H
