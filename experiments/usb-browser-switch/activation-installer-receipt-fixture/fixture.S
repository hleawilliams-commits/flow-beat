/* Authored AArch64 scalar targets and permanent replacement code.
 * No firmware bytes, exceptions, asynchronous-unwind or full ABI proof.
 * Each target occupies its own 4096-byte code page. Descriptor3 deliberately
 * aliases target0 in the receipt owner; target3 remains an unused owned symbol. */
.section .text.rc_targets,"ax",%progbits

.macro TARGET index
.balign 4096
.global rc_target\index
.type rc_target\index,%function
rc_target\index:
.cfi_startproc
.if \index == 0
    /* Actual installer may re-encode or expand this ADRP; no asserted out count. */
    adrp x9, rc_target_hits
    add x9, x9, :lo12:rc_target_hits
    ldr x10, [x9]
    add x10, x10, #1
    str x10, [x9]
    add x0, x0, #64
.else
    add x0, x0, #(64 + \index)
    nop
    nop
    nop
    adrp x9, rc_target_hits
    add x9, x9, :lo12:rc_target_hits
    ldr x10, [x9, #(8 * \index)]
    add x10, x10, #1
    str x10, [x9, #(8 * \index)]
.endif
    ret
.cfi_endproc
.size rc_target\index, .-rc_target\index
.space 4096-(.-rc_target\index), 0
.global rc_target\index\()_page_end
rc_target\index\()_page_end:
.endm
TARGET 0
TARGET 1
TARGET 2
TARGET 3

.section .text.rc_replacements,"ax",%progbits
.macro REPLACEMENT index
.balign 16
.global rc_replacement\index
.type rc_replacement\index,%function
rc_replacement\index:
.cfi_startproc
    adrp x9, rc_replacement_hits
    add x9, x9, :lo12:rc_replacement_hits
    ldr x10, [x9, #(8 * \index)]
    add x10, x10, #1
    str x10, [x9, #(8 * \index)]
    add x0, x0, #(256 + \index)
    adrp x16, rc_originals
    add x16, x16, :lo12:rc_originals
    add x16, x16, #(8 * \index)
    ldar x16, [x16]
    br x16
.cfi_endproc
.size rc_replacement\index, .-rc_replacement\index
.endm
REPLACEMENT 0
REPLACEMENT 1
REPLACEMENT 2
REPLACEMENT 3
.section .note.GNU-stack,"",%progbits
