#include <stdio.h>

void main(void) {
    while (1) {
        char c = getchar();
        if (c == EOF)
            break;
        putchar(c);
    }
}
