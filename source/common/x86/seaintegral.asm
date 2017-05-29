;*****************************************************************************
;* Copyright (C) 2013-2017 MulticoreWare, Inc
;*
;* Authors: Jayashri Murugan <jayashri@multicorewareinc.com>
;*          Vignesh V Menon <vignesh@multicorewareinc.com>
;*          Praveen Tiwari <praveen@multicorewareinc.com>
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* This program is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License
;* along with this program; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
;*
;* This program is also available under a commercial proprietary license.
;* For more information, contact us at license @ x265.com.
;*****************************************************************************/

%include "x86inc.asm"
%include "x86util.asm"

SECTION .text 

;-----------------------------------------------------------------------------
;void integral_init4v_c(uint32_t *sum4, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral4v, 2, 3, 2
    mov r2, r1
    shl r2, 4

.loop
    movu    m0, [r0]
    movu    m1, [r0 + r2]
    psubd   m1, m0
    movu    [r0], m1
    add     r0, 32
    sub     r1, 8
    cmp     r1, 0
    jnz     .loop
    RET

;-----------------------------------------------------------------------------
;void integral_init8v_c(uint32_t *sum8, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral8v, 2, 3, 2
    mov r2, r1
    shl r2, 5

.loop
    movu    m0, [r0]
    movu    m1, [r0 + r2]
    psubd   m1, m0
    movu    [r0], m1
    add     r0, 32
    sub     r1, 8
    cmp     r1, 0
    jnz     .loop
    RET

;-----------------------------------------------------------------------------
;void integral_init12v_c(uint32_t *sum12, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral12v, 2, 4, 2
    mov r2, r1
    mov r3, r1
    shl r2, 5
    shl r3, 4
    add r2, r3

.loop
    movu    m0, [r0]
    movu    m1, [r0 + r2]
    psubd   m1, m0
    movu    [r0], m1
    add     r0, 32
    sub     r1, 8
    cmp     r1, 0
    jnz     .loop
    RET

;-----------------------------------------------------------------------------
;void integral_init16v_c(uint32_t *sum16, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral16v, 2, 3, 2
    mov r2, r1
    shl r2, 6

.loop
    movu    m0, [r0]
    movu    m1, [r0 + r2]
    psubd   m1, m0
    movu    [r0], m1
    add     r0, 32
    sub     r1, 8
    cmp     r1, 0
    jnz     .loop
    RET

;-----------------------------------------------------------------------------
;void integral_init24v_c(uint32_t *sum24, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral24v, 2, 4, 2
    mov r2, r1
    mov r3, r1
    shl r2, 6
    shl r3, 5
    add r2, r3

.loop
    movu    m0, [r0]
    movu    m1, [r0 + r2]
    psubd   m1, m0
    movu    [r0], m1
    add     r0, 32
    sub     r1, 8
    cmp     r1, 0
    jnz     .loop
    RET

;-----------------------------------------------------------------------------
;void integral_init32v_c(uint32_t *sum32, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral32v, 2, 3, 2
    mov r2, r1
    shl r2, 7

.loop
    movu    m0, [r0]
    movu    m1, [r0 + r2]
    psubd   m1, m0
    movu    [r0], m1
    add     r0, 32
    sub     r1, 8
    cmp     r1, 0
    jnz     .loop
    RET

;-----------------------------------------------------------------------------
;static void integral_init4h_c(uint32_t *sum, pixel *pix, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2

%macro INTEGRAL_FOUR_HORIZONTAL_16 0
    pmovzxbw       m0, [r1]
    pmovzxbw       m1, [r1 + 1]
    paddw          m0, m1
    pmovzxbw       m1, [r1 + 2]
    paddw          m0, m1
    pmovzxbw       m1, [r1 + 3]
    paddw          m0, m1
%endmacro

cglobal integral4h, 3, 5, 3
    lea            r3, [4 * r2]
    sub            r0, r3
    sub            r2, 4                      ;stride - 4
    mov            r4, r2
    shr            r4, 4

.loop_16:
    INTEGRAL_FOUR_HORIZONTAL_16
    vperm2i128     m2, m0, m0, 1
    pmovzxwd       m2, xm2
    pmovzxwd       m0, xm0
    movu           m1, [r0]
    paddd          m0, m1
    movu           [r0 + r3], m0
    movu           m1, [r0 + 32]
    paddd          m2, m1
    movu           [r0 + r3 + 32], m2
    add            r1, 16
    add            r0, 64
    sub            r2, 16
    sub            r4, 1
    jnz            .loop_16
    cmp            r2, 12
    je             .loop_12
    cmp            r2, 4
    je             .loop_4

.loop_12:
    INTEGRAL_FOUR_HORIZONTAL_16
    vperm2i128     m2, m0, m0, 1
    pmovzxwd       xm2, xm2
    pmovzxwd       m0, xm0
    movu           m1, [r0]
    paddd          m0, m1
    movu           [r0 + r3], m0
    movu           xm1, [r0 + 32]
    paddd          xm2, xm1
    movu           [r0 + r3 + 32], xm2
    jmp             .end

.loop_4:
    INTEGRAL_FOUR_HORIZONTAL_16
    pmovzxwd       xm0, xm0
    movu           xm1, [r0]
    paddd          xm0, xm1
    movu           [r0 + r3], xm0
    jmp            .end

.end
    RET

;-----------------------------------------------------------------------------
;static void integral_init8h_c(uint32_t *sum, pixel *pix, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral8h, 3, 3, 0
 
    RET

;-----------------------------------------------------------------------------
;static void integral_init12h_c(uint32_t *sum, pixel *pix, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral12h, 3, 3, 0
 
    RET

;-----------------------------------------------------------------------------
;static void integral_init16h_c(uint32_t *sum, pixel *pix, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral16h, 3, 3, 0
 
    RET

;-----------------------------------------------------------------------------
;static void integral_init24h_c(uint32_t *sum, pixel *pix, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral24h, 3, 3, 0
 
    RET

;-----------------------------------------------------------------------------
;static void integral_init32h_c(uint32_t *sum, pixel *pix, intptr_t stride)
;-----------------------------------------------------------------------------
INIT_YMM avx2
cglobal integral32h, 3, 3, 0
 
    RET
