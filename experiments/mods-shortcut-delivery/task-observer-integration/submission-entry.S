/* SPDX-License-Identifier: GPL-3.0-only
 * Linux AArch64, known two-register input / full-x0-return call wrapper.
 * No native object dereferences and no stack-argument forwarding contract.
 * Before and after are noexcept AAPCS64 functions. Exceptions in original
 * unwind through this frame, bypassing after; no mandatory cleanup may be held.
 * Original pointer must be published non-null before entry and remain mapped.
 */
.text
.p2align 2
.global az_task_call_entry
.hidden az_task_call_before
.hidden az_task_call_after
.hidden az_task_call_original
.type az_task_call_entry,%function
az_task_call_entry:
.cfi_startproc
    sub sp,sp,#416
.cfi_def_cfa_offset 416
    stp x0,x1,[sp,#0]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x9,[sp,#64]
    stp x10,x11,[sp,#80]
    stp x12,x13,[sp,#96]
    stp x14,x15,[sp,#112]
    stp x16,x17,[sp,#128]
    stp x18,x30,[sp,#144]
.cfi_offset 30,-264
    stp q0,q1,[sp,#160]
    stp q2,q3,[sp,#192]
    stp q4,q5,[sp,#224]
    stp q6,q7,[sp,#256]
    mrs x9,nzcv
    mrs x10,fpcr
    mrs x11,fpsr
    stp x9,x10,[sp,#288]
    str x11,[sp,#304]
    /* Private TLS state at 320/328; separate zeroed scalar context at 352. */
    bl __errno_location
    str x0,[sp,#320]
    ldr w9,[x0]
    str w9,[sp,#328]
    stp xzr,xzr,[sp,#352]
    stp xzr,xzr,[sp,#368]
    stp xzr,xzr,[sp,#384]
    stp xzr,xzr,[sp,#400]
    ldp x1,x2,[sp,#0]
    ldr x3,[sp,#152]
    add x0,sp,#352
    bl az_task_call_before
    ldr x9,[sp,#320]
    ldr w10,[sp,#328]
    str w10,[x9]
    ldp x9,x10,[sp,#288]
    ldr x11,[sp,#304]
    msr nzcv,x9
    msr fpcr,x10
    msr fpsr,x11
    ldp q0,q1,[sp,#160]
    ldp q2,q3,[sp,#192]
    ldp q4,q5,[sp,#224]
    ldp q6,q7,[sp,#256]
    ldp x0,x1,[sp,#0]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp x8,x9,[sp,#64]
    ldp x10,x11,[sp,#80]
    ldp x12,x13,[sp,#96]
    ldp x14,x15,[sp,#112]
    ldp x16,x17,[sp,#128]
    ldp x18,x30,[sp,#144]
    adrp x16,:got:az_task_call_original
    ldr x16,[x16,:got_lo12:az_task_call_original]
    ldar x16,[x16]
    blr x16
.global az_task_call_native_return
.hidden az_task_call_native_return
az_task_call_native_return:
    /* Never overwrite original LR at 152 or the before-produced context. */
    stp x0,x1,[sp,#0]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x9,[sp,#64]
    stp x10,x11,[sp,#80]
    stp x12,x13,[sp,#96]
    stp x14,x15,[sp,#112]
    stp x16,x17,[sp,#128]
    str x18,[sp,#144]
    stp q0,q1,[sp,#160]
    stp q2,q3,[sp,#192]
    stp q4,q5,[sp,#224]
    stp q6,q7,[sp,#256]
    mrs x9,nzcv
    mrs x10,fpcr
    mrs x11,fpsr
    stp x9,x10,[sp,#288]
    str x11,[sp,#304]
    ldr x9,[sp,#320]
    ldr w10,[x9]
    str w10,[sp,#328]
    mov x1,x0
    add x0,sp,#352
    bl az_task_call_after
    ldr x9,[sp,#320]
    ldr w10,[sp,#328]
    str w10,[x9]
    ldp x9,x10,[sp,#288]
    ldr x11,[sp,#304]
    msr nzcv,x9
    msr fpcr,x10
    msr fpsr,x11
    ldp q0,q1,[sp,#160]
    ldp q2,q3,[sp,#192]
    ldp q4,q5,[sp,#224]
    ldp q6,q7,[sp,#256]
    ldp x0,x1,[sp,#0]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp x8,x9,[sp,#64]
    ldp x10,x11,[sp,#80]
    ldp x12,x13,[sp,#96]
    ldp x14,x15,[sp,#112]
    ldp x16,x17,[sp,#128]
    ldp x18,x30,[sp,#144]
.cfi_restore 30
    add sp,sp,#416
.cfi_def_cfa_offset 0
    ret
.global az_task_call_entry_end
.hidden az_task_call_entry_end
az_task_call_entry_end:
.cfi_endproc
.size az_task_call_entry,.-az_task_call_entry
.section .note.GNU-stack,"",%progbits
