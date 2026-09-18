global _start
extern ldMain
extern _DYNAMIC

section .text

_start:
mov r12, rsp

mov rdi, r12
lea rsi, [rel _DYNAMIC]
call ldMain

mov r15, rax

mov rsp, r12

xor rax, rax
xor rbx, rbx
xor rcx, rcx
xor rdx, rdx
xor rsi, rsi
xor rdi, rdi
xor rbp, rbp
xor r8,  r8
xor r9,  r9
xor r10, r10
xor r11, r11
xor r12, r12
xor r13, r13
xor r14, r14

jmp r15
