#pragma once

#include "types.h"

#define PAGE_PRESENT (1ull << 0)
#define PAGE_WRITE (1ull << 1)
#define PAGE_LARGE (1ull << 7)
#define PAGE_GLOBAL (1ull << 8)
#define PAGE_NX (1ull << 63)

#define RECURSIVE_PLM4E 0x100ull

// These macros provide pointers that can be used to access page map entries of mapping a given virtual address.
// They make use of the PML4E number RECURSIVE_PLM4E being mapped to the PML4.
#define PTE_PTR(x) ((u64 *)((0xFFFFull << 48) | (RECURSIVE_PLM4E << 39) | ((x >> 9) & 0x0000007FFFFFFFF8ull)))
#define PDE_PTR(x) ((u64 *)((0xFFFFull << 48) | (RECURSIVE_PLM4E << 39) | (RECURSIVE_PLM4E << 30) | ((x >> 18) & 0x000000003FFFFFF8ull)))
#define PDPTE_PTR(x) ((u64 *)((0xFFFFull << 48) | (RECURSIVE_PLM4E << 39) | (RECURSIVE_PLM4E << 30) | (RECURSIVE_PLM4E << 21) | ((x >> 27) & 0x00000000001FFFF8ull)))
#define PML4E_PTR(x) ((u64 *)((0xFFFFull << 48) | (RECURSIVE_PLM4E << 39) | (RECURSIVE_PLM4E << 30) | (RECURSIVE_PLM4E << 21) | (RECURSIVE_PLM4E << 12) ((x >> 27) & 0x0000000000000FF8ull)))
