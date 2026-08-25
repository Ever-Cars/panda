/*
 * Copyright (c) 2009 ARM Ltd
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

/*
 * Extracted from newlib libc/machine/arm/arm_asm.h
 * Removed #include "arm-acle-compat.h" (unnecessary with GCC 4.8+).
 * Added ASM_ALIAS macro from aeabi_memset-soft.S / aeabi_memmove-soft.S.
 */

#ifndef ARM_ASM__H
#define ARM_ASM__H

#if __ARM_ARCH >= 7 && defined (__ARM_ARCH_ISA_ARM)
# define _ISA_ARM_7
#endif

#if __ARM_ARCH >= 6 && defined (__ARM_ARCH_ISA_ARM)
# define _ISA_ARM_6
#endif

#if __ARM_ARCH >= 5
# define _ISA_ARM_5
#endif

#if __ARM_ARCH >= 4 && __ARM_ARCH_ISA_THUMB >= 1
# define _ISA_ARM_4T
#endif

#if __ARM_ARCH >= 4 && __ARM_ARCH_ISA_THUMB == 0
# define _ISA_ARM_4
#endif


#if __ARM_ARCH_ISA_THUMB >= 2
# define _ISA_THUMB_2
#endif

#if __ARM_ARCH_ISA_THUMB >= 1
# define _ISA_THUMB_1
#endif

/* Check whether leaf function PAC signing has been requested in the
   -mbranch-protect compile-time option.  */
#define LEAF_PROTECT_BIT 2

#ifdef __ARM_FEATURE_PAC_DEFAULT
# define HAVE_PAC_LEAF \
	((__ARM_FEATURE_PAC_DEFAULT & (1 << LEAF_PROTECT_BIT)) && 1)
#else
# define HAVE_PAC_LEAF 0
#endif

/* Provide default parameters for PAC-code handling in leaf-functions.  */
#if HAVE_PAC_LEAF
# ifndef PAC_LEAF_PUSH_IP
#  define PAC_LEAF_PUSH_IP 1
# endif
#else /* !HAVE_PAC_LEAF */
# undef PAC_LEAF_PUSH_IP
# define PAC_LEAF_PUSH_IP 0
#endif /* HAVE_PAC_LEAF */

#define STACK_ALIGN_ENFORCE 0

#ifdef __ASSEMBLER__

/* Macro to create function aliases in assembly (thumb or ARM mode). */
.macro ASM_ALIAS new old
	.global	\new
	.type	\new, %function
#if defined (__thumb__)
	.thumb_set	\new, \old
#else
	.set	\new, \old
#endif
.endm

/* Emit .cfi_restore directives for a consecutive sequence of registers.  */
	.macro cfirestorelist first, last
	.cfi_restore \last
	.if \last-\first
	 cfirestorelist \first, \last-1
	.endif
	.endm

/* Emit .cfi_offset directives for a consecutive sequence of registers.  */
	.macro cfisavelist first, last, index=1
	.cfi_offset \last, -4*(\index)
	.if \last-\first
	 cfisavelist \first, \last-1, \index+1
	.endif
	.endm

.macro _prologue first=-1, last=-1, push_ip=PAC_LEAF_PUSH_IP, push_lr=0
	.if \push_ip & 1 != \push_ip
	 .error "push_ip may be either 0 or 1"
	.endif
	.if \push_lr & 1 != \push_lr
	 .error "push_lr may be either 0 or 1"
	.endif
	.if \first != -1
	 .if \last == -1
	  _prologue \first, \first, \push_ip, \push_lr
	  .exitm
	 .endif
	.endif
#if HAVE_PAC_LEAF
#if __ARM_FEATURE_BTI_DEFAULT
	pacbti	ip, lr, sp
#else
	pac	ip, lr, sp
#endif /* __ARM_FEATURE_BTI_DEFAULT */
	.cfi_register 143, 12
#else
#if __ARM_FEATURE_BTI_DEFAULT
	bti
#endif /* __ARM_FEATURE_BTI_DEFAULT */
#endif /* HAVE_PAC_LEAF */
	.if \first != -1
	 .if \last != \first
	  .if \last >= 13
	.error "SP cannot be in the save list"
	  .endif
	  .if \push_ip
	   .if \push_lr
	push {r\first-r\last, ip, lr}
	.cfi_adjust_cfa_offset ((\last-\first)+3)*4
	.cfi_offset 14, -4
	.cfi_offset 143, -8
	cfisavelist \first, \last, 3
	   .else
	push {r\first-r\last, ip}
	.cfi_adjust_cfa_offset ((\last-\first)+2)*4
	.cfi_offset 143, -4
	cfisavelist \first, \last, 2
	   .endif
	  .else
	   .if \push_lr
	push {r\first-r\last, lr}
	.cfi_adjust_cfa_offset ((\last-\first)+2)*4
	.cfi_offset 14, -4
	cfisavelist \first, \last, 2
	   .else
	push {r\first-r\last}
	.cfi_adjust_cfa_offset ((\last-\first)+1)*4
	cfisavelist \first, \last, 1
	   .endif
	  .endif
	 .else
	  .if \push_ip
	   .if \push_lr
	push {r\first, ip, lr}
	.cfi_adjust_cfa_offset 12
	.cfi_offset 14, -4
	.cfi_offset 143, -8
        cfisavelist \first, \first, 3
	   .else
	push {r\first, ip}
	.cfi_adjust_cfa_offset 8
	.cfi_offset 143, -4
        cfisavelist \first, \first, 2
	   .endif
	  .else
	   .if \push_lr
	push {r\first, lr}
	.cfi_adjust_cfa_offset 8
	.cfi_offset 14, -4
	cfisavelist \first, \first, 2
	   .else
	push {r\first}
	.cfi_adjust_cfa_offset 4
	cfisavelist \first, \first, 1
	   .endif
	  .endif
	 .endif
	.else
	 .if \push_ip
	  .if \push_lr
	push {ip, lr}
	.cfi_adjust_cfa_offset 8
	.cfi_offset 14, -4
	.cfi_offset 143, -8
	  .else
	push {ip}
	.cfi_adjust_cfa_offset 4
	.cfi_offset 143, -4
	  .endif
	 .else
          .if \push_lr
	push {lr}
	.cfi_adjust_cfa_offset 4
	.cfi_offset 14, -4
          .endif
	 .endif
	.endif
.endm

.macro _epilogue first=-1, last=-1, push_ip=PAC_LEAF_PUSH_IP, push_lr=0
	.if \push_ip & 1 != \push_ip
	 .error "push_ip may be either 0 or 1"
	.endif
	.if \push_lr & 1 != \push_lr
	 .error "push_lr may be either 0 or 1"
	.endif
	.if \first != -1
	 .if \last == -1
	  _epilogue \first, \first, \push_ip, \push_lr
	  .exitm
	 .endif
	 .if \last != \first
	  .if \last >= 13
	.error "SP cannot be in the save list"
	  .endif
	  .if \push_ip
	   .if \push_lr
	pop {r\first-r\last, ip, lr}
	.cfi_restore 14
	.cfi_register 143, 12
	cfirestorelist \first, \last
	   .else
	pop {r\first-r\last, ip}
	.cfi_register 143, 12
	cfirestorelist \first, \last
	   .endif
	  .else
	   .if \push_lr
	pop {r\first-r\last, lr}
	.cfi_restore 14
	cfirestorelist \first, \last
	   .else
	pop {r\first-r\last}
	cfirestorelist \first, \last
	   .endif
	  .endif
	 .else
	  .if \push_ip
	   .if \push_lr
	pop {r\first, ip, lr}
	.cfi_restore 14
	.cfi_register 143, 12
	cfirestorelist \first, \first
	   .else
	pop {r\first, ip}
	.cfi_register 143, 12
	cfirestorelist \first, \first
	   .endif
	  .else
	   .if \push_lr
	pop {r\first, lr}
	.cfi_restore 14
	cfirestorelist \first, \first
	   .else
	pop {r\first}
	cfirestorelist \first, \first
	   .endif
	  .endif
	 .endif
	.else
	 .if \push_ip
	  .if \push_lr
	pop {ip, lr}
	.cfi_restore 14
	.cfi_register 143, 12
	  .else
	pop {ip}
	.cfi_register 143, 12
	  .endif
	 .else
          .if \push_lr
	pop {lr}
	.cfi_restore 14
          .endif
	 .endif
	.endif
#if HAVE_PAC_LEAF
	aut	ip, lr, sp
#endif /* HAVE_PAC_LEAF */
	bx	lr
.endm

.macro _preprocess_reglist1 first:req, last:req, push_ip:req, push_lr:req, reglist_op:req
	.if \last == 0
	 \reglist_op \first, 0, \push_ip, \push_lr
	.elseif \last == 1
	 \reglist_op \first, 1, \push_ip, \push_lr
	.elseif \last == 2
	 \reglist_op \first, 2, \push_ip, \push_lr
	.elseif \last == 3
	 \reglist_op \first, 3, \push_ip, \push_lr
	.elseif \last == 4
	 \reglist_op \first, 4, \push_ip, \push_lr
	.elseif \last == 5
	 \reglist_op \first, 5, \push_ip, \push_lr
	.elseif \last == 6
	 \reglist_op \first, 6, \push_ip, \push_lr
	.elseif \last == 7
	 \reglist_op \first, 7, \push_ip, \push_lr
	.elseif \last == 8
	 \reglist_op \first, 8, \push_ip, \push_lr
	.elseif \last == 9
	 \reglist_op \first, 9, \push_ip, \push_lr
	.elseif \last == 10
	 \reglist_op \first, 10, \push_ip, \push_lr
	.elseif \last == 11
	 \reglist_op \first, 11, \push_ip, \push_lr
	.else
	 .error "last (\last) out of range"
	.endif
.endm

.macro _preprocess_reglist first:req, last, push_ip=0, push_lr=0, reglist_op:req
	.ifb \last
	 _preprocess_reglist \first \first \push_ip \push_lr
	.else
	 .if \first > \last
	  .error "last (\last) must be at least as great as first (\first)"
	 .endif
	 .if \first == 0
	  _preprocess_reglist1 0, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 1
	  _preprocess_reglist1 1, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 2
	  _preprocess_reglist1 2, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 3
	  _preprocess_reglist1 3, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 4
	  _preprocess_reglist1 4, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 5
	  _preprocess_reglist1 5, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 6
	  _preprocess_reglist1 6, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 7
	  _preprocess_reglist1 7, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 8
	  _preprocess_reglist1 8, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 9
	  _preprocess_reglist1 9, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 10
	  _preprocess_reglist1 10, \last, \push_ip, \push_lr, \reglist_op
	 .elseif \first == 11
	  _preprocess_reglist1 11, \last, \push_ip, \push_lr, \reglist_op
	 .else
	  .error "first (\first) out of range"
	 .endif
	.endif
.endm

.macro _align8 first, last, push_ip=0, push_lr=0, reglist_op=_prologue
	.ifb \first
	 .ifnb \last
	  .error "can't have last (\last) without specifying first"
	 .else
	  .if ((\push_ip + \push_lr) % 2) == 0
	   \reglist_op first=-1, last=-1, push_ip=\push_ip, push_lr=\push_lr
	   .exitm
	  .else
	   _align8 2, 2, \push_ip, \push_lr, \reglist_op
	   .exitm
	  .endif
	 .endif
	.endif

	.ifb \last
	 _align8 \first, \first, \push_ip, \push_lr, \reglist_op
	.else
	 .if \push_ip & 1 <> \push_ip
	  .error "push_ip may be 0 or 1"
	 .endif
	 .if \push_lr & 1 <> \push_lr
	  .error "push_lr may be 0 or 1"
	 .endif
	 .ifeq (\last - \first + \push_ip + \push_lr) % 2
	  .if \first == 0
	   .error "Alignment required and first register is r0"
	   .exitm
	  .endif
	  _preprocess_reglist \first-1, \last, \push_ip, \push_lr, \reglist_op
	 .else
	  _preprocess_reglist \first \last, \push_ip, \push_lr, \reglist_op
	 .endif
	.endif
.endm

.macro prologue first, last, push_ip=PAC_LEAF_PUSH_IP, push_lr=0, align8=STACK_ALIGN_ENFORCE
	.if \align8
	 _align8 \first, \last, \push_ip, \push_lr, _prologue
	.else
	 _prologue first=\first, last=\last, push_ip=\push_ip, push_lr=\push_lr
	.endif
.endm

.macro epilogue first, last, push_ip=PAC_LEAF_PUSH_IP, push_lr=0, align8=STACK_ALIGN_ENFORCE
	.if \align8
	 _align8 \first, \last, \push_ip, \push_lr, reglist_op=_epilogue
	.else
	 _epilogue first=\first, last=\last, push_ip=\push_ip, push_lr=\push_lr
	.endif
.endm

#endif /* __ASSEMBLER__ */

#endif /* ARM_ASM__H */
