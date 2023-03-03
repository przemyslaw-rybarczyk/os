#pragma once

#include "types.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_WRITE (1ull << 1)
#define PAGE_USER (1ull << 2)
#define PAGE_LARGE (1ull << 7)
#define PAGE_GLOBAL (1ull << 8)
#define PAGE_NX (1ull << 63)

#define PAGE_MASK 0xFFFFFFFFFFFFF000ull

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

#define RECURSIVE_PLM4E 0x100ull

// These macros provide pointers that can be used to access page map entries of mapping a given virtual address.
// They make use of the PML4E number RECURSIVE_PLM4E being mapped to the PML4.
#define PTE_PTR(x) ((u64 *)(ASSEMBLE_ADDR_PML4E(RECURSIVE_PLM4E, (u64)(x) >> 9) & ~7ull))
#define PDE_PTR(x) ((u64 *)(ASSEMBLE_ADDR_PDPTE(RECURSIVE_PLM4E, RECURSIVE_PLM4E, (u64)(x) >> 18) & ~7ull))
#define PDPTE_PTR(x) ((u64 *)(ASSEMBLE_ADDR_PDE(RECURSIVE_PLM4E, RECURSIVE_PLM4E, RECURSIVE_PLM4E, (u64)(x) >> 27) & ~7ull))
#define PML4E_PTR(x) ((u64 *)(ASSEMBLE_ADDR(RECURSIVE_PLM4E, RECURSIVE_PLM4E, RECURSIVE_PLM4E, RECURSIVE_PLM4E, (u64)(x) >> 36) & ~7ull))

// Takes a pointer to a page map entry obtained through the recursive mapping,
// and returns a virtual pointer to the page it has in its address field.
#define DEREF_ENTRY_PTR(x) ((void *)SIGN_EXTEND_ADDR((u64)(x) << 9))

void page_alloc_init(void);
u64 page_alloc(void);
void page_free(u64 page);
u64 get_free_memory_size(void);
bool map_page(u64 addr, bool user, bool global, bool write, bool execute);
bool map_pages(u64 start, u64 end, bool user, bool global, bool write, bool execute);
void remove_identity_mapping(void);
