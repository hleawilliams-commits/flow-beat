/* SPDX-License-Identifier: GPL-3.0-only */
.text
.p2align 2
.global dso_claim_call
.type dso_claim_call,%function
dso_claim_call:
.cfi_startproc
stp x29,x30,[sp,#-16]!
.cfi_def_cfa_offset 16
.cfi_offset 29,-16
.cfi_offset 30,-8
mov x29,sp
mov x8,x0
mov x0,x1
mov x1,x2
mov x2,x3
mov x3,x4
blr x8
.global dp_call_marker
.type dp_call_marker,%function
dp_call_marker:
ldp x29,x30,[sp],#16
.size dp_call_marker,4
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
ret
.cfi_endproc
.size dso_claim_call,.-dso_claim_call
.section .note.GNU-stack,"",%progbits
