#include "types.h"
#include "page.h"

#include "framebuffer.h"
#include "spinlock.h"
#include "string.h"

#define MEMORY_RANGE_TYPE_USABLE 1

#define MEMORY_RANGE_ACPI_ATTR_VALID (1 << 0)
#define MEMORY_RANGE_ACPI_ATTR_NONVOLATILE (1 << 1)

#define PAGE_STACK_PML4E 0x1FCull
#define PAGE_STACK_BOTTOM (u64 *)ASSEMBLE_ADDR_PML4E(PAGE_STACK_PML4E, 0)

#define ID_MAP_INIT_AREA ASSEMBLE_ADDR_PDPTE(0x1FD, 0x002, 0)

static spinlock_t page_stack_lock;

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
extern u64 pdpt_page_stack[0x200];
extern u64 pt_id_map_init[0x200];

bool page_alloc_init(void) {
    // Number of pages allocated in the identity mapping initialization area
    size_t filled_id_map_pages = 0;
    // The virtual address of the top of the page stack
    u64 *pml4 = (u64 *)get_pml4();
    u64 *page_stack_top_pdpt = pdpt_page_stack;
    u64 *page_stack_top_pd = NULL;
    u64 *page_stack_top_pt = NULL;
    // Iterate over the memory ranges gathered by the bootloader
    for (u16 i = 0; i < memory_ranges_length / sizeof(MemoryRange); i++) {
        // If the memory type or ACPI attributes don't mark the memory range as valid, skip it
        if (memory_ranges[i].type != MEMORY_RANGE_TYPE_USABLE)
            continue;
        if ((memory_ranges[i].acpi_attrs & (MEMORY_RANGE_ACPI_ATTR_VALID | MEMORY_RANGE_ACPI_ATTR_NONVOLATILE)) != (MEMORY_RANGE_ACPI_ATTR_VALID))
            continue;
        u64 page_start = (((memory_ranges[i].start - 1) >> PAGE_BITS) + 1) << PAGE_BITS;
        u64 page_end = ((memory_ranges[i].start + memory_ranges[i].length) >> PAGE_BITS) << PAGE_BITS;
        // Add each page in the range to the page stack
        for (u64 page = page_start; page < page_end; page += PAGE_SIZE) {
            // Discard low memory pages, as many of them are used by the bootloader
            if (page < (1ull << 20))
                continue;
            // Discard pages that would go outside the identity mapping
            if (page >= IDENTITY_MAPPING_SIZE)
                continue;
            // Fill part of the identity mapping
            // The identity mapping page map consists of 0x200 PDs, each one mapping 0x200 large pages.
            // We use the first 0x200 pages we find as PDs for this mapping.
            // We first map them as regular pages so that we fill them with PD entries for the large pages.
            if (filled_id_map_pages < 0x200) {
                // Map the page in the initialization area
                pt_id_map_init[filled_id_map_pages] = page | PAGE_WRITE | PAGE_PRESENT;
                filled_id_map_pages++;
                // Once we got all the pages we need, we fill them with PD entries and map them as PDs.
                if (filled_id_map_pages == 0x200) {
                    for (size_t i = 0; i < 0x200 * 0x200; i++)
                        *((u64 *)ID_MAP_INIT_AREA + i) = (i * LARGE_PAGE_SIZE) | PAGE_NX | PAGE_GLOBAL | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
                    pml4[IDENTITY_MAPPING_PML4E] = (u64)pt_id_map_init | PAGE_WRITE | PAGE_PRESENT;
                }
                continue;
            }
            // If we go exhaust all space in the PDPT, we end the loop prematurely.
            // This should never happen.
            if (page_stack_top >= PAGE_STACK_BOTTOM + PDPT_SIZE / sizeof(u64))
                return true;
            // If we reach the end of the mapped part of the stack,
            // we use the current page to extend the mapping.
            // Otherwise, we just push the page on top of the stack.
            if ((u64)page_stack_top % PD_SIZE == 0 && page_stack_top_pdpt[ADDR_PDPTE(page_stack_top)] == 0) {
                memset((void *)PHYS_ADDR(page), 0, PAGE_SIZE);
                page_stack_top_pdpt[ADDR_PDPTE(page_stack_top)] = page | PAGE_WRITE | PAGE_PRESENT;
                page_stack_top_pd = (u64 *)PHYS_ADDR(page);
            } else if ((u64)page_stack_top % PT_SIZE == 0 && page_stack_top_pd[ADDR_PDE(page_stack_top)] == 0) {
                memset((void *)PHYS_ADDR(page), 0, PAGE_SIZE);
                page_stack_top_pd[ADDR_PDE(page_stack_top)] = page | PAGE_WRITE | PAGE_PRESENT;
                page_stack_top_pt = (u64 *)PHYS_ADDR(page);
            } else if ((u64)page_stack_top % PAGE_SIZE == 0 && page_stack_top_pt[ADDR_PTE(page_stack_top)] == 0) {
                memset((void *)PHYS_ADDR(page), 0, PAGE_SIZE);
                page_stack_top_pt[ADDR_PTE(page_stack_top)] = page | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
            } else {
                *page_stack_top = page;
                page_stack_top++;
            }
        }
    }
    // If we didn't find enough pages to create the identity mapping, the initialization fails
    if (filled_id_map_pages < 0x200) {
        return false;
    }
    return true;
}

// Allocates a new page and returns its physical address.
// Returns 0 on failure.
// The page is not cleared.
u64 page_alloc(void) {
    spinlock_acquire(&page_stack_lock);
    if (page_stack_top == PAGE_STACK_BOTTOM) {
        spinlock_release(&page_stack_lock);
        return 0;
    }
    page_stack_top--;
    u64 page = *page_stack_top;
    spinlock_release(&page_stack_lock);
    return page;
}

// Allocates a new page, clears it, and returns its physical address.
// Returns 0 on failure.
u64 page_alloc_clear(void) {
    u64 page = page_alloc();
    if (page == 0)
        return 0;
    memset((void *)PHYS_ADDR(page), 0, PAGE_SIZE);
    return page;
}

void page_free(u64 page) {
    spinlock_acquire(&page_stack_lock);
    *page_stack_top = page;
    page_stack_top++;
    spinlock_release(&page_stack_lock);
}

// Returns the number of free pages.
u64 get_free_memory_size(void) {
    return page_stack_top - PAGE_STACK_BOTTOM;
}

// If the page map entry is empty, fills it with a newly allocated page.
// If `clear` is true, the page is cleared.
// Returns true on success, false on failure.
static bool ensure_page_map_entry_filled(u64 *entry, bool user, bool global, bool write, bool execute, bool clear) {
    if (!(*entry & PAGE_PRESENT)) {
        u64 page = page_alloc();
        if (page == 0)
            return false;
        if (clear)
            memset((void *)PHYS_ADDR(page), 0, PAGE_SIZE);
        *entry = (page & PAGE_MASK)
            | (execute ? 0 : PAGE_NX)
            | (global ? PAGE_GLOBAL : 0)
            | (user ? PAGE_USER : 0)
            | (write ? PAGE_WRITE : 0)
            | PAGE_PRESENT;
    }
    return true;
}

// Maps the page containing a given address, allocating any necessary page tables along the way.
// If the page or any page tables containing it are already allocated, they are not modified.
// Returns true on success, false on failure.
bool map_page(u64 addr, bool user, bool global, bool write, bool execute) {
    u64 *pml4 = (u64 *)PHYS_ADDR(get_pml4());
    if (!ensure_page_map_entry_filled(&pml4[ADDR_PML4E(addr)], user, false, true, true, true))
        return false;
    u64 *pdpt = (u64 *)PHYS_ADDR(pml4[ADDR_PML4E(addr)] & PAGE_MASK);
    if (!ensure_page_map_entry_filled(&pdpt[ADDR_PDPTE(addr)], user, false, true, true, true))
        return false;
    u64 *pd = (u64 *)PHYS_ADDR(pdpt[ADDR_PDPTE(addr)] & PAGE_MASK);
    if (!ensure_page_map_entry_filled(&pd[ADDR_PDE(addr)], user, false, true, true, true))
        return false;
    u64 *pt = (u64 *)PHYS_ADDR(pd[ADDR_PDE(addr)] & PAGE_MASK);
    if (!ensure_page_map_entry_filled(&pt[ADDR_PTE(addr)], user, global, write, execute, false))
        return false;
    return true;
}

// The same as map_page(), but maps an entire range of addresses
bool map_pages(u64 start, u64 end, bool user, bool global, bool write, bool execute) {
    u64 start_page = start / PAGE_SIZE * PAGE_SIZE;
    u64 end_page = (end + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
    for (u64 page = start_page; page < end_page; page += PAGE_SIZE)
        if (!map_page(page, user, global, write, execute))
            return false;
    return true;
}
