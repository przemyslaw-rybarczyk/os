#pragma once

#include "types.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_WRITE (1ull << 1)
#define PAGE_USER (1ull << 2)
#define PAGE_LARGE (1ull << 7)
#define PAGE_GLOBAL (1ull << 8)
#define PAGE_NX (1ull << 63)

#define PAGE_MASK 0x00FFFFFFFFFFF000ull

#define PAGE_BITS 12
#define LARGE_PAGE_BITS 21
#define PT_BITS 21
#define PD_BITS 30
#define PDPT_BITS 39

#define PAGE_SIZE (1ull << PAGE_BITS)
#define LARGE_PAGE_SIZE (1ull << LARGE_PAGE_BITS)
#define PT_SIZE (1ull << PT_BITS)
#define PD_SIZE (1ull << PD_BITS)
#define PDPT_SIZE (1ull << PDPT_BITS)

// Takes an address and fills its first 16 bits with a sign extension of the lower 48 bits
#define SIGN_EXTEND_ADDR(x) (((((x) >> 47) & 1) ? 0xFFFF000000000000ull : 0) | (x & 0x0000FFFFFFFFFFFFull))

// These macros can be used to assemble addresses from their component parts - indices of page tables within other page tables,
// and the offset of the address relative to the start of the page.
// The offset can be larger than required, in which case it is truncated.
#define ASSEMBLE_ADDR(pml4e, pdpte, pde, pte, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(pdpte) << 30) | ((u64)(pde) << 21) | ((u64)(pte) << 12) | ((u64)(i) & 0x0000000000000FFFull)))
#define ASSEMBLE_ADDR_PDE(pml4e, pdpte, pde, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(pdpte) << 30) | ((u64)(pde) << 21) | ((u64)(i) & 0x00000000001FFFFFull)))
#define ASSEMBLE_ADDR_PDPTE(pml4e, pdpte, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(pdpte) << 30) | ((u64)(i) & 0x000000003FFFFFFFull)))
#define ASSEMBLE_ADDR_PML4E(pml4e, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(i) & 0x0000007FFFFFFFFFull)))

#define ADDR_PML4E(x) (((u64)(x) >> PDPT_BITS) & 0x1FF)
#define ADDR_PDPTE(x) (((u64)(x) >> PD_BITS) & 0x1FF)
#define ADDR_PDE(x) (((u64)(x) >> PT_BITS) & 0x1FF)
#define ADDR_PTE(x) (((u64)(x) >> PAGE_BITS) & 0x1FF)

static inline u64 get_pml4(void) {
    u64 pml4;
    asm ("mov %0, cr3" : "=r"(pml4));
    return pml4;
}

#define IDENTITY_MAPPING_PML4E 0x102ull
#define IDENTITY_MAPPING_SIZE PDPT_SIZE

// Used to access physical memory directly
// x must be less than IDENTITY_MAPPING_SIZE.
#define PHYS_ADDR(x) (ASSEMBLE_ADDR_PML4E(IDENTITY_MAPPING_PML4E, 0) + (x))

// Largest address accessible to userspace
#define USER_MAX_ADDR 0x00007FFFFFFFFFFF

bool page_alloc_init(void);
u64 page_alloc(void);
u64 page_alloc_clear(void);
void page_free(u64 page);
u64 get_free_memory_size(void);
bool map_page(u64 addr, bool user, bool global, bool write, bool execute);
bool map_pages(u64 start, u64 end, bool user, bool global, bool write, bool execute);
