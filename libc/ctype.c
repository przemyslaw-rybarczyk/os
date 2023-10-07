#include <ctype.h>

int isalnum(int c) {
    return ('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

int isalpha(int c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

int islower(int c) {
    return 'a' <= c && c <= 'z';
}

int isupper(int c) {
    return 'A' <= c && c <= 'Z';
}

int isdigit(int c) {
    return '0' <= c && c <= '9';
}

int isxdigit(int c) {
    return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

int iscntrl(int c) {
    return (0x00 <= c && c <= 0x1F) || c == 0x7F;
}

int isgraph(int c) {
    return '!' <= c && c <= '~';
}

int isspace(int c) {
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

int isblank(int c) {
    return c == ' ' || c == '\t';
}

int isprint(int c) {
    return ' ' <= c && c <= '~';
}

int ispunct(int c) {
    return ('!' <= c && c <= '/') || (':' <= c && c <= '@') || ('[' <= c && c <= '`') || ('{' <= c && c <= '~');
}

int tolower(int c) {
    if ('A' <= c && c <= 'Z')
        return c - 'A' + 'a';
    else
        return c;
}

int toupper(int c) {
    if ('a' <= c && c <= 'z')
        return c - 'a' + 'A';
    else
        return c;
}
