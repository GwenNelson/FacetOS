.section .text
.global _start
.type _start, @function
_start:
    movq %rsp, %rbp
    movq %rsp, %rdi
    subq $8, %rsp
    pushq %rbp
    call facet_posix_start
1:
    pause
    jmp 1b

/* sel4runtime calls these hooks while loading and tearing down the runtime.
 * Defining complete no-op hooks here prevents the linker from extracting only
 * crti.o's unmatched prologue from the sel4runtime archive when crtn.o is
 * deliberately absent from this custom CRT. */
.global _init
.type _init, @function
_init:
    ret

.global _fini
.type _fini, @function
_fini:
    ret
