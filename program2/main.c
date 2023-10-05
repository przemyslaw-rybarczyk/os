#include <stdio.h>

void main(void) {
    while (1) {
        int c = getchar();
        if (c == EOF)
            break;
        putchar(c);
    }
}
