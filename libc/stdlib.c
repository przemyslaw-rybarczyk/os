#include <stdlib.h>

int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

long long llabs(long long n) {
    return n < 0 ? -n : n;
}

div_t div(int x, int y) {
    return (div_t){.quot = x / y, .rem = x % y};
}

ldiv_t ldiv(long x, long y) {
    return (ldiv_t){.quot = x / y, .rem = x % y};
}

lldiv_t lldiv(long long x, long long y) {
    return (lldiv_t){.quot = x / y, .rem = x % y};
}
