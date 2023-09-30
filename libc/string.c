#ifdef _KERNEL

#include "types.h"
#include "string.h"

#else

#include <zr/types.h>
#include <string.h>

#endif

int memcmp(const void *p1, const void *p2, size_t n) {
    const u8 *s1 = p1;
    const u8 *s2 = p2;
    for (size_t i = 0; i < n; i++) {
        if (s1[i] < s2[i])
            return -1;
        if (s1[i] > s2[i])
            return 1;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t i = 0;
    while (s[i] != '\0')
        i++;
    return i;
}
