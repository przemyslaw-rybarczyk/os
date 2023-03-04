#include "types.h"
#include "stack.h"

#include "page.h"
#include "smp.h"
#include "spinlock.h"

#define STACK_PML4E 0x1FEull
#define KERNEL_INIT_STACK ASSEMBLE_ADDR_PML4E(STACK_PML4E, 0)
#define KERNEL_STACK_AREA_END (KERNEL_INIT_STACK + PDPT_SIZE)

static spinlock_t stack_alloc_lock = SPINLOCK_FREE;

// The address of the last stack that has memory allocated for it
// This variable is global so it can be used to set up the initial kernel stacks for each core.
u64 last_kernel_stack = KERNEL_INIT_STACK;

// The address of the first unused allocated kernel stack
// If it's 0, all allocated stacks are in use.
// All unused kernel stacks form a linked list, where each one starts with the address of the next unused stack (not in any particular order).
// Not that the stack addresses used here point to the beginning of the stack's memory and not at it's bottom, which comes at the end.
static u64 first_free_kernel_stack = 0;

// Set up the initial stack for each AP
// The initialization is later completed by the AP initialization code, which increments the `last_kernel_stack` variable.
bool stack_init(void) {
    for (size_t i = 1; i < cpu_num; i++) {
        if (!map_page(KERNEL_INIT_STACK + 2 * i * PAGE_SIZE, false, true, true, false))
            return false;
    }
    return true;
}

// Allocate a new kernel stack
// Returns a pointer to the bottom of the stack.
// Returns NULL on failure.
void *stack_alloc(void) {
    spinlock_acquire(&stack_alloc_lock);
    if (first_free_kernel_stack != 0) {
        // Get the first free allocated stack if there is one
        u64 stack_addr = first_free_kernel_stack;
        first_free_kernel_stack = *(u64 *)first_free_kernel_stack;
        spinlock_release(&stack_alloc_lock);
        return (void *)(stack_addr + PAGE_SIZE);
    } else {
        // If there are no free allocated stacks, allocate a new one
        // We increment `last_kernel_stack` by two pages so that every pair of stacks is separated by an unallocated guard page.
        // This is done to prevent overwriting other stacks if a stack overflow happens.
        u64 stack_addr = last_kernel_stack + 2 * PAGE_SIZE;
        // Make sure not to allocate stacks past the limit
        if (stack_addr >= KERNEL_STACK_AREA_END) {
            spinlock_release(&stack_alloc_lock);
            return NULL;
        }
        if (!map_page(stack_addr, false, true, true, false)) {
            spinlock_release(&stack_alloc_lock);
            return NULL;
        }
        last_kernel_stack = stack_addr;
        spinlock_release(&stack_alloc_lock);
        return (void *)(stack_addr + PAGE_SIZE);
    }
}

// Free a stack
void stack_free(void *stack) {
    u64 stack_addr = (u64)stack - PAGE_SIZE;
    spinlock_acquire(&stack_alloc_lock);
    *(u64 *)stack_addr = first_free_kernel_stack;
    first_free_kernel_stack = stack_addr;
    spinlock_release(&stack_alloc_lock);
}