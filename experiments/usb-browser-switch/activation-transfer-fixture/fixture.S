/* SPDX-License-Identifier: GPL-3.0-only
 * Authored fixture code, never a firmware copy. SP-based static FDEs describe
 * only these functions. No metadata is registered for generated anonymous code.
 */
.text
.p2align 2
.global fixture_action_target
.type fixture_action_target,%function
fixture_action_target:
.cfi_startproc
.cfi_def_cfa sp,0
    stp x29,x30,[sp,#-144]!
.cfi_def_cfa_offset 144
.cfi_offset 29,-144
.cfi_offset 30,-136
    mov x29,sp
    str x16,[x0,#32]
    str x17,[x0,#40]
    add x9,sp,#144
    str x9,[x0,#48]
    str x30,[x0,#56]
    stp x0,x1,[x0,#64]
    str x2,[x0,#80]
    mov x9,sp
    str x9,[x0,#88]
    str x29,[x0,#96]
    mov x3,#1
    bl transfer_body
    ldp x29,x30,[sp],#144
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.global fixture_action_target_end
fixture_action_target_end:
.cfi_endproc
.size fixture_action_target,.-fixture_action_target

.p2align 2
.global fixture_service_target
.type fixture_service_target,%function
fixture_service_target:
.cfi_startproc
.cfi_def_cfa sp,0
    stp x29,x30,[sp,#-272]!
.cfi_def_cfa_offset 272
.cfi_offset 29,-272
.cfi_offset 30,-264
    mov x29,sp
    str x16,[x0,#32]
    str x17,[x0,#40]
    add x9,sp,#272
    str x9,[x0,#48]
    str x30,[x0,#56]
    stp x0,x1,[x0,#64]
    str x2,[x0,#80]
    mov x9,sp
    str x9,[x0,#88]
    str x29,[x0,#96]
    mov x3,#2
    bl transfer_body
    ldp x29,x30,[sp],#272
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.global fixture_service_target_end
fixture_service_target_end:
.cfi_endproc
.size fixture_service_target,.-fixture_service_target

.macro fixture_caller name,target
.p2align 2
.global \name
.type \name,%function
\name:
.cfi_startproc
.cfi_def_cfa sp,0
    stp x29,x30,[sp,#-32]!
.cfi_def_cfa_offset 32
.cfi_offset 29,-32
.cfi_offset 30,-24
    mov x29,sp
    mov x9,sp
    str x9,[x0,#0]
    adr x9,1f
    str x9,[x0,#8]
    mov x16,#0x1616
    mov x17,#0x1717
    stp x16,x17,[x0,#16]
    bl \target
1:
    ldp x29,x30,[sp],#32
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.global \name\()_end
\name\()_end:
.cfi_endproc
.size \name,.-\name
.endm
fixture_caller fixture_call_action,fixture_action_target
fixture_caller fixture_call_service,fixture_service_target
.section .note.GNU-stack,"",%progbits
