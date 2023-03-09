#include "types.h"
#include "page.h"

#include "spinlock.h"
#include "string.h"

#define MEMORY_RANGE_TYPE_USABLE 1

#define MEMORY_RANGE_ACPI_ATTR_VALID (1 << 0)
#define MEMORY_RANGE_ACPI_ATTR_NONVOLATILE (1 << 1)

#define PAGE_STACK_PML4E 0x1FCull
#define PAGE_STACK_BOTTOM (u64 *)ASSEMBLE_ADDR_PML4E(PAGE_STACK_PML4E, 0)

static spinlock_t page_stack_lock = SPINLOCK_FREE;

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

// Set up the indentity mapping within PML4E number IDENTITY_MAPPING_PML4E
bool identity_mapping_init(void) {
    // Set the PML4E for identity mapping
    u64 pml4e_page = page_alloc();
    if (pml4e_page == 0)
        return false;
    *PML4E_PTR(ASSEMBLE_ADDR_PML4E(IDENTITY_MAPPING_PML4E, 0)) = pml4e_page | PAGE_WRITE | PAGE_PRESENT;
    // Set each PDPTE to a new page
    for (size_t i = 0; i < 0x200; i++) {
        u64 pdpte_page = page_alloc();
        if (pdpte_page == 0)
            return false;
        *PDPTE_PTR(ASSEMBLE_ADDR_PDPTE(IDENTITY_MAPPING_PML4E, i, 0)) = pdpte_page | PAGE_WRITE | PAGE_PRESENT;
        // Within the PD, set each entry to map a large page
        for (size_t j = 0; j < 0x200; j++) {
            u64 phys_addr = ASSEMBLE_ADDR_PDE(0, i, j, 0);
            *PDE_PTR(ASSEMBLE_ADDR_PDE(IDENTITY_MAPPING_PML4E, i, j, 0)) = phys_addr | PAGE_NX | PAGE_GLOBAL | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
        }
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
        *entry = (page & PAGE_MASK)
            | (execute ? 0 : PAGE_NX)
            | (global ? PAGE_GLOBAL : 0)
            | (user ? PAGE_USER : 0)
            | (write ? PAGE_WRITE : 0)
            | PAGE_PRESENT;
        if (clear)
            memset(DEREF_ENTRY_PTR(entry), 0, PAGE_SIZE);
    }
    return true;
}

// Maps the page containing a given address, allocating any necessary page tables along the way.
// If the page or any page tables containing it are already allocated, they are not modified.
// Returns true on success, false on failure.
bool map_page(u64 addr, bool user, bool global, bool write, bool execute) {
    if (!ensure_page_map_entry_filled(PML4E_PTR(addr), user, global, true, true, true))
        return false;
    if (!ensure_page_map_entry_filled(PDPTE_PTR(addr), user, global, true, true, true))
        return false;
    if (!ensure_page_map_entry_filled(PDE_PTR(addr), user, global, true, true, true))
        return false;
    if (!ensure_page_map_entry_filled(PTE_PTR(addr), user, global, write, execute, false))
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

// Removes the identity mapping created by the bootloader.
// Should be called after the contents of low memory are no longer needed.
void remove_identity_mapping(void) {
    *PML4E_PTR(0) = 0;
    asm volatile ("mov rax, cr3; mov cr3, rax");
}
