#include <stdio.h>

char buf[20];

void main(void) {
    int x = printf("AAA %% %d %d 0x%x 0x%X 0%o %s%c %s%c\n", 1234, -1234, 0x1a2b, 0x1A2B, 01234, "Hello", ',', "World", '!');
    int y = snprintf(buf, 20, "AAA %% %d %d 0x%x 0x%X 0%o %s%c %s%c\n", 1234, -1234, 0x1a2b, 0x1A2B, 01234, "Hello", ',', "World", '!');
    puts(buf);
    putchar('\n');
    printf("%d %d\n", x, y);
}
