#include "types.h"

#include <syscalls.h>

void main(u64 arg) {
    while (1) {
        print_char('-');
        print_char(arg);
    }
}
