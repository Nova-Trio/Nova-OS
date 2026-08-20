global idt_load
global isr_table
extern interrupt_dispatch

section .text

idt_load:
lidt [rdi]
ret


%macro ISR_NOERR 1
isr_%1:
push qword 0
push qword %1
jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_%1:
push qword %1
jmp isr_common
%endmacro

%assign i 0
%rep 256
  %if i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30
    ISR_ERR i
  %else
    ISR_NOERR i
  %endif
  %assign i i+1
%endrep

isr_common:
push rax
push rbx
push rcx
push rdx
push rsi
push rdi
push rbp
push r8
push r9
push r10
push r11
push r12
push r13
push r14
push r15

mov rdi, rsp
cld
call interrupt_dispatch

pop r15
pop r14
pop r13
pop r12
pop r11
pop r10
pop r9
pop r8
pop rbp
pop rdi
pop rsi
pop rdx
pop rcx
pop rbx
pop rax

add rsp, 16
iretq

section .data
align 8
isr_table:
%assign i 0
%rep 256
  dq isr_%+ i
  %assign i i+1
%endrep
