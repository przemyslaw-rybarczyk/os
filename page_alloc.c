#include "types.h"
#include "page_alloc.h"

#include "page.h"
#include "string.h"

#define MEMORY_RANGE_TYPE_USABLE 1

#define MEMORY_RANGE_ACPI_ATTR_VALID (1 << 0)
#define MEMORY_RANGE_ACPI_ATTR_NONVOLATILE (1 << 1)

#define PAGE_STACK_PML4E 0x1FCull
#define PAGE_STACK_BOTTOM (u64 *)ASSEMBLE_ADDR_PML4E(PAGE_STACK_PML4E, 0)

// Free pages are stored in a stack.
static u64 *page_stack_top = PAGE_STACK_BOTTOM;

typedef struct MemoryRange {
    u64 start;
    u64 length;
    u32 type;
    u32 acpi_attrs;
} __attribute__((packed)) MemoryRange;

extern MemoryRange memory_ranges[];
extern u16 memory_ranges_length;

void page_alloc_init(void) {
    // Iterate over the memory ranges gathered by the bootloader
    for (u16 i = 0; i < memory_ranges_length / sizeof(MemoryRange); i++) {
        // If the memory type or ACPI attributes don't mark the memory range as valid, skip it
        if (memory_ranges[i].type != MEMORY_RANGE_TYPE_USABLE)
            continue;
        if ((memory_ranges[i].acpi_attrs & (MEMORY_RANGE_ACPI_ATTR_VALID | MEMORY_RANGE_ACPI_ATTR_NONVOLATILE)) != (MEMORY_RANGE_ACPI_ATTR_VALID | MEMORY_RANGE_ACPI_ATTR_NONVOLATILE))
            continue;
        u64 page_start = (((memory_ranges[i].start - 1) >> PAGE_BITS) + 1) << PAGE_BITS;
        u64 page_end = ((memory_ranges[i].start + memory_ranges[i].length) >> PAGE_BITS) << PAGE_BITS;
        // Add each page in the range to the page stack
        for (u64 page = page_start; page < page_end; page += PAGE_SIZE) {
            // Discard low memory pages
            if (page < (1ull << 20))
                continue;
            // If we go exhaust all space in the PDPT, we end the loop prematurely.
            // This should never happen.
            if (page_stack_top >= PAGE_STACK_BOTTOM + PDPT_SIZE / sizeof(u64))
                return;
            // If we reach the end of the mapped part of the stack,
            // we use the current page to extend the mapping.
            // Otherwise, we just push the page on top of the stack.
            if ((u64)page_stack_top % PD_SIZE == 0 && *PDPTE_PTR(page_stack_top) == 0) {
                *PDPTE_PTR(page_stack_top) = page | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
                memset(PDE_PTR(page_stack_top), 0, PAGE_SIZE);
            } else if ((u64)page_stack_top % PT_SIZE == 0 && *PDE_PTR(page_stack_top) == 0) {
                *PDE_PTR(page_stack_top) = page | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
                memset(PTE_PTR(page_stack_top), 0, PAGE_SIZE);
            } else if ((u64)page_stack_top % PAGE_SIZE == 0 && *PTE_PTR(page_stack_top) == 0) {
                *PTE_PTR(page_stack_top) = page | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
                memset(page_stack_top, 0, PAGE_SIZE);
            } else {
                *page_stack_top = page;
                page_stack_top++;
            }
        }
    }
}

// Allocates a new page and returns its physical address.
// Returns 0 on failure.
// The page is not cleared.
u64 page_alloc(void) {
    if (page_stack_top == PAGE_STACK_BOTTOM)
        return 0;
    page_stack_top--;
    return *page_stack_top;
}

void page_free(u64 page) {
    *page_stack_top = page;
    page_stack_top++;
}

// Returns the number of free pages.
u64 get_free_memory_size(void) {
    return page_stack_top - PAGE_STACK_BOTTOM;
}
