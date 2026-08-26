.section .text
.global _start
.type _start, @function
_start:
    movq %rsp, %rbp
    movq %rsp, %rdi
    subq $8, %rsp
    pushq %rbp
    call facet_native_start
1:
    pause
    jmp 1b

.global _init
.type _init, @function
_init:
    ret

.global _fini
.type _fini, @function
_fini:
    ret
