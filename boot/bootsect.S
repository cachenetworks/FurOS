#define BOOT_INFO_LBA 1
#define KERNEL_LBA_START 2
#define BOOT_INFO_BUF_OFFSET 0x0600
#define BOOT_INFO_BUF_SEG 0x0000
#define KERNEL_LOAD_BUFFER_PHYS 0x00010000
#define KERNEL_LOAD_BUFFER_SEG  0x1000
#define KERNEL_LOAD_ADDR        0x00100000
#define MAX_KERNEL_SECTORS      1920

.section .text
.code16
.global _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    movb %dl, boot_drive

    movw $BOOT_INFO_BUF_OFFSET, dap_offset
    movw $BOOT_INFO_BUF_SEG, dap_segment
    movl $BOOT_INFO_LBA, dap_lba_low
    movl $0, dap_lba_high
    movb $0x42, %ah
    movb boot_drive, %dl
    movw $dap, %si
    int $0x13
    jc disk_error

    xorw %si, %si
check_magic:
    movb boot_magic(%si), %al
    cmpb %al, BOOT_INFO_BUF_OFFSET(%si)
    jne invalid_info
    incw %si
    cmpw $8, %si
    jne check_magic

    movl BOOT_INFO_BUF_OFFSET + 8, %eax
    movl %eax, kernel_size_bytes
    movl BOOT_INFO_BUF_OFFSET + 12, %eax
    movl %eax, kernel_sector_count
    movl BOOT_INFO_BUF_OFFSET + 16, %eax
    movl %eax, kernel_entry_offset

    cmpl $0, kernel_size_bytes
    je invalid_info
    cmpl $0, kernel_sector_count
    je invalid_info
    cmpl $MAX_KERNEL_SECTORS, kernel_sector_count
    ja invalid_info

    xorw %cx, %cx
load_loop:
    movzwl %cx, %eax
    cmpl kernel_sector_count, %eax
    jae load_done

    movw %cx, %bx
    shlw $5, %bx
    movw $0x0000, dap_offset
    movw $KERNEL_LOAD_BUFFER_SEG, %ax
    addw %bx, %ax
    movw %ax, dap_segment

    movl $KERNEL_LBA_START, %eax
    movzwl %cx, %edx
    addl %edx, %eax
    movl %eax, dap_lba_low
    movl $0, dap_lba_high

    movb $0x42, %ah
    movb boot_drive, %dl
    movw $dap, %si
    int $0x13
    jc disk_error

    incw %cx
    jmp load_loop

load_done:
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    lgdt gdt_descriptor

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    ljmp $0x08, $pmode_entry

invalid_info:
    movw $invalid_msg, %si
    jmp print_loop

disk_error:
    movw $error_msg, %si
print_loop:
    lodsb
    testb %al, %al
    jz halt
    movb $0x0e, %ah
    movb $0x07, %bl
    int $0x10
    jmp print_loop

halt:
    hlt
    jmp halt

gdt_table:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_table - 1
    .long gdt_table

boot_drive:
    .byte 0

error_msg:
    .asciz "Disk boot error"
invalid_msg:
    .asciz "Invalid install data"

boot_magic:
    .ascii "FURBOOT1"

kernel_size_bytes:
    .long 0
kernel_sector_count:
    .long 0
kernel_entry_offset:
    .long 0

.align 16
dap:
    .byte 0x10
    .byte 0x00
dap_count:
    .word 0x0001
dap_offset:
    .word 0x0000
dap_segment:
    .word KERNEL_LOAD_BUFFER_SEG
dap_lba_low:
    .long 0x00000000
dap_lba_high:
    .long 0x00000000

.code32
pmode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl $KERNEL_LOAD_BUFFER_PHYS, %esi
    movl $KERNEL_LOAD_ADDR, %edi
    movl kernel_size_bytes, %ecx
    cld
    rep movsb

    movl kernel_entry_offset, %eax
    addl $KERNEL_LOAD_ADDR, %eax
    jmp *%eax

.org 510
.word 0xaa55
