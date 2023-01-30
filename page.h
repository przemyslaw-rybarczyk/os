#pragma once

#include "types.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_WRITE (1ull << 1)
#define PAGE_LARGE (1ull << 7)
#define PAGE_GLOBAL (1ull << 8)
#define PAGE_NX (1ull << 63)

#define SIGN_EXTEND_PML4E(pml4e) (((pml4e) >> 8) * (0xFFFFull << 9) + (pml4e))

// These macros can be used to assemble addresses from their component parts - indices of page tables within other page tables,
// and the offset of the address relative to the start of the page.
// The offset can be larger than required, in which case it is truncated.
#define ASSEMBLE_ADDR(pml4e, pdpte, pde, pte, i) ((SIGN_EXTEND_PML4E((pml4e)) << 39) | ((pdpte) << 30) | ((pde) << 21) | ((pte) << 12) | ((i) & 0x0000000000000FF8ull))
#define ASSEMBLE_ADDR_PDE(pml4e, pdpte, pde, i) ((SIGN_EXTEND_PML4E((pml4e)) << 39) | ((pdpte) << 30) | ((pde) << 21) | ((i) & 0x00000000001FFFF8ull))
#define ASSEMBLE_ADDR_PDPTE(pml4e, pdpte, i) ((SIGN_EXTEND_PML4E((pml4e)) << 39) | ((pdpte) << 30) | ((i) & 0x000000003FFFFFF8ull))
#define ASSEMBLE_ADDR_PML4E(pml4e, i) ((SIGN_EXTEND_PML4E((pml4e)) << 39) | ((i) & 0x0000007FFFFFFFF8ull))

#define RECURSIVE_PLM4E 0x100ull

// These macros provide pointers that can be used to access page map entries of mapping a given virtual address.
// They make use of the PML4E number RECURSIVE_PLM4E being mapped to the PML4.
#define PTE_PTR(x) ((u64 *)ASSEMBLE_ADDR_PML4E(RECURSIVE_PLM4E, (x) >> 9))
#define PDE_PTR(x) ((u64 *)ASSEMBLE_ADDR_PDPTE(RECURSIVE_PLM4E, RECURSIVE_PLM4E, (x) >> 18))
#define PDPTE_PTR(x) ((u64 *)ASSEMBLE_ADDR_PDE(RECURSIVE_PLM4E, RECURSIVE_PLM4E, RECURSIVE_PLM4E, (x) >> 27))
#define PML4E_PTR(x) ((u64 *)ASSEMBLE_ADDR(RECURSIVE_PLM4E, RECURSIVE_PLM4E, RECURSIVE_PLM4E, RECURSIVE_PLM4E, (x) >> 36))
