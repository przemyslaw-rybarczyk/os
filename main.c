void kernel_main(void) {
    while (1)
        asm volatile("hlt");
}
