    .text
    .global main
    .type main, %function

    .extern printf
    .extern fflush

/* format string in rodata */
    .section .rodata
fmt:
    .asciz "%#x"
.macro trap
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

    // getpid
    mov    x8, #172       // syscall: getpid
    svc    #0              // make syscall
    mov    x12, x0

    trap
    /* build test value into x20 (64-bit) */
    // movz    x20, #0xBABE
    // movk    x20, #0xCAFE, lsl #16
    // movk    x20, #0xBEEF, lsl #32
    // movk    x20, #0xDEAD, lsl #48

    /* load address of fmt into x0 (using adrp/add for PIC correctness) */
    adrp    x0, :pg_hi21:fmt
    add     x0, x0, :lo12:fmt

    /* first printf argument (value) goes in x1 */
    mov     x1, x20

    /* call printf(fmt, x20) */
    bl      printf

    /* call fflush(NULL) to flush all stdio output (x0 = 0) */
    mov     x0, xzr      // NULL
    bl      fflush

    
    trap
    /* return 0 from main */
    // mov     x0, #0

    /* epilogue: restore FP & LR and return */
    ldp     x29, x30, [sp], #16
    ret

    .size main, .-main
