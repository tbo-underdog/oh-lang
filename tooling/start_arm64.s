// Freestanding entry point for aarch64 Linux: call main, exit with its return.
.global _start
_start:
    bl main
    mov x8, #93      // exit
    svc #0
