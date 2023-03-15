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
extern u64 pdpt_page_stack[PAGE_MAP_LEVEL_SIZE];
extern u64 pt_id_map_init[PAGE_MAP_LEVEL_SIZE];

err_t page_alloc_init(void) {
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
            // The identity mapping page map consists of PAGE_MAP_LEVEL_SIZE PDs, each one mapping PAGE_MAP_LEVEL_SIZE large pages.
            // We use the first PAGE_MAP_LEVEL_SIZE pages we find as PDs for this mapping.
            // We first map them as regular pages so that we fill them with PD entries for the large pages.
            if (filled_id_map_pages < PAGE_MAP_LEVEL_SIZE) {
                // Map the page in the initialization area
                pt_id_map_init[filled_id_map_pages] = page | PAGE_WRITE | PAGE_PRESENT;
                filled_id_map_pages++;
                // Once we got all the pages we need, we fill them with PD entries and map them as PDs.
                if (filled_id_map_pages == PAGE_MAP_LEVEL_SIZE) {
                    for (size_t i = 0; i < PAGE_MAP_LEVEL_SIZE * PAGE_MAP_LEVEL_SIZE; i++)
                        *((u64 *)ID_MAP_INIT_AREA + i) = (i * LARGE_PAGE_SIZE) | PAGE_NX | PAGE_GLOBAL | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
                    pml4[IDENTITY_MAPPING_PML4E] = (u64)pt_id_map_init | PAGE_WRITE | PAGE_PRESENT;
                }
                continue;
            }
            // If we go exhaust all space in the PDPT, we end the loop prematurely.
            // This should never happen.
            if (page_stack_top >= PAGE_STACK_BOTTOM + PDPT_SIZE / sizeof(u64))
                return 0;
            // If we reach the end of the mapped part of the stack,
            // we use the current page to extend the mapping.
            // Otherwise, we just push the page on top of the stack.
            if ((u64)page_stack_top % PD_SIZE == 0 && page_stack_top_pdpt[ADDR_PDPTE(page_stack_top)] == 0) {
                memset(PHYS_ADDR(page), 0, PAGE_SIZE);
                page_stack_top_pdpt[ADDR_PDPTE(page_stack_top)] = page | PAGE_WRITE | PAGE_PRESENT;
                page_stack_top_pd = PHYS_ADDR(page);
            } else if ((u64)page_stack_top % PT_SIZE == 0 && page_stack_top_pd[ADDR_PDE(page_stack_top)] == 0) {
                memset(PHYS_ADDR(page), 0, PAGE_SIZE);
                page_stack_top_pd[ADDR_PDE(page_stack_top)] = page | PAGE_WRITE | PAGE_PRESENT;
                page_stack_top_pt = PHYS_ADDR(page);
            } else if ((u64)page_stack_top % PAGE_SIZE == 0 && page_stack_top_pt[ADDR_PTE(page_stack_top)] == 0) {
                memset(PHYS_ADDR(page), 0, PAGE_SIZE);
                page_stack_top_pt[ADDR_PTE(page_stack_top)] = page | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
            } else {
                *page_stack_top = page;
                page_stack_top++;
            }
        }
    }
    // If we didn't find enough pages to create the identity mapping, the initialization fails
    if (filled_id_map_pages < PAGE_MAP_LEVEL_SIZE)
        return ERR_NO_MEMORY;
    return 0;
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
    memset(PHYS_ADDR(page), 0, PAGE_SIZE);
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

// Used to protect access to the kernel page map
static spinlock_t kernel_page_lock;

static u64 get_mapping_start_index(u64 start, u64 page_map_start, u64 page_map_bits) {
    return start < page_map_start ? 0 : (start >> page_map_bits) % PAGE_MAP_LEVEL_SIZE;
}

static u64 get_mapping_end_index(u64 end, u64 page_map_start, u64 page_map_bits) {
    return end >= page_map_start + (PAGE_MAP_LEVEL_SIZE << page_map_bits) ? PAGE_MAP_LEVEL_SIZE - 1 : (end >> page_map_bits) % PAGE_MAP_LEVEL_SIZE;
}

// Free the entries mapping the range from `start` to `end` inclusive within a page map at address `page_map` mapping the range starting at `page_map_start` of length `1 << page_map_bits`
// Flags are ignored when unmapping entries, including the present flag.
// Assumes that the page map maps addresses for at least part of the range and that all addresses are truncated to 48 bits.
static void free_page_map_range(u64 start, u64 end, u64 *page_map, u64 page_map_start, u64 page_map_bits) {
    u64 mapping_start_index = get_mapping_start_index(start, page_map_start, page_map_bits);
    u64 mapping_end_index = get_mapping_end_index(end, page_map_start, page_map_bits);
    for (u64 i = mapping_start_index; i <= mapping_end_index; i++) {
        u64 next_page_map = page_map[i] & PAGE_MASK;
        if (page_map_bits > PAGE_BITS)
            free_page_map_range(start, end, PHYS_ADDR(next_page_map), page_map_start + (i << page_map_bits), page_map_bits - 9);
        page_free(next_page_map);
    }
}

// Fill the entries mapping the range from `start` to `end` inclusive within a page map at address `page_map` mapping the range starting at `page_map_start` of length `1 << page_map_bits`
// No flags are set, including the present flag. This prevents programs from accessing memory that would be unmapped later if an error occurs.
// Assumes that the page map maps addresses for at least part of the range and that all addresses are truncated to 48 bits.
// If an error occurs, all allocated pages are freed.
static err_t fill_page_map_range(u64 start, u64 end, u64 *page_map, u64 page_map_start, u64 page_map_bits) {
    err_t err;
    // Iterate over the relevant range of page map entries
    u64 mapping_start_index = get_mapping_start_index(start, page_map_start, page_map_bits);
    u64 mapping_end_index = get_mapping_end_index(end, page_map_start, page_map_bits);
    for (u64 i = mapping_start_index; i <= mapping_end_index; i++) {
        u64 *next_page_map;
        if (page_map[i] & PAGE_PRESENT) {
            // If the we're trying to map a page that's already mapped, return an error
            if (page_map_bits == PAGE_BITS) {
                err = ERR_PAGE_ALREADY_MAPPED;
                goto fail;
            }
            // If we're mapping a page map and it already exists, use it
            next_page_map = PHYS_ADDR(page_map[i] & PAGE_MASK);
        } else {
            // If there is no page present yet, allocate one
            u64 new_page_phys = page_map_bits > PAGE_BITS ? page_alloc_clear() : page_alloc();
            if (new_page_phys == 0) {
                err = ERR_NO_MEMORY;
                goto fail;
            }
            page_map[i] = new_page_phys;
            next_page_map = PHYS_ADDR(new_page_phys);
        }
        if (page_map_bits > PAGE_BITS) {
            // Recurse to map the lower level page maps
            err = fill_page_map_range(start, end, next_page_map, page_map_start + (i << page_map_bits), page_map_bits - 9);
            if (err)
                goto fail;
        }
        continue;
fail:
        // Free the previously allocated pages and return an error
        for (u64 j = mapping_start_index; j < i; j++)
            free_page_map_range(start, end, PHYS_ADDR(page_map[i] & PAGE_MASK), page_map_start + (i << page_map_bits), page_map_bits - 9);
        return err;
    }
    return 0;
}

// Enable the entries mapping the range from `start` to `end` inclusive within a page map at address `page_map` mapping the range starting at `page_map_start` of length `1 << page_map_bits`
// This sets the given flags for all mapped pages. Page map entries mapping multiple pages have their user, write, and present flags set.
// Assumes that the page map maps addresses for at least part of the range and that all addresses are truncated to 48 bits.
static void enable_page_map_range(u64 start, u64 end, u64 *page_map, u64 page_map_start, u64 page_map_bits, u64 flags) {
    u64 mapping_start_index = get_mapping_start_index(start, page_map_start, page_map_bits);
    u64 mapping_end_index = get_mapping_end_index(end, page_map_start, page_map_bits);
    for (u64 i = mapping_start_index; i <= mapping_end_index; i++) {
        if (page_map_bits > PAGE_BITS) {
            enable_page_map_range(start, end, PHYS_ADDR(page_map[i] & PAGE_MASK), page_map_start + (i << page_map_bits), page_map_bits - 9, flags);
            page_map[i] |= PAGE_USER | PAGE_WRITE | PAGE_PRESENT;
        } else {
            page_map[i] |= flags;
        }
    }
}

// Map the pages in the given range with the specified flags
// Assumes all addresses are truncated to 48 bits.
static err_t map_pages(u64 start, u64 length, u64 flags) {
    err_t err;
    if (start % PAGE_SIZE != 0 || length % PAGE_SIZE != 0)
        return ERR_OUT_OF_RANGE;
    if (start + length < start)
        return ERR_OUT_OF_RANGE;
    if (length == 0)
        return 0;
    err = fill_page_map_range(start, start + length - PAGE_SIZE, PHYS_ADDR(get_pml4()), 0, PDPT_BITS);
    if (err)
        return err;
    enable_page_map_range(start, start + length - PAGE_SIZE, PHYS_ADDR(get_pml4()), 0, PDPT_BITS, flags);
    return 0;
}

// Map the pages in the given range as kernel memory
err_t map_kernel_pages(u64 start, u64 length, bool write, bool execute) {
    if (start + length < KERNEL_MIN_ADDR)
        return ERR_OUT_OF_RANGE;
    spinlock_acquire(&kernel_page_lock);
    bool result = map_pages(start % PML4_SIZE, length, (execute ? 0 : PAGE_NX) | PAGE_GLOBAL | (write ? PAGE_WRITE : 0) | PAGE_PRESENT);
    spinlock_release(&kernel_page_lock);
    return result;
}

// Map the pages in the given range as userspace memory
err_t map_user_pages(u64 start, u64 length, bool write, bool execute) {
    if (start > USER_MAX_ADDR)
        return ERR_OUT_OF_RANGE;
    return map_pages(start % PML4_SIZE, length, (execute ? 0 : PAGE_NX) | PAGE_USER | (write ? PAGE_WRITE : 0) | PAGE_PRESENT);
}
