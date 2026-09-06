global _start
extern main
global exit

section .text
_start:
xor rbp, rbp
pop rdi
mov rsi, rsp

mov rdx, rdi
inc rdx
shl rdx, 3
add rdx, rsi

and rsp, -16
call main

mov rdi, rax
call exit

.halt:
hlt
jmp .halt

exit:
mov rax, 60
syscall
.halt:
hlt
jmp .halt
