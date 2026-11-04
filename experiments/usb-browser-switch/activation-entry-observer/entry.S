/* SPDX-License-Identifier: GPL-3.0-only
 * Inactive Linux AArch64 entry-only adapters, ordinary fixed-width state only.
 * No original target is called here except the final tail BR. Each corresponding
 * original slot must be stable/non-null before reachability; disabled, contended
 * and full capture still forward once. No return hook or cleanup scope survives.
 * Snapshot preserves state seen at this entry, except x16 used for tail transfer.
 * Installer/island x17 scratch, SVE/SME, PAC/BTI, signals/cancellation, foreign
 * unwinding and installed-trampoline CFI remain outside this contract.
 */
.text
.p2align 2
.hidden az_usb_entry_observe
.hidden az_usb_action_original
.hidden az_usb_service_original

.macro usb_entry name, boundary, original
.global \name
.type \name, %function
\name:
.cfi_startproc
.cfi_def_cfa sp, 0
    sub sp, sp, #800
.cfi_def_cfa_offset 800
    stp x0, x1, [sp, #0]
.cfi_offset 0, -800
.cfi_offset 1, -792
    stp x2, x3, [sp, #16]
.cfi_offset 2, -784
.cfi_offset 3, -776
    stp x4, x5, [sp, #32]
.cfi_offset 4, -768
.cfi_offset 5, -760
    stp x6, x7, [sp, #48]
.cfi_offset 6, -752
.cfi_offset 7, -744
    stp x8, x9, [sp, #64]
.cfi_offset 8, -736
.cfi_offset 9, -728
    stp x10, x11, [sp, #80]
.cfi_offset 10, -720
.cfi_offset 11, -712
    stp x12, x13, [sp, #96]
.cfi_offset 12, -704
.cfi_offset 13, -696
    stp x14, x15, [sp, #112]
.cfi_offset 14, -688
.cfi_offset 15, -680
    stp x16, x17, [sp, #128]
.cfi_offset 16, -672
.cfi_offset 17, -664
    stp x18, x19, [sp, #144]
.cfi_offset 18, -656
.cfi_offset 19, -648
    stp x20, x21, [sp, #160]
.cfi_offset 20, -640
.cfi_offset 21, -632
    stp x22, x23, [sp, #176]
.cfi_offset 22, -624
.cfi_offset 23, -616
    stp x24, x25, [sp, #192]
.cfi_offset 24, -608
.cfi_offset 25, -600
    stp x26, x27, [sp, #208]
.cfi_offset 26, -592
.cfi_offset 27, -584
    stp x28, x29, [sp, #224]
.cfi_offset 28, -576
.cfi_offset 29, -568
    str x30, [sp, #240]
.cfi_offset 30, -560
    str xzr, [sp, #248]
    stp q0, q1, [sp, #256]
.cfi_offset 64, -544
.cfi_offset 65, -528
    stp q2, q3, [sp, #288]
.cfi_offset 66, -512
.cfi_offset 67, -496
    stp q4, q5, [sp, #320]
.cfi_offset 68, -480
.cfi_offset 69, -464
    stp q6, q7, [sp, #352]
.cfi_offset 70, -448
.cfi_offset 71, -432
    stp q8, q9, [sp, #384]
.cfi_offset 72, -416
.cfi_offset 73, -400
    stp q10, q11, [sp, #416]
.cfi_offset 74, -384
.cfi_offset 75, -368
    stp q12, q13, [sp, #448]
.cfi_offset 76, -352
.cfi_offset 77, -336
    stp q14, q15, [sp, #480]
.cfi_offset 78, -320
.cfi_offset 79, -304
    stp q16, q17, [sp, #512]
.cfi_offset 80, -288
.cfi_offset 81, -272
    stp q18, q19, [sp, #544]
.cfi_offset 82, -256
.cfi_offset 83, -240
    stp q20, q21, [sp, #576]
.cfi_offset 84, -224
.cfi_offset 85, -208
    stp q22, q23, [sp, #608]
.cfi_offset 86, -192
.cfi_offset 87, -176
    stp q24, q25, [sp, #640]
.cfi_offset 88, -160
.cfi_offset 89, -144
    stp q26, q27, [sp, #672]
.cfi_offset 90, -128
.cfi_offset 91, -112
    stp q28, q29, [sp, #704]
.cfi_offset 92, -96
.cfi_offset 93, -80
    stp q30, q31, [sp, #736]
.cfi_offset 94, -64
.cfi_offset 95, -48
    mrs x9, nzcv
    mrs x10, fpcr
    mrs x11, fpsr
    str x9, [sp, #768]
    str x10, [sp, #776]
    str x11, [sp, #784]
    str xzr, [sp, #792]
    mov x0, sp
    mov w1, #\boundary
    bl az_usb_entry_observe
    ldr x9, [sp, #768]
    ldr x10, [sp, #776]
    ldr x11, [sp, #784]
    msr nzcv, x9
    msr fpcr, x10
    msr fpsr, x11
    ldp q0, q1, [sp, #256]
.cfi_restore 64
.cfi_restore 65
    ldp q2, q3, [sp, #288]
.cfi_restore 66
.cfi_restore 67
    ldp q4, q5, [sp, #320]
.cfi_restore 68
.cfi_restore 69
    ldp q6, q7, [sp, #352]
.cfi_restore 70
.cfi_restore 71
    ldp q8, q9, [sp, #384]
.cfi_restore 72
.cfi_restore 73
    ldp q10, q11, [sp, #416]
.cfi_restore 74
.cfi_restore 75
    ldp q12, q13, [sp, #448]
.cfi_restore 76
.cfi_restore 77
    ldp q14, q15, [sp, #480]
.cfi_restore 78
.cfi_restore 79
    ldp q16, q17, [sp, #512]
.cfi_restore 80
.cfi_restore 81
    ldp q18, q19, [sp, #544]
.cfi_restore 82
.cfi_restore 83
    ldp q20, q21, [sp, #576]
.cfi_restore 84
.cfi_restore 85
    ldp q22, q23, [sp, #608]
.cfi_restore 86
.cfi_restore 87
    ldp q24, q25, [sp, #640]
.cfi_restore 88
.cfi_restore 89
    ldp q26, q27, [sp, #672]
.cfi_restore 90
.cfi_restore 91
    ldp q28, q29, [sp, #704]
.cfi_restore 92
.cfi_restore 93
    ldp q30, q31, [sp, #736]
.cfi_restore 94
.cfi_restore 95
    ldp x0, x1, [sp, #0]
.cfi_restore 0
.cfi_restore 1
    ldp x2, x3, [sp, #16]
.cfi_restore 2
.cfi_restore 3
    ldp x4, x5, [sp, #32]
.cfi_restore 4
.cfi_restore 5
    ldp x6, x7, [sp, #48]
.cfi_restore 6
.cfi_restore 7
    ldp x8, x9, [sp, #64]
.cfi_restore 8
.cfi_restore 9
    ldp x10, x11, [sp, #80]
.cfi_restore 10
.cfi_restore 11
    ldp x12, x13, [sp, #96]
.cfi_restore 12
.cfi_restore 13
    ldp x14, x15, [sp, #112]
.cfi_restore 14
.cfi_restore 15
    ldp x16, x17, [sp, #128]
.cfi_restore 16
.cfi_restore 17
    ldp x18, x19, [sp, #144]
.cfi_restore 18
.cfi_restore 19
    ldp x20, x21, [sp, #160]
.cfi_restore 20
.cfi_restore 21
    ldp x22, x23, [sp, #176]
.cfi_restore 22
.cfi_restore 23
    ldp x24, x25, [sp, #192]
.cfi_restore 24
.cfi_restore 25
    ldp x26, x27, [sp, #208]
.cfi_restore 26
.cfi_restore 27
    ldp x28, x29, [sp, #224]
.cfi_restore 28
.cfi_restore 29
    ldr x30, [sp, #240]
.cfi_restore 30
    add sp, sp, #800
.cfi_def_cfa_offset 0
    /* x16 becomes the target-address scratch; it is no longer recoverable. */
    adrp x16, :got:\original
.cfi_undefined 16
    ldr x16, [x16, :got_lo12:\original]
    ldar x16, [x16]
    br x16
.global \name\()_end
.hidden \name\()_end
\name\()_end:
.cfi_endproc
.size \name, .-\name
.endm

usb_entry az_usb_action_entry, 1, az_usb_action_original
.p2align 2
usb_entry az_usb_service_entry, 2, az_usb_service_original
.section .note.GNU-stack,"",%progbits
