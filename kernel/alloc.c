#include "types.h"
#include "alloc.h"

#include "framebuffer.h"
#include "page.h"
#include "string.h"
#include "spinlock.h"

#define MALLOC_ALIGNMENT 8
#define INIT_HEAP_SIZE (1ull << 20)
#define MIN_HEAP_EXTEND_SIZE (1ull << 20)

#define KERNEL_HEAP_START ASSEMBLE_ADDR_PML4E(0x100ull, 0)
#define KERNEL_HEAP_END_MAX ASSEMBLE_ADDR_PML4E(0x101ull, 0)

static u64 kernel_heap_end = KERNEL_HEAP_START;

// Extend the kernel heap by at least `increment` bytes.
// Returns true on success, false on failure.
static err_t extend_kernel_heap(size_t increment) {
    err_t err;
    // Check the increment won't increase heap size past the limit
    if (kernel_heap_end + increment > KERNEL_HEAP_END_MAX || kernel_heap_end + increment < kernel_heap_end)
        return false;
    // Round up the increment to page size
    increment = (increment + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
    // Allocate the pages needed to extend the heap
    err = map_kernel_pages(kernel_heap_end, increment, true, false);
    if (err)
        return err;
    kernel_heap_end += increment;
    return 0;
}

// The heap is split into consecutive regions, each one starting with a header.
// The region header contains a flag determining whether the region is allocated or not,
// and pointers to the immediatelly preceding and following region.
// Through these pointers, all regions form a doubly linked list.
// Since all regions are placed consecutively, a region's size can be calculated by simply subtracting
// its address from the address of the next region.
// A special case is the dummy region, whose header is placed at the very end of the heap.
// The dummy region is placed in the linked list between the last and first region, making the list circular.
// This arrangement simplifies traversal and modification of the list.
// It also makes it possible to correctly calculate the size of the last non-dummy region.

// For allocated blocks, the header is followed by the actual data.
// For free blocks, the header is extended with two pointers.
// They collect all free regions of memory into a doubly linked list.
// Same as with the first list, this list is circular and includes the dummy region.
// Unlike it though, the regions are not ordered in any way.
// Als note that even though the dummy region contains the full free region header, it is marked as unallocated.
// This is to prevent it from being coalesced with adjacent free regions.

typedef struct MemoryRegion {
    bool allocated;
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

static spinlock_t alloc_lock;

err_t alloc_init(void) {
    err_t err;
    // Allocate the initlial heap
    err = extend_kernel_heap(INIT_HEAP_SIZE);
    if (err)
        return err;
    // Create the first region and the dummy region and use them to form the linked lists
    FreeMemoryRegion *first_region = (FreeMemoryRegion *)KERNEL_HEAP_START;
    dummy_region = (FreeMemoryRegion *)((char *)kernel_heap_end - sizeof(FreeMemoryRegion));
    *first_region = (FreeMemoryRegion){{false, (MemoryRegion *)dummy_region, (MemoryRegion *)dummy_region}, dummy_region, dummy_region};
    *dummy_region = (FreeMemoryRegion){{true, (MemoryRegion *)first_region, (MemoryRegion *)first_region}, first_region, first_region};
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

// Allocates the given amount of memory within the specified region.
// If there is enough space left over, a new region will be created from it.
// No bound check of any kind is performed.
static void *allocate_in_region(size_t n, FreeMemoryRegion *region) {
    // If there is enough space left to fit another unallocated region after the allocation, create one
    if (region_size((MemoryRegion *)region) >= n + sizeof(FreeMemoryRegion)) {
        FreeMemoryRegion *new_region = (FreeMemoryRegion *)((char *)region + sizeof(MemoryRegion) + n);
        new_region->header.allocated = false;
        insert_into_region_list((MemoryRegion *)new_region, (MemoryRegion *)region);
        insert_into_free_region_list(new_region);
    }
    // Mark the region as allocated and return the data
    region->header.allocated = true;
    remove_from_free_region_list(region);
    return (void *)&((AllocatedMemoryRegion *)region)->data;
}

void *malloc(size_t n) {
    if (n == 0)
        return NULL;
    spinlock_acquire(&alloc_lock);
    // Round allocation size up to multiple of alignment
    n = ((n + MALLOC_ALIGNMENT - 1) / MALLOC_ALIGNMENT) * MALLOC_ALIGNMENT;
    // Make sure allocation is large enough to fit a free region header when it's freed
    if (n < sizeof(FreeMemoryRegion) - sizeof(MemoryRegion))
        n = sizeof(FreeMemoryRegion) - sizeof(MemoryRegion);
    // Go through every free region until finding one that can fit the allocation
    for (FreeMemoryRegion *region = dummy_region->next_free_region; region != dummy_region; region = region->next_free_region) {
        if (region_size((MemoryRegion *)region) >= n) {
            void *ret = allocate_in_region(n, region);
            spinlock_release(&alloc_lock);
            return ret;
        }
    }
    // If we didn't find a free region, extend the heap and allocate a new one in the new space
    size_t heap_extend_size = n + sizeof(MemoryRegion);
    if (heap_extend_size < MIN_HEAP_EXTEND_SIZE)
        heap_extend_size = MIN_HEAP_EXTEND_SIZE;
    if (extend_kernel_heap(heap_extend_size) != 0) {
        spinlock_release(&alloc_lock);
        return NULL;
    }
    // Create a new dummy region at the end of the extended heap
    FreeMemoryRegion *new_dummy_region = (FreeMemoryRegion *)((char *)kernel_heap_end - sizeof(FreeMemoryRegion));
    new_dummy_region->header.allocated = true;
    insert_into_region_list((MemoryRegion *)new_dummy_region, (MemoryRegion *)dummy_region);
    insert_into_free_region_list(new_dummy_region);
    FreeMemoryRegion *old_dummy_region = dummy_region;
    dummy_region = new_dummy_region;
    // Turn the old dummy region into a regular free region and coalesce it with the preceding region if possible
    if (!old_dummy_region->header.prev_region->allocated) {
        remove_from_free_region_list(old_dummy_region);
        remove_from_region_list((MemoryRegion *)old_dummy_region);
    } else {
        old_dummy_region->header.allocated = false;
    }
    // Allocate the memory in the final region
    void *ret = allocate_in_region(n, (FreeMemoryRegion *)(dummy_region->header.prev_region));
    spinlock_release(&alloc_lock);
    return ret;
}

void free(void *p) {
    if (p == NULL)
        return;
    spinlock_acquire(&alloc_lock);
    FreeMemoryRegion *region = (FreeMemoryRegion *)((char *)p - sizeof(MemoryRegion));
    // If the next region is free, coalesce with it
    if (!region->header.next_region->allocated) {
        remove_from_free_region_list((FreeMemoryRegion *)region->header.next_region);
        remove_from_region_list(region->header.next_region);
    }
    // If the previous region is free, coalesce with it
    // Since this removes the freed region, we return from the function in this case.
    if (!region->header.prev_region->allocated) {
        remove_from_region_list((MemoryRegion *)region);
        spinlock_release(&alloc_lock);
        return;
    }
    // Mark the region as free
    region->header.allocated = false;
    // Place the region at the start of the free region list
    insert_into_free_region_list(region);
    spinlock_release(&alloc_lock);
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

void print_debug_heap_info(void) {
    spinlock_acquire(&alloc_lock);
    framebuffer_lock();
    print_string("Heap state:\n");
    print_string("Address            Size               Status Prev free region   Next free region\n");
    for (MemoryRegion *region = dummy_region->header.next_region;; region = region->next_region) {
        print_hex_u64((u64)region);
        print_char(' ');
        if (region != (MemoryRegion *)dummy_region)
            print_hex_u64(region_size(region));
        else
            print_string("                  ");
        print_char(' ');
        print_string(region->allocated ? "used" : "free");
        print_string("   ");
        if (!region->allocated || region == (MemoryRegion *)dummy_region) {
            print_hex_u64((u64)((FreeMemoryRegion *)region)->prev_free_region);
            print_char(' ');
            print_hex_u64((u64)((FreeMemoryRegion *)region)->next_free_region);
            print_char(' ');
        }
        if (region == (MemoryRegion *)dummy_region)
            print_string("[dummy] ");
        else if (region->next_region < region)
            print_string("[broken order] ");
        if (region->next_region->prev_region != region)
            print_string("[broken backlink] ");
        print_newline();
        if (region == (MemoryRegion *)dummy_region)
            break;
    }
    framebuffer_unlock();
    spinlock_release(&alloc_lock);
}
