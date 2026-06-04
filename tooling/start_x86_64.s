# Freestanding entry point for x86-64 Linux: call main, exit with its return.
.intel_syntax noprefix
.global _start
_start:
    call main
    mov edi, eax
    mov eax, 60      # exit
    syscall
