global cpuSwitchTo
global threadEntryTrampoline
global userThreadTrampoline
extern schedUnlock
extern schedThreadExit

section .text

; void cpuSwitchTo(Thread *prev, Thread *next)
cpuSwitchTo:
push rbx
push rbp
push r12
push r13
push r14
push r15

mov [rdi], rsp
mov rsp, [rsi]

pop r15
pop r14
pop r13
pop r12
pop rbp
pop rbx

ret

threadEntryTrampoline:
call schedUnlock
sti

mov rdi, r13
call r12

call schedThreadExit

.halt:
cli
hlt
jmp .halt

userThreadTrampoline:
call schedUnlock

mov rdi, r12
xor rax, rax
xor rbx, rbx
xor rcx, rcx
xor rdx, rdx
xor rsi, rsi
xor rbp, rbp
xor r8, r8
xor r9, r9
xor r10, r10
xor r11, r11
xor r12, r12
xor r13, r13
xor r14, r14
xor r15, r15

cli
swapgs
iretq
