#ifdef _KERNEL

#include "types.h"
#include "alloc.h"
#include "interrupt.h"
#include "page.h"
#include "string.h"
#include "spinlock.h"

#define HEAP_START ASSEMBLE_ADDR_PML4E(UINT64_C(0x100), 0)
#define HEAP_END_MAX ASSEMBLE_ADDR_PML4E(UINT64_C(0x101), 0)

#else

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zr/syscalls.h>
#include <zr/types.h>

#define HEAP_START UINT64_C(0x0000008000000000)
#define HEAP_END_MAX UINT64_C(0x0000010000000000)

static void panic(const char *str) {
    fputs("Memory allocator error: ", stderr);
    fputs(str, stderr);
    process_exit();
}

#endif

#define MALLOC_ALIGNMENT 16
#define INIT_HEAP_SIZE (UINT64_C(1) << 20)
#define MIN_HEAP_EXTEND_SIZE (UINT64_C(1) << 20)

#ifndef _KERNEL
#define PAGE_SIZE (UINT64_C(1) << 12)
#endif

static u64 heap_end = HEAP_START;

// Extend the heap by at least `increment` bytes.
// Returns true on success, false on failure.
static err_t heap_extend(size_t increment) {
    err_t err;
    // Round up the increment to page size
    increment = (increment + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
    // Check the increment won't increase heap size past the limit
    if (heap_end + increment > HEAP_END_MAX || heap_end + increment < heap_end)
#ifdef _KERNEL
        return ERR_KERNEL_NO_MEMORY;
#else
        return ERR_NO_MEMORY;
#endif
    // Allocate the pages needed to extend the heap
#ifdef _KERNEL
    err = map_kernel_pages(heap_end, increment, true, false);
#else
    err = map_pages(heap_end, increment, MAP_PAGES_WRITE);
#endif
    if (err)
        return err;
    heap_end += increment;
    return 0;
}

// The heap is split into consecutive regions, each one starting with a header.
// The region header contains a field determining whether the region is allocated or not,
// and pointers to the immediatelly preceding and following region.
// The `allocated` field is a u64 - only two values are valid and any other value indicates
// that the heap has been corrupted.
// Through the pointers, all regions form a doubly linked list.
// Since all regions are placed consecutively, a region's size can be calculated by simply subtracting
// its address from the address of the next region.
// A special case is the dummy region, whose header is placed at the end of the allocated part of the heap.
// The dummy region is placed in the linked list between the last and first region, making the list circular.
// This arrangement simplifies traversal and modification of the list.
// It also makes it possible to correctly calculate the size of the last non-dummy region.
// The dummy region header is followed by the free part at the end of the heap.

// For allocated regions, the header is followed by the actual data.
// For free regions, the header is extended with two pointers.
// They collect all free regions of memory into a doubly linked list.
// Same as with the first list, this list is circular and includes the dummy region.
// Unlike it though, the regions are not ordered in any way.

#define REGION_ALLOCATED UINT64_C(0x391DC2962365553E)
#define REGION_FREE UINT64_C(0xEF9AC4499FB1083A)

typedef struct MemoryRegion {
    u64 allocated;
    struct MemoryRegion *prev_region;
    struct MemoryRegion *next_region;
} __attribute__((aligned(MALLOC_ALIGNMENT))) MemoryRegion;

typedef struct FreeMemoryRegion {
    struct MemoryRegion header;
    struct FreeMemoryRegion *prev_free_region;
    struct FreeMemoryRegion *next_free_region;
} __attribute__((aligned(MALLOC_ALIGNMENT))) FreeMemoryRegion;

typedef struct AllocatedMemoryRegion {
    struct MemoryRegion header;
    char data[];
} AllocatedMemoryRegion;

static FreeMemoryRegion *dummy_region;

#ifdef _KERNEL
static spinlock_t alloc_lock;
#define alloc_lock_acquire() (spinlock_acquire(&alloc_lock))
#define alloc_lock_release() (spinlock_release(&alloc_lock))
#else
#define alloc_lock_acquire()
#define alloc_lock_release()
#endif

err_t _alloc_init(void) {
    err_t err;
    // Allocate the initlial heap
    err = heap_extend(INIT_HEAP_SIZE);
    if (err)
        return err;
    // Create the first region and the dummy region and use them to form the linked lists
    dummy_region = (FreeMemoryRegion *)HEAP_START;
    *dummy_region = (FreeMemoryRegion){{REGION_FREE, (MemoryRegion *)dummy_region, (MemoryRegion *)dummy_region}, dummy_region, dummy_region};
    return 0;
}

static void insert_into_region_list(MemoryRegion *region, MemoryRegion *prev) {
    region->next_region = prev->next_region;
    prev->next_region->prev_region = region;
    region->prev_region = prev;
    prev->next_region = region;
}

static void remove_from_region_list(MemoryRegion *region) {
    region->prev_region->next_region = region->next_region;
    region->next_region->prev_region = region->prev_region;
}

static void insert_into_free_region_list(FreeMemoryRegion *region) {
    region->next_free_region = dummy_region->next_free_region;
    dummy_region->next_free_region->prev_free_region = region;
    region->prev_free_region = dummy_region;
    dummy_region->next_free_region = region;
}

static void remove_from_free_region_list(FreeMemoryRegion *region) {
    region->prev_free_region->next_free_region = region->next_free_region;
    region->next_free_region->prev_free_region = region->prev_free_region;
}

static size_t region_size(MemoryRegion *region) {
    return (char *)region->next_region - (char *)region - sizeof(MemoryRegion);
}

static size_t dummy_region_size(void) {
    return (char *)heap_end - (char *)dummy_region - sizeof(MemoryRegion);
}

// Allocates the given amount of memory within the specified region.
// If there is enough space left over, a new region will be created from it.
// No bound check of any kind is performed.
static void *allocate_in_region(size_t n, FreeMemoryRegion *region) {
    // If there is enough space left to fit another unallocated region after the allocation, create one
    if (region_size((MemoryRegion *)region) >= n + sizeof(FreeMemoryRegion)) {
        FreeMemoryRegion *new_region = (FreeMemoryRegion *)((char *)region + sizeof(MemoryRegion) + n);
        new_region->header.allocated = REGION_FREE;
        insert_into_region_list((MemoryRegion *)new_region, (MemoryRegion *)region);
        insert_into_free_region_list(new_region);
    }
    // Mark the region as allocated and return the data
    region->header.allocated = REGION_ALLOCATED;
    remove_from_free_region_list(region);
    return (void *)&((AllocatedMemoryRegion *)region)->data;
}

// Allocates the given amount of memory in the final free part of the heap (after the dummy region header).
// The dummy region header is updated appropriately. If there isn't enough space, tries to extend the heap.
static void *allocate_at_end(size_t n) {
    // If there isn't enough space at the end of the heap, extend it
    if (n + sizeof(FreeMemoryRegion) > dummy_region_size()) {
        size_t heap_extend_size = n + sizeof(FreeMemoryRegion) - dummy_region_size();
        if (heap_extend_size < MIN_HEAP_EXTEND_SIZE)
            heap_extend_size = MIN_HEAP_EXTEND_SIZE;
        if (heap_extend(heap_extend_size) != 0)
            return NULL;
    }
    // Move the dummy region to after the allocation, turning the old dummy region into a regular region
    // large enough to fit the allocation
    FreeMemoryRegion *region = dummy_region;
    FreeMemoryRegion *new_dummy_region = (FreeMemoryRegion *)((char *)region + n + sizeof(MemoryRegion));
    new_dummy_region->header.allocated = REGION_FREE;
    insert_into_region_list((MemoryRegion *)new_dummy_region, (MemoryRegion *)region);
    insert_into_free_region_list(new_dummy_region);
    dummy_region = new_dummy_region;
    // Mark the new region as allocated and return the data
    region->header.allocated = REGION_ALLOCATED;
    remove_from_free_region_list(region);
    return (void *)(&((AllocatedMemoryRegion *)region)->data);
}

void *malloc(size_t n) {
    if (n == 0)
        return NULL;
    alloc_lock_acquire();
    // Round allocation size up to multiple of alignment
    n = ((n + MALLOC_ALIGNMENT - 1) / MALLOC_ALIGNMENT) * MALLOC_ALIGNMENT;
    // Make sure allocation is large enough to fit a free region header when it's freed
    if (n < sizeof(FreeMemoryRegion) - sizeof(MemoryRegion))
        n = sizeof(FreeMemoryRegion) - sizeof(MemoryRegion);
    // Search for the smallest free region that can fit the allocation
    if (dummy_region->header.allocated != REGION_FREE)
        panic("Heap corruption detected");
    FreeMemoryRegion *best_fit_region = NULL;
    size_t best_fit_size = SIZE_MAX;
    for (FreeMemoryRegion *region = dummy_region->next_free_region; region != dummy_region; region = region->next_free_region) {
        if (region->header.allocated != REGION_FREE)
            panic("Heap corruption detected");
        size_t size = region_size((MemoryRegion *)region);
        if (size >= n && size < best_fit_size) {
            best_fit_region = region;
            best_fit_size = size;
        }
    }
    if (best_fit_region != NULL) {
        void *p = allocate_in_region(n, best_fit_region);
        alloc_lock_release();
        return p;
    }
    // If we didn't find a free region large enough, allocate at the end of the heap
    void *p = allocate_at_end(n);
    alloc_lock_release();
    return p;
}

void free(void *p) {
    if (p == NULL)
        return;
    alloc_lock_acquire();
    FreeMemoryRegion *region = (FreeMemoryRegion *)((char *)p - sizeof(MemoryRegion));
    // Mark the region as free and insert it into the free region list
    if (region->header.allocated == REGION_FREE)
        panic("Double free");
    if (region->header.allocated != REGION_ALLOCATED)
        panic("Heap corruption detected");
    region->header.allocated = REGION_FREE;
    insert_into_free_region_list(region);
    // If the next region is free, coalesce with it
    if (region->header.next_region->allocated == REGION_FREE) {
        // If we're coalescing with the dummy region, update the dummy region
        if ((FreeMemoryRegion *)region->header.next_region == dummy_region)
            dummy_region = region;
        remove_from_free_region_list((FreeMemoryRegion *)region->header.next_region);
        remove_from_region_list(region->header.next_region);
    } else if (region->header.next_region->allocated != REGION_ALLOCATED) {
        panic("Heap corruption detected");
    }
    // If the previous region is free, coalesce with it
    if (region->header.prev_region->allocated == REGION_FREE) {
        // If this region is the dummy region now, move it backwards
        if (dummy_region == region)
            dummy_region = (FreeMemoryRegion *)region->header.prev_region;
        remove_from_free_region_list(region);
        remove_from_region_list((MemoryRegion *)region);
    } else if (region->header.prev_region->allocated != REGION_ALLOCATED) {
        panic("Heap corruption detected");
    }
    alloc_lock_release();
}

void *realloc(void *p, size_t n) {
    if (p == NULL || n == 0)
        return NULL;
    MemoryRegion *region = (MemoryRegion *)((char *)p - sizeof(MemoryRegion));
    // Allocate a new memory area
    void *np = malloc(n);
    if (np == NULL)
        return NULL;
    // Copy the memory
    size_t bytes_to_copy = region_size(region);
    if (n < bytes_to_copy)
        bytes_to_copy = n;
    memcpy(np, p, bytes_to_copy);
    // Free the old memory area
    free(p);
    return np;
}

#ifndef _KERNEL

void *calloc(size_t n, size_t size) {
    if (__builtin_umull_overflow(n, size, &n))
        return NULL;
    void *p = malloc(n);
    if (p == NULL)
        return NULL;
    memset(p, 0, n);
    return p;
}

#endif
