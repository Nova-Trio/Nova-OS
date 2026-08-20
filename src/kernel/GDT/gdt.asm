global gdt_load
global gdt_load_tss

section .text

gdt_load;
lgdt [rdi]

mov ds, dx
mov es, dx
mov fs, dx
mov gs, dx
mov ss, dx

push rsi
lea rax, [rel .reload_cs]
push rax
retfq

.reload_cs:
ret

gdt_load_tss:
ltr di
ret
