global cpuSwitchTo
global threadEntryTrampoline
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
