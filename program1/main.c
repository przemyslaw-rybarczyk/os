#include "types.h"

#include <syscalls.h>

void main(u64 arg) {
    for (size_t i = 0; i < 1000; i++) {
        print_char(arg);
    }
}
