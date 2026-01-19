    .text
    .global main
    .type main, %function

    .extern printf
    .extern fflush

/* format string in rodata */
    .section .rodata
fmt:
    .asciz "%#x"

fmt_128:
    .asciz "%016llx %016llx"


.macro trap
    // getpid
    mov    x8, #172       // syscall: getpid
    svc    #0              // make syscall
    mov    x12, x0
    mov     x8, #129        // syscall: kill
    mov     x0, x12        // pid (from getpid)
    mov     x1, #5         // SIGTRAP
    svc     #0
.endm
    .text

main:
    /* prologue: save FP & LR, maintain 16-byte alignment */
    stp     x29, x30, [sp, #-16]!   // sp -= 16; store x29, x30
    mov     x29, sp



    // trap
    /* build test value into x20 (64-bit) */
    // movz    x20, #0xBABE
    // movk    x20, #0xCAFE, lsl #16
    // movk    x20, #0xBEEF, lsl #32
    movz    x22, #0xBEEF
    movk    x22, #0xDEAD, lsl #16
    // movz    w22, #0xBEEF
    // movk    w22, #0xDEAD, lsl #16
    // mov     x0, xzr
    // bl      fflush

    trap

    /* epilogue: restore FP & LR and return */
    ldp     x29, x30, [sp], #16
    ret

    .size main, .-main
