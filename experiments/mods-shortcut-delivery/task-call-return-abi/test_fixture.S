/* SPDX-License-Identifier: GPL-3.0-only */
.text
.p2align 2
.macro ENTRY name
.global \name
.type \name,%function
\name:
.cfi_startproc
    b az_task_call_entry
.cfi_endproc
.size \name,.-\name
.endm
ENTRY fixture_entry_regs
ENTRY fixture_entry_integer
ENTRY fixture_entry_pair
ENTRY fixture_entry_large
ENTRY fixture_entry_hfa
ENTRY fixture_entry_vector
ENTRY fixture_entry_nested
ENTRY fixture_entry_throw

/* AAPCS64-compliant hostile helpers: C++ records then all tested volatile
 * registers and FP/control state are clobbered. C++ also overwrites errno. */
.macro HELPER name,record
.global \name
.type \name,%function
\name:
.cfi_startproc
    sub sp,sp,#16
.cfi_def_cfa_offset 16
    str x30,[sp,#8]
.cfi_offset 30,-8
    bl \record
    mov x9,#0xa0000000
    msr nzcv,x9
    mov x9,#0xc00000
    msr fpcr,x9
    mov x9,#0x1f
    msr fpsr,x9
    movi v0.16b,#0xee
    movi v1.16b,#0xee
    movi v2.16b,#0xee
    movi v3.16b,#0xee
    movi v4.16b,#0xee
    movi v5.16b,#0xee
    movi v6.16b,#0xee
    movi v7.16b,#0xee
.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18
    mov x\reg,#0xeeee
.endr
    ldr x30,[sp,#8]
.cfi_restore 30
    add sp,sp,#16
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size \name,.-\name
.endm
HELPER az_task_call_before,fixture_before
HELPER az_task_call_after,fixture_after

/* Captures into x19 base, uses x20 as scratch; original SP supplied separately. */
.macro CAPTURE
    stp x0,x1,[x19,#0]
    stp x2,x3,[x19,#16]
    stp x4,x5,[x19,#32]
    stp x6,x7,[x19,#48]
    stp x8,x9,[x19,#64]
    stp x10,x11,[x19,#80]
    stp x12,x13,[x19,#96]
    stp x14,x15,[x19,#112]
    stp x16,x17,[x19,#128]
    stp x18,x30,[x19,#144]
    mrs x20,nzcv
    str x20,[x19,#168]
    mrs x20,fpcr
    str x20,[x19,#176]
    mrs x20,fpsr
    str x20,[x19,#184]
    stp q0,q1,[x19,#192]
    stp q2,q3,[x19,#224]
    stp q4,q5,[x19,#256]
    stp q6,q7,[x19,#288]
.endm
.global fixture_register_target
.type fixture_register_target,%function
fixture_register_target:
.cfi_startproc
    stp x19,x20,[sp,#-32]!
.cfi_def_cfa_offset 32
.cfi_offset 19,-32
.cfi_offset 20,-24
    str x30,[sp,#16]
.cfi_offset 30,-16
    adrp x19,fixture_input
    add x19,x19,:lo12:fixture_input
    CAPTURE
    add x20,sp,#32
    str x20,[x19,#160]
    bl fixture_native_record
    mov x20,#0x90000000
    msr nzcv,x20
    mov x20,#0x800000
    msr fpcr,x20
    mov x20,#0x10
    msr fpsr,x20
    movi v0.16b,#0x5a
    movi v1.16b,#0x5a
    movi v2.16b,#0x5a
    movi v3.16b,#0x5a
    movi v4.16b,#0x5a
    movi v5.16b,#0x5a
    movi v6.16b,#0x5a
    movi v7.16b,#0x5a
    mov x0,#1792
    mov x1,#1793
    mov x2,#1794
    mov x3,#1795
    mov x4,#1796
    mov x5,#1797
    mov x6,#1798
    mov x7,#1799
    mov x8,#1800
    mov x9,#1801
    mov x10,#1802
    mov x11,#1803
    mov x12,#1804
    mov x13,#1805
    mov x14,#1806
    mov x15,#1807
    mov x16,#1808
    mov x17,#1809
    mov x18,#1810
    movz x0,#0x3210
    movk x0,#0x7654,lsl#16
    movk x0,#0xba98,lsl#32
    movk x0,#0xfedc,lsl#48
    movz x1,#0xcdef
    movk x1,#0x89ab,lsl#16
    movk x1,#0x4567,lsl#32
    movk x1,#0x0123,lsl#48
    ldr x30,[sp,#16]
.cfi_restore 30
    ldp x19,x20,[sp],#32
.cfi_restore 19
.cfi_restore 20
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size fixture_register_target,.-fixture_register_target

.global fixture_invoke_registers
.type fixture_invoke_registers,%function
fixture_invoke_registers:
.cfi_startproc
    stp x19,x20,[sp,#-80]!
.cfi_def_cfa_offset 80
.cfi_offset 19,-80
.cfi_offset 20,-72
    str x30,[sp,#16]
.cfi_offset 30,-64
    str x18,[sp,#24]
    mrs x19,fpcr
    str x19,[sp,#32]
    mrs x19,fpsr
    str x19,[sp,#40]
    adrp x19,fixture_expected_sp
    add x19,x19,:lo12:fixture_expected_sp
    mov x20,sp
    str x20,[x19]
    adrp x19,fixture_expected_vectors
    add x19,x19,:lo12:fixture_expected_vectors
    ldp q0,q1,[x19,#0]
    ldp q2,q3,[x19,#32]
    ldp q4,q5,[x19,#64]
    ldp q6,q7,[x19,#96]
    mov x20,#0x60000000
    msr nzcv,x20
    mov x20,#0x400000
    msr fpcr,x20
    mov x20,#1
    msr fpsr,x20
    adrp x19,fixture_expected_registers
    add x19,x19,:lo12:fixture_expected_registers
    ldp x0,x1,[x19,#0]
    ldp x2,x3,[x19,#16]
    ldp x4,x5,[x19,#32]
    ldp x6,x7,[x19,#48]
    ldp x8,x9,[x19,#64]
    ldp x10,x11,[x19,#80]
    ldp x12,x13,[x19,#96]
    ldp x14,x15,[x19,#112]
    ldp x16,x17,[x19,#128]
    ldr x18,[x19,#144]
    bl fixture_entry_regs
    adrp x19,fixture_output
    add x19,x19,:lo12:fixture_output
    CAPTURE
    mov x20,sp
    str x20,[x19,#160]
    ldr x19,[sp,#32]
    msr fpcr,x19
    ldr x19,[sp,#40]
    msr fpsr,x19
    ldr x18,[sp,#24]
    ldr x30,[sp,#16]
.cfi_restore 30
    ldp x19,x20,[sp],#80
.cfi_restore 19
.cfi_restore 20
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.global fixture_invoke_registers_end
.hidden fixture_invoke_registers_end
fixture_invoke_registers_end:
.size fixture_invoke_registers,.-fixture_invoke_registers
.section .note.GNU-stack,"",%progbits
