/* SPDX-License-Identifier: GPL-3.0-only
 * Linux AArch64 entry-only observer. No native call or result interpretation.
 * az_dispatch_original must be non-null and published before this entry is reachable.
 * The helper is nonthrowing and obeys AAPCS64; it may not call the native target.
 */
.text
.p2align 2
.global az_dispatch_entry
.hidden az_dispatch_observe_entry
.hidden az_dispatch_original
.type az_dispatch_entry, %function
az_dispatch_entry:
.cfi_startproc
    sub sp, sp, #320
.cfi_def_cfa_offset 320
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x30, [sp, #144]
.cfi_offset 30, -168
    stp q0, q1, [sp, #160]
    stp q2, q3, [sp, #192]
    stp q4, q5, [sp, #224]
    stp q6, q7, [sp, #256]
    mrs x9, nzcv
    mrs x10, fpcr
    mrs x11, fpsr
    stp x9, x10, [sp, #288]
    str x11, [sp, #304]
    /* Fifth native input remains in w4; helper argument six receives original LR. */
    mov x5, x30
    bl az_dispatch_observe_entry
    ldp x9, x10, [sp, #288]
    ldr x11, [sp, #304]
    msr nzcv, x9
    msr fpcr, x10
    msr fpsr, x11
    ldp q0, q1, [sp, #160]
    ldp q2, q3, [sp, #192]
    ldp q4, q5, [sp, #224]
    ldp q6, q7, [sp, #256]
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x30, [sp, #144]
.cfi_restore 30
    add sp, sp, #320
.cfi_def_cfa_offset 0
    /* x16/IP0 is the sole tail-transfer scratch; AAPCS64 permits veneers to use it. */
    adrp x16, :got:az_dispatch_original
    ldr x16, [x16, :got_lo12:az_dispatch_original]
    ldar x16, [x16]
    br x16
.global az_dispatch_entry_end
.hidden az_dispatch_entry_end
az_dispatch_entry_end:
.cfi_endproc
.size az_dispatch_entry, .-az_dispatch_entry
.section .note.GNU-stack,"",%progbits
