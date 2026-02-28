OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start32)

SECTIONS
{
    . = 0x100000;
    __kernel_image_start = .;

    .multiboot : ALIGN(8)
    {
        KEEP(*(.multiboot))
    }

    .text : ALIGN(0x1000)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    }

    __kernel_image_end = .;

    .bss : ALIGN(0x1000)
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}
