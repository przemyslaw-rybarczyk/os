#include <zr/types.h>
#include <string.h>

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

void *memchr(const void *p, int c, size_t n) {
    const u8 *s = p;
    for (size_t i = 0; i < n; i++)
        if (s[i] == (u8)c)
            return (void *)&s[i];
    return NULL;
}

size_t strlen(const char *s) {
    size_t i = 0;
    while (s[i] != '\0')
        i++;
    return i;
}

int strcmp(const char *s1, const char *s2) {
    return strncmp(s1, s2, SIZE_MAX);
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)s1[i] < (unsigned char)s2[i])
            return -1;
        if ((unsigned char)s1[i] > (unsigned char)s2[i])
            return 1;
        if (s1[i] == '\0')
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    for (size_t i = 0; ; i++) {
        if (s[i] == c)
            return (char *)&s[i];
        if (s[i] == '\0')
            return NULL;
    }
}

char *strrchr(const char *s, int c) {
    char *ret = NULL;
    for (size_t i = 0; ; i++) {
        if (s[i] == c)
            ret = (char *)&s[i];
        if (s[i] == '\0')
            return ret;
    }
}

size_t strspn(const char *s, const char *set) {
    for (size_t i = 0; ; i++) {
        for (size_t j = 0; ; j++) {
            if (set[j] == '\0')
                return i;
            if (set[j] == s[i])
                break;
        }
    }
}

size_t strcspn(const char *s, const char *set) {
    for (size_t i = 0; ; i++) {
        if (s[i] == '\0')
            return i;
        for (size_t j = 0; set[j] != '\0'; j++) {
            if (set[j] == s[i])
                return i;
        }
    }
}

char *strpbrk(const char *s, const char *set) {
    size_t i = strcspn(s, set);
    if (s[i] == '\0')
        return NULL;
    return (char *)&s[i];
}

char *strstr(const char *s1, const char *s2) {
    for (size_t i = 0; s1[i] != '\0'; i++) {
        for (size_t j = 0; ; j++) {
            if (s2[j] == '\0')
                return (char *)&s1[i];
            if (s1[i + j] != s2[j])
                break;
        }
    }
    return NULL;
}

char *strcpy(char *dest, const char *src) {
    for (size_t i = 0; ; i++) {
        dest[i] = src[i];
        if (src[i] == '\0')
            return dest;
    }
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src) {
    return strncat(dest, src, SIZE_MAX);
}

char *strncat(char *dest, const char *src, size_t n) {
    size_t i = strlen(dest);
    for (size_t j = 0; ; j++) {
        if (j >= n || src[j] == '\0') {
            dest[i + j] = '\0';
            return dest;
        }
        dest[i + j] = src[j];
    }
}
