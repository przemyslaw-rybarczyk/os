#pragma once

#include "types.h"
#include "error.h"

#define PAGE_PRESENT (UINT64_C(1) << 0)
#define PAGE_WRITE (UINT64_C(1) << 1)
#define PAGE_USER (UINT64_C(1) << 2)
#define PAGE_LARGE (UINT64_C(1) << 7)
#define PAGE_GLOBAL (UINT64_C(1) << 8)
#define PAGE_NX (UINT64_C(1) << 63)

#define PAGE_MASK UINT64_C(0x000FFFFFFFFFF000)

#define PAGE_BITS 12
#define LARGE_PAGE_BITS 21
#define PT_BITS 21
#define PD_BITS 30
#define PDPT_BITS 39
#define PML4_BITS 48
#define PAGE_MAP_LEVEL_BITS 9

#define PAGE_SIZE (UINT64_C(1) << PAGE_BITS)
#define LARGE_PAGE_SIZE (UINT64_C(1) << LARGE_PAGE_BITS)
#define PT_SIZE (UINT64_C(1) << PT_BITS)
#define PD_SIZE (UINT64_C(1) << PD_BITS)
#define PDPT_SIZE (UINT64_C(1) << PDPT_BITS)
#define PML4_SIZE (UINT64_C(1) << PML4_BITS)
#define PAGE_MAP_LEVEL_SIZE (UINT64_C(1) << PAGE_MAP_LEVEL_BITS)

// Takes an address and fills its first 16 bits with a sign extension of the lower 48 bits
#define SIGN_EXTEND_ADDR(x) (((((x) >> 47) & 1) ? UINT64_C(0xFFFF000000000000) : 0) | (x & UINT64_C(0x0000FFFFFFFFFFFF)))

// These macros can be used to assemble addresses from their component parts - indices of page tables within other page tables,
// and the offset of the address relative to the start of the page.
// The offset can be larger than required, in which case it is truncated.
#define ASSEMBLE_ADDR(pml4e, pdpte, pde, pte, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(pdpte) << 30) | ((u64)(pde) << 21) | ((u64)(pte) << 12) | ((u64)(i) & UINT64_C(0x0000000000000FFF))))
#define ASSEMBLE_ADDR_PDE(pml4e, pdpte, pde, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(pdpte) << 30) | ((u64)(pde) << 21) | ((u64)(i) & UINT64_C(0x00000000001FFFFF))))
#define ASSEMBLE_ADDR_PDPTE(pml4e, pdpte, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(pdpte) << 30) | ((u64)(i) & UINT64_C(0x000000003FFFFFFF))))
#define ASSEMBLE_ADDR_PML4E(pml4e, i) (SIGN_EXTEND_ADDR(((u64)(pml4e) << 39) | ((u64)(i) & UINT64_C(0x0000007FFFFFFFFF))))

#define ADDR_PML4E(x) (((u64)(x) >> PDPT_BITS) & 0x1FF)
#define ADDR_PDPTE(x) (((u64)(x) >> PD_BITS) & 0x1FF)
#define ADDR_PDE(x) (((u64)(x) >> PT_BITS) & 0x1FF)
#define ADDR_PTE(x) (((u64)(x) >> PAGE_BITS) & 0x1FF)

static inline u64 get_pml4(void) {
    u64 pml4;
    asm ("mov %0, cr3" : "=r"(pml4));
    return pml4;
}

#define IDENTITY_MAPPING_PML4E UINT64_C(0x102)
#define IDENTITY_MAPPING_SIZE PDPT_SIZE

// Used to access physical memory directly
// x must be less than IDENTITY_MAPPING_SIZE.
#define PHYS_ADDR(x) ((void *)(ASSEMBLE_ADDR_PML4E(IDENTITY_MAPPING_PML4E, 0) + (x)))

// Lowest address not accessible to userspace
#define USER_ADDR_UPPER_BOUND UINT64_C(0x0000800000000000)

// Lowest address used by kernel
#define KERNEL_ADDR_LOWER_BOUND UINT64_C(0xFFFF800000000000)

err_t page_alloc_init(void);
u64 page_alloc(void);
u64 page_alloc_clear(void);
void page_free(u64 page);
size_t get_free_memory_size(void);
err_t map_kernel_pages(u64 start, u64 length, bool write, bool execute);
err_t map_user_pages(u64 start, u64 length, bool write, bool execute);
void page_map_free_contents(u64 page_map_addr);
err_t verify_user_buffer(const void *start, size_t length);
