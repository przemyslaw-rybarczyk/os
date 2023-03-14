#include "types.h"

#include "framebuffer.h"
#include "page.h"

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

bool syscall_map_pages(u64 start, u64 length, u64 flags) {
    if (flags & ~(MAP_PAGES_WRITE | MAP_PAGES_EXECUTE))
        return false;
    return map_user_pages(start, length, flags & MAP_PAGES_WRITE, flags & MAP_PAGES_EXECUTE);
}

bool syscall_print_char(u64 c) {
    framebuffer_lock();
    print_char(c);
    framebuffer_unlock();
    return true;
}

const void * const syscalls[] = {
    syscall_map_pages,
    syscall_print_char,
};
