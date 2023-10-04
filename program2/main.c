#include <stdio.h>

#define BUF_SIZE 80

char buf[BUF_SIZE];

void main(void) {
    while (1) {
        char *s = fgets(buf, BUF_SIZE, stdin);
        if (s == NULL)
            break;
        fputs(s, stdout);
    }
}
