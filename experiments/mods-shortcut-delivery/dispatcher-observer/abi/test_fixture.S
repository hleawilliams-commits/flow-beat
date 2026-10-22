/* SPDX-License-Identifier: GPL-3.0-only */
.text
.p2align 2
/* Distinct correctly declared C++ prototypes reach the untyped assembly entry. */
.macro ENTRY name
.global \name
.type \name,%function
\name:
.cfi_startproc
    b az_dispatch_entry
.cfi_endproc
.size \name,.-\name
.endm
ENTRY fixture_entry_regs
ENTRY fixture_entry_integer
ENTRY fixture_entry_pair
ENTRY fixture_entry_large
ENTRY fixture_entry_hfa
ENTRY fixture_entry_vector
ENTRY fixture_entry_double
ENTRY fixture_entry_nested
ENTRY fixture_entry_throw

/* Stronger than a normal helper: preserve only required callee-saved state and
 * deliberately destroy every input register, NZCV and FP state before return. */
.global az_dispatch_observe_entry
.type az_dispatch_observe_entry,%function
az_dispatch_observe_entry:
.cfi_startproc
    sub sp,sp,#16
.cfi_def_cfa_offset 16
    str x30,[sp,#8]
.cfi_offset 30,-8
    bl fixture_record
    mov x9,#0xa0000000
    msr nzcv,x9
    mov x9,#0x800000
    msr fpcr,x9
    mov x9,#0x10
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
.size az_dispatch_observe_entry,.-az_dispatch_observe_entry

.global fixture_register_target
.type fixture_register_target,%function
fixture_register_target:
.cfi_startproc
    stp x19,x20,[sp,#-16]!
.cfi_def_cfa_offset 16
.cfi_offset 19,-16
.cfi_offset 20,-8
    adrp x19,fixture_register_capture
    add x19,x19,:lo12:fixture_register_capture
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
    add x20,sp,#16
    str x20,[x19,#160]
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
    movz x0,#0x9876
    movk x0,#0x5432,lsl#16
    ldp x19,x20,[sp],#16
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
    stp x19,x30,[sp,#-64]!
.cfi_def_cfa_offset 64
.cfi_offset 19,-64
.cfi_offset 30,-56
    str x18,[sp,#16]
    mrs x19,fpcr
    str x19,[sp,#24]
    mrs x19,fpsr
    str x19,[sp,#32]
    adrp x19,fixture_expected_sp
    add x19,x19,:lo12:fixture_expected_sp
    mov x16,sp
    str x16,[x19]
    adrp x19,fixture_expected_vectors
    add x19,x19,:lo12:fixture_expected_vectors
    ldp q0,q1,[x19,#0]
    ldp q2,q3,[x19,#32]
    ldp q4,q5,[x19,#64]
    ldp q6,q7,[x19,#96]
    mov x16,#0x60000000
    msr nzcv,x16
    mov x16,#0x400000
    msr fpcr,x16
    mov x16,#1
    msr fpsr,x16
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
    ldr x19,[sp,#24]
    msr fpcr,x19
    ldr x19,[sp,#32]
    msr fpsr,x19
    ldr x18,[sp,#16]
    ldp x19,x30,[sp],#64
.cfi_restore 19
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.global fixture_invoke_registers_end
.hidden fixture_invoke_registers_end
fixture_invoke_registers_end:
.size fixture_invoke_registers,.-fixture_invoke_registers
.section .note.GNU-stack,"",%progbits
