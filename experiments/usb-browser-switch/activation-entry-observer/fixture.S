/* SPDX-License-Identifier: GPL-3.0-only
 * Owned synthetic AArch64 fixture only. No firmware addresses or instructions.
 * The external helper interceptor preserves the actual helper's returned errno.
 */
.text
.p2align 2

.macro SAVE_ALL
    stp x0,x1,[sp,#0]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x9,[sp,#64]
    stp x10,x11,[sp,#80]
    stp x12,x13,[sp,#96]
    stp x14,x15,[sp,#112]
    stp x16,x17,[sp,#128]
    stp x18,x19,[sp,#144]
    stp x20,x21,[sp,#160]
    stp x22,x23,[sp,#176]
    stp x24,x25,[sp,#192]
    stp x26,x27,[sp,#208]
    stp x28,x29,[sp,#224]
    str x30,[sp,#240]
    str xzr,[sp,#248]
    stp q0,q1,[sp,#256]
    stp q2,q3,[sp,#288]
    stp q4,q5,[sp,#320]
    stp q6,q7,[sp,#352]
    stp q8,q9,[sp,#384]
    stp q10,q11,[sp,#416]
    stp q12,q13,[sp,#448]
    stp q14,q15,[sp,#480]
    stp q16,q17,[sp,#512]
    stp q18,q19,[sp,#544]
    stp q20,q21,[sp,#576]
    stp q22,q23,[sp,#608]
    stp q24,q25,[sp,#640]
    stp q26,q27,[sp,#672]
    stp q28,q29,[sp,#704]
    stp q30,q31,[sp,#736]
    mrs x9,nzcv
    mrs x10,fpcr
    mrs x11,fpsr
    str x9,[sp,#768]
    str x10,[sp,#776]
    str x11,[sp,#784]
    str xzr,[sp,#792]
.endm

/* The interceptor is test-only; observer.cpp is compiled with a symbol rename.
 * It calls the real helper exactly once, captures only the still-live local
 * snapshot afterward, then changes permitted volatile registers and adds
 * explicit synthetic FP/control-state clobber stress. The latter is not a
 * claim that a production helper may violate its FP environment contract.
 */
.global az_usb_entry_observe
.type az_usb_entry_observe,%function
az_usb_entry_observe:
.cfi_startproc
    stp x19,x20,[sp,#-32]!
.cfi_def_cfa_offset 32
.cfi_offset 19,-32
.cfi_offset 20,-24
    stp x29,x30,[sp,#16]
.cfi_offset 29,-16
.cfi_offset 30,-8
    mov x29,sp
    mov x19,x0
    mov w20,w1
    bl az_usb_real_observe
    mov w2,w0
    mov x0,x19
    mov w1,w20
    bl fixture_inspect
    mov x9,#0x60000000
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
    mov x9,#0xeeeeeeeeeeeeeeee
    ins v8.d[1],x9
    ins v9.d[1],x9
    ins v10.d[1],x9
    ins v11.d[1],x9
    ins v12.d[1],x9
    ins v13.d[1],x9
    ins v14.d[1],x9
    ins v15.d[1],x9
    movi v16.16b,#0xee
    movi v17.16b,#0xee
    movi v18.16b,#0xee
    movi v19.16b,#0xee
    movi v20.16b,#0xee
    movi v21.16b,#0xee
    movi v22.16b,#0xee
    movi v23.16b,#0xee
    movi v24.16b,#0xee
    movi v25.16b,#0xee
    movi v26.16b,#0xee
    movi v27.16b,#0xee
    movi v28.16b,#0xee
    movi v29.16b,#0xee
    movi v30.16b,#0xee
    movi v31.16b,#0xee
    mov x0,#0xeeee
    mov x1,#0xeeee
    mov x2,#0xeeee
    mov x3,#0xeeee
    mov x4,#0xeeee
    mov x5,#0xeeee
    mov x6,#0xeeee
    mov x7,#0xeeee
    mov x8,#0xeeee
    mov x9,#0xeeee
    mov x10,#0xeeee
    mov x11,#0xeeee
    mov x12,#0xeeee
    mov x13,#0xeeee
    mov x14,#0xeeee
    mov x15,#0xeeee
    mov x16,#0xeeee
    mov x17,#0xeeee
    mov x18,#0xeeee
    ldp x29,x30,[sp,#16]
.cfi_restore 29
.cfi_restore 30
    ldp x19,x20,[sp],#32
.cfi_restore 19
.cfi_restore 20
.cfi_def_cfa_offset 0
    ret
.global fixture_interceptor_end
.hidden fixture_interceptor_end
fixture_interceptor_end:
.cfi_endproc
.size az_usb_entry_observe,.-az_usb_entry_observe

.macro CALLSITE name,target
.global \name
.type \name,%function
\name:
.cfi_startproc
    stp x29,x30,[sp,#-16]!
.cfi_def_cfa_offset 16
.cfi_offset 29,-16
.cfi_offset 30,-8
    mov x29,sp
    bl \target
.global \name\()_lr
\name\()_lr:
    ldp x29,x30,[sp],#16
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size \name,.-\name
.endm
CALLSITE fixture_call_action,az_usb_action_entry
CALLSITE fixture_call_service,az_usb_service_entry

/* Typed ten-integer call: copy actual incoming ninth/tenth arguments into
 * this callsite's outgoing stack area before entering the production adapter.
 */
.global fixture_call_stack
.type fixture_call_stack,%function
fixture_call_stack:
.cfi_startproc
    sub sp,sp,#32
.cfi_def_cfa_offset 32
    stp x29,x30,[sp,#16]
.cfi_offset 29,-16
.cfi_offset 30,-8
    mov x29,sp
    ldp x9,x10,[sp,#32]
    stp x9,x10,[sp,#0]
    bl az_usb_action_entry
.global fixture_call_stack_lr
fixture_call_stack_lr:
    ldp x29,x30,[sp,#16]
.cfi_restore 29
.cfi_restore 30
    add sp,sp,#32
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size fixture_call_stack,.-fixture_call_stack

/* One stable original address serves distinct explicitly typed machine-ABI
 * return fixtures. No C++ call through an incompatible function-pointer type.
 */
.global fixture_return_dispatch
.type fixture_return_dispatch,%function
fixture_return_dispatch:
.cfi_startproc
    cmp x0,#1
    b.eq fixture_scalar_target
    cmp x0,#2
    b.eq fixture_pair_target
    cmp x0,#3
    b.eq fixture_hfa_target
    cmp x0,#4
    b.eq fixture_vector_target
    cmp x0,#5
    b.eq fixture_sret_target
    b fixture_stack_target
.cfi_endproc
.size fixture_return_dispatch,.-fixture_return_dispatch

.global fixture_register_target
.type fixture_register_target,%function
fixture_register_target:
.cfi_startproc
    sub sp,sp,#800
.cfi_def_cfa_offset 800
    SAVE_ALL
.cfi_offset 19,-648
.cfi_offset 20,-640
.cfi_offset 21,-632
.cfi_offset 22,-624
.cfi_offset 23,-616
.cfi_offset 24,-608
.cfi_offset 25,-600
.cfi_offset 26,-592
.cfi_offset 27,-584
.cfi_offset 28,-576
.cfi_offset 29,-568
.cfi_offset 30,-560
.cfi_offset 72,-416
.cfi_offset 73,-400
.cfi_offset 74,-384
.cfi_offset 75,-368
.cfi_offset 76,-352
.cfi_offset 77,-336
.cfi_offset 78,-320
.cfi_offset 79,-304
    mov x0,sp
    bl fixture_target_capture
    ldp q8,q9,[sp,#384]
    ldp q10,q11,[sp,#416]
    ldp q12,q13,[sp,#448]
    ldp q14,q15,[sp,#480]
.cfi_restore 72
.cfi_restore 73
.cfi_restore 74
.cfi_restore 75
.cfi_restore 76
.cfi_restore 77
.cfi_restore 78
.cfi_restore 79
    ldp x18,x19,[sp,#144]
    ldp x20,x21,[sp,#160]
    ldp x22,x23,[sp,#176]
    ldp x24,x25,[sp,#192]
    ldp x26,x27,[sp,#208]
    ldp x28,x29,[sp,#224]
    ldr x30,[sp,#240]
.cfi_restore 19
.cfi_restore 20
.cfi_restore 21
.cfi_restore 22
.cfi_restore 23
.cfi_restore 24
.cfi_restore 25
.cfi_restore 26
.cfi_restore 27
.cfi_restore 28
.cfi_restore 29
.cfi_restore 30
    ldr x9,[sp,#776]
    msr fpcr,x9
    add sp,sp,#800
.cfi_def_cfa_offset 0
    movz x0,#0x3210
    movk x0,#0x7654,lsl#16
    movk x0,#0xba98,lsl#32
    movk x0,#0xfedc,lsl#48
    ret
.cfi_endproc
.size fixture_register_target,.-fixture_register_target

.macro REGISTER_CALLER name,target,index
.global \name
.type \name,%function
\name:
.cfi_startproc
    sub sp,sp,#320
.cfi_def_cfa_offset 320
    stp x19,x20,[sp,#32]
.cfi_offset 19,-288
.cfi_offset 20,-280
    stp x21,x22,[sp,#48]
.cfi_offset 21,-272
.cfi_offset 22,-264
    stp x23,x24,[sp,#64]
.cfi_offset 23,-256
.cfi_offset 24,-248
    stp x25,x26,[sp,#80]
.cfi_offset 25,-240
.cfi_offset 26,-232
    stp x27,x28,[sp,#96]
.cfi_offset 27,-224
.cfi_offset 28,-216
    stp x29,x30,[sp,#112]
.cfi_offset 29,-208
.cfi_offset 30,-200
    stp q8,q9,[sp,#128]
.cfi_offset 72,-192
.cfi_offset 73,-176
    stp q10,q11,[sp,#160]
.cfi_offset 74,-160
.cfi_offset 75,-144
    stp q12,q13,[sp,#192]
.cfi_offset 76,-128
.cfi_offset 77,-112
    stp q14,q15,[sp,#224]
.cfi_offset 78,-96
.cfi_offset 79,-80
    str x18,[sp,#256]
    mrs x9,fpcr
    mrs x10,fpsr
    stp x9,x10,[sp,#264]
    mrs x9,nzcv
    str x9,[sp,#280]
    adrp x9,fixture_expected_sp
    add x9,x9,:lo12:fixture_expected_sp
    mov x10,sp
    str x10,[x9,#(\index*8)]
    adrp x9,fixture_stack_values
    add x9,x9,:lo12:fixture_stack_values
    ldp x10,x11,[x9,#0]
    stp x10,x11,[sp,#0]
    ldp x10,x11,[x9,#16]
    stp x10,x11,[sp,#16]
    adrp x16,fixture_expected_vectors
    add x16,x16,:lo12:fixture_expected_vectors
    ldp q0,q1,[x16,#0]
    ldp q2,q3,[x16,#32]
    ldp q4,q5,[x16,#64]
    ldp q6,q7,[x16,#96]
    ldp q8,q9,[x16,#128]
    ldp q10,q11,[x16,#160]
    ldp q12,q13,[x16,#192]
    ldp q14,q15,[x16,#224]
    ldp q16,q17,[x16,#256]
    ldp q18,q19,[x16,#288]
    ldp q20,q21,[x16,#320]
    ldp q22,q23,[x16,#352]
    ldp q24,q25,[x16,#384]
    ldp q26,q27,[x16,#416]
    ldp q28,q29,[x16,#448]
    ldp q30,q31,[x16,#480]
    mov x9,#0xa0000000
    msr nzcv,x9
    mov x9,#0x400000
    msr fpcr,x9
    mov x9,#1
    msr fpsr,x9
    adrp x30,fixture_expected_gpr
    add x30,x30,:lo12:fixture_expected_gpr
    ldp x0,x1,[x30,#0]
    ldp x2,x3,[x30,#16]
    ldp x4,x5,[x30,#32]
    ldp x6,x7,[x30,#48]
    ldp x8,x9,[x30,#64]
    ldp x10,x11,[x30,#80]
    ldp x12,x13,[x30,#96]
    ldp x14,x15,[x30,#112]
    ldp x16,x17,[x30,#128]
    ldp x18,x19,[x30,#144]
    ldp x20,x21,[x30,#160]
    ldp x22,x23,[x30,#176]
    ldp x24,x25,[x30,#192]
    ldp x26,x27,[x30,#208]
    ldp x28,x29,[x30,#224]
    bl \target
.global \name\()_lr
\name\()_lr:
    adrp x9,fixture_register_results
    add x9,x9,:lo12:fixture_register_results
    str x0,[x9,#(\index*8)]
    adrp x9,fixture_return_sp
    add x9,x9,:lo12:fixture_return_sp
    mov x10,sp
    str x10,[x9,#(\index*8)]
    adrp x9,fixture_return_stack
    add x9,x9,:lo12:fixture_return_stack
    add x9,x9,#(\index*32)
    ldp x10,x11,[sp,#0]
    stp x10,x11,[x9,#0]
    ldp x10,x11,[sp,#16]
    stp x10,x11,[x9,#16]
    ldp q8,q9,[sp,#128]
    ldp q10,q11,[sp,#160]
    ldp q12,q13,[sp,#192]
    ldp q14,q15,[sp,#224]
.cfi_restore 72
.cfi_restore 73
.cfi_restore 74
.cfi_restore 75
.cfi_restore 76
.cfi_restore 77
.cfi_restore 78
.cfi_restore 79
    ldp x9,x10,[sp,#264]
    msr fpcr,x9
    msr fpsr,x10
    ldr x9,[sp,#280]
    msr nzcv,x9
    ldr x18,[sp,#256]
    ldp x19,x20,[sp,#32]
    ldp x21,x22,[sp,#48]
    ldp x23,x24,[sp,#64]
    ldp x25,x26,[sp,#80]
    ldp x27,x28,[sp,#96]
    ldp x29,x30,[sp,#112]
.cfi_restore 19
.cfi_restore 20
.cfi_restore 21
.cfi_restore 22
.cfi_restore 23
.cfi_restore 24
.cfi_restore 25
.cfi_restore 26
.cfi_restore 27
.cfi_restore 28
.cfi_restore 29
.cfi_restore 30
    add sp,sp,#320
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size \name,.-\name
.endm
REGISTER_CALLER fixture_register_action,az_usb_action_entry,0
REGISTER_CALLER fixture_register_service,az_usb_service_entry,1
.section .note.GNU-stack,"",%progbits

