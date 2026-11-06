/*
 * Copyright (c) 2013 ARM Ltd
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ARM LTD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ARM LTD BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* This memcpy routine is optimised for Cortex-M3/M4/M7 cores with/without
   unaligned access.

   Compile with: -mcpu=cortex-m7 -mthumb
   (or -mcpu=cortex-m4, -mcpu=cortex-m3)

   This is an OPTIONAL replacement for memcpy.c -- provides ~1.5-2x speedup
   for large aligned copies and ~2-4x for misaligned copies vs the C version.

   Prototype: void *memcpy(void *dst, const void *src, size_t count);
 */

#include "arm_asm.h"

#ifndef __OPT_BIG_BLOCK_SIZE
#define __OPT_BIG_BLOCK_SIZE (4 * 16)
#endif

#ifndef __OPT_MID_BLOCK_SIZE
#define __OPT_MID_BLOCK_SIZE (4 * 4)
#endif

#if __OPT_BIG_BLOCK_SIZE == 16
#define BEGIN_UNROLL_BIG_BLOCK \
  .irp offset, 0,4,8,12
#elif __OPT_BIG_BLOCK_SIZE == 32
#define BEGIN_UNROLL_BIG_BLOCK \
  .irp offset, 0,4,8,12,16,20,24,28
#elif __OPT_BIG_BLOCK_SIZE == 64
#define BEGIN_UNROLL_BIG_BLOCK \
  .irp offset, 0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60
#else
#error "Illegal __OPT_BIG_BLOCK_SIZE"
#endif

#if __OPT_MID_BLOCK_SIZE == 8
#define BEGIN_UNROLL_MID_BLOCK \
  .irp offset, 0,4
#elif __OPT_MID_BLOCK_SIZE == 16
#define BEGIN_UNROLL_MID_BLOCK \
  .irp offset, 0,4,8,12
#else
#error "Illegal __OPT_MID_BLOCK_SIZE"
#endif

#define END_UNROLL .endr

	.syntax unified
	.text
	.align	2
	.global	memcpy
	.thumb
	.thumb_func
	.fnstart
	.cfi_sections .debug_frame
	.cfi_startproc
	.type	memcpy, %function
memcpy:
	@ r0: dst
	@ r1: src
	@ r2: len
#ifdef __ARM_FEATURE_UNALIGNED
	prologue push_ip=HAVE_PAC_LEAF
	mov	ip, r0
#else
	prologue 0 push_ip=HAVE_PAC_LEAF
#endif /* __ARM_FEATURE_UNALIGNED */
	orr	r3, r1, r0
	ands	r3, r3, #3
	bne	.Lmisaligned_copy

.Lbig_block:
	subs	r2, __OPT_BIG_BLOCK_SIZE
	blo	.Lmid_block

	.align 2
.Lbig_block_loop:
	BEGIN_UNROLL_BIG_BLOCK
#ifdef __ARM_ARCH_7EM__
	ldr	r3, [r1], #4
	str	r3, [r0], #4
	END_UNROLL
#else /* __ARM_ARCH_7M__ */
	ldr	r3, [r1, \offset]
	str	r3, [r0, \offset]
	END_UNROLL
	adds	r0, __OPT_BIG_BLOCK_SIZE
	adds	r1, __OPT_BIG_BLOCK_SIZE
#endif
	subs	r2, __OPT_BIG_BLOCK_SIZE
	bhs .Lbig_block_loop

.Lmid_block:
	adds	r2, __OPT_BIG_BLOCK_SIZE - __OPT_MID_BLOCK_SIZE
	blo	.Lcopy_word_by_word

	.align 2
.Lmid_block_loop:
	BEGIN_UNROLL_MID_BLOCK
#ifdef __ARM_ARCH_7EM__
	ldr	r3, [r1], #4
	str	r3, [r0], #4
	END_UNROLL
#else /* __ARM_ARCH_7M__ */
	ldr	r3, [r1, \offset]
	str	r3, [r0, \offset]
	END_UNROLL
	adds    r0, __OPT_MID_BLOCK_SIZE
	adds    r1, __OPT_MID_BLOCK_SIZE
#endif
	subs	r2, __OPT_MID_BLOCK_SIZE
	bhs	.Lmid_block_loop

.Lcopy_word_by_word:
	adds	r2, __OPT_MID_BLOCK_SIZE - 4
	blo	.Lcopy_less_than_4

	.align 2
.Lcopy_word_by_word_loop:
	ldr	r3, [r1], #4
	str	r3, [r0], #4
	subs	r2, #4
	bhs	.Lcopy_word_by_word_loop

.Lcopy_less_than_4:
	adds	r2, #4
	beq	.Ldone

	lsls	r2, r2, #31
	itt ne
	ldrbne  r3, [r1], #1
	strbne  r3, [r0], #1

	bcc	.Ldone
#ifdef __ARM_FEATURE_UNALIGNED
	ldrh	r3, [r1]
	strh	r3, [r0]
#else
	ldrb	r3, [r1]
	strb	r3, [r0]
	ldrb	r3, [r1, #1]
	strb	r3, [r0, #1]
#endif /* __ARM_FEATURE_UNALIGNED */

.Ldone:
	.cfi_remember_state
#ifdef __ARM_FEATURE_UNALIGNED
	mov	r0, ip
	epilogue push_ip=HAVE_PAC_LEAF
#else
	epilogue 0 push_ip=HAVE_PAC_LEAF
#endif /*  __ARM_FEATURE_UNALIGNED */

	.align 2
.Lmisaligned_copy:
	.cfi_restore_state
#ifdef __ARM_FEATURE_UNALIGNED
#define Ldst_aligned Lbig_block

	cmp	r2, #8
	blo	.Lbyte_copy

	lsls	r3, r1, #30
	beq	.Ldst_aligned
#else
	cmp	r2, #12
	blo	.Lbyte_copy
#endif /* __ARM_FEATURE_UNALIGNED */

	ands	r3, r0, #3
	beq	.Ldst_aligned

	rsb	r3, #4
	subs	r2, r3

	lsls    r3, r3, #31
	itt ne
	ldrbne  r3, [r1], #1
	strbne  r3, [r0], #1

	bcc .Ldst_aligned

#ifdef __ARM_FEATURE_UNALIGNED
	ldrh    r3, [r1], #2
	strh    r3, [r0], #2
	b	.Ldst_aligned
#else
	ldrb    r3, [r1], #1
	strb    r3, [r0], #1
	ldrb    r3, [r1], #1
	strb    r3, [r0], #1
.Ldst_aligned:
	ands	r3, r1, #3
	beq	.Lbig_block

	push	{r4, r5}
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset 4, 0
	.cfi_rel_offset 5, 4
	subs	r2, #4

	subs	r1, r3
	rsb	ip, r3, #4

	ldr	r4, [r1], #4

	cmp	r3, #2
	beq	.Lmisaligned_copy_2_2
	cmp	r3, #3
	beq	.Lmisaligned_copy_3_1

	.macro mis_src_copy shift
1:
#ifdef __ARM_BIG_ENDIAN
	lsls	r4, r4, \shift
#else
	lsrs	r4, r4, \shift
#endif
	ldr	r3, [r1], #4
#ifdef __ARM_BIG_ENDIAN
	lsrs	r5, r3, 32-\shift
#else
	lsls	r5, r3, 32-\shift
#endif
	orr	r4, r4, r5
	str	r4, [r0], #4
	mov	r4, r3
	subs	r2, #4
	bhs	1b
	.endm

.Lmisaligned_copy_1_3:
	mis_src_copy shift=8
	b	.Lsrc_misaligned_tail

.Lmisaligned_copy_3_1:
	mis_src_copy shift=24
	b	.Lsrc_misaligned_tail

.Lmisaligned_copy_2_2:
	mis_src_copy shift=16

.Lsrc_misaligned_tail:
	adds	r2, #4
	subs	r1, ip
	pop	{r4, r5}
	.cfi_restore 4
	.cfi_restore 5
	.cfi_adjust_cfa_offset -8

#endif /* __ARM_FEATURE_UNALIGNED */

.Lbyte_copy:
	subs	r2, #4
	blo	.Lcopy_less_than_4

.Lbyte_copy_loop:
	subs    r2, #1
	ldrb    r3, [r1], #1
	strb    r3, [r0], #1
	bhs	.Lbyte_copy_loop

	ldrb	r3, [r1]
	strb	r3, [r0]
	ldrb	r3, [r1, #1]
	strb	r3, [r0, #1]
	ldrb	r3, [r1, #2]
	strb	r3, [r0, #2]

#ifdef __ARM_FEATURE_UNALIGNED
	mov	r0, ip
	epilogue push_ip=HAVE_PAC_LEAF
#else
	epilogue 0 push_ip=HAVE_PAC_LEAF
#endif /* __ARM_FEATURE_UNALIGNED */
	.cfi_endproc
	.cantunwind
	.fnend
	.size	memcpy, .-memcpy
