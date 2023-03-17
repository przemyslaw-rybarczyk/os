#include "types.h"

#include "error.h"
#include "framebuffer.h"
#include "page.h"
#include "process.h"

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

err_t syscall_map_pages(u64 start, u64 length, u64 flags) {
    if (flags & ~(MAP_PAGES_WRITE | MAP_PAGES_EXECUTE))
        return ERR_INVALID_ARG;
    return map_user_pages(start, length, flags & MAP_PAGES_WRITE, flags & MAP_PAGES_EXECUTE);
}

err_t syscall_print_char(u64 c) {
    framebuffer_lock();
    print_char(c);
    framebuffer_unlock();
    return 0;
}

err_t syscall_process_exit(void) {
    process_exit();
}

const void * const syscalls[] = {
    syscall_map_pages,
    syscall_print_char,
    syscall_process_exit,
};
