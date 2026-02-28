.section .multiboot,"a"
.align 8
multiboot_header:
    .long 0xe85250d6
    .long 0
    .long multiboot_header_end - multiboot_header
    .long -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header))
    .short 0
    .short 0
    .long 8
multiboot_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.align 4
.global fur_boot_magic
fur_boot_magic:
    .long 0

.align 4096
pml4_table:
    .skip 4096
pdp_table:
    .skip 4096
pd_table0:
    .skip 4096
pd_table1:
    .skip 4096
pd_table2:
    .skip 4096
pd_table3:
    .skip 4096

.section .rodata
.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.section .text
.code32
.global _start32
.type _start32, @function
_start32:
    cli
    movl %eax, fur_boot_magic
    movl $stack_top, %esp

    call setup_page_tables
    call enable_long_mode

    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

.type setup_page_tables, @function
setup_page_tables:
    xorl %eax, %eax
    movl $pml4_table, %edi
    movl $(4096 * 6 / 4), %ecx
    rep stosl

    movl $pdp_table, %eax
    orl $0x03, %eax
    movl %eax, pml4_table
    movl $0, pml4_table + 4

    movl $pd_table0, %eax
    orl $0x03, %eax
    movl %eax, pdp_table
    movl $0, pdp_table + 4

    movl $pd_table1, %eax
    orl $0x03, %eax
    movl %eax, pdp_table + 8
    movl $0, pdp_table + 12

    movl $pd_table2, %eax
    orl $0x03, %eax
    movl %eax, pdp_table + 16
    movl $0, pdp_table + 20

    movl $pd_table3, %eax
    orl $0x03, %eax
    movl %eax, pdp_table + 24
    movl $0, pdp_table + 28

    xorl %edx, %edx
    movl $pd_table0, %ebx
2:
    xorl %ecx, %ecx
1:
    movl %ecx, %eax
    movl %edx, %esi
    shll $9, %esi
    addl %esi, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, (%ebx, %ecx, 8)
    movl $0, 4(%ebx, %ecx, 8)
    incl %ecx
    cmpl $512, %ecx
    jne 1b
    addl $4096, %ebx
    incl %edx
    cmpl $4, %edx
    jne 2b
    ret

.type enable_long_mode, @function
enable_long_mode:
    movl $pml4_table, %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    movl $0xc0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0
    ret

.code64
.global long_mode_start
.type long_mode_start, @function
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    leaq stack_top(%rip), %rsp
    call kernel_main

2:
    hlt
    jmp 2b

.section .note.GNU-stack, "", @progbits
