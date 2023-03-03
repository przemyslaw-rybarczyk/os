#include "types.h"
#include "acpi.h"

#include "string.h"
#include "page.h"

#define DEVICES_PML4E 0x1FDull
#define LAPIC_PDPTE 0x001ull
#define TEMP_PDPTE 0x1FEull
#define TEMP_AREA_RSDT 0
#define TEMP_AREA_MADT 3
#define TEMP_AREA_IOAPIC 6

#define MADT_LAPIC 0x00
#define MADT_IO_APIC 0x01
#define MADT_INT_SOURCE_OVERRIDE 0x02
#define MADT_LAPIC_ADDR_OVERRIDE 0x05

#define MADT_LAPIC_ENABLED (1 << 0)
#define MADT_LAPIC_ONLINE_CAPABLE (1 << 1)

#define MADT_INT_BUS_ISA 0
#define MADT_INT_POLARITY (3 << 0)
#define MADT_INT_POLARITY_LOW (3 << 0)
#define MADT_INT_TRIGGER (3 << 2)
#define MADT_INT_TRIGGER_LEVEL (3 << 2)

#define IOAPICVER 0x01
#define IOAPICVER_MAX_REDIR_MASK 0x00FF0000ul
#define IOAPICVER_MAX_REDIR_OFFSET 16

#define IOREDTBL 0x10
#define IOREDTBL_DESTINATION_ALL (0xFFull << 56)
#define IOREDTBL_MASKED (1ul << 16)
#define IOREDTBL_TRIGGER_LEVEL (1ul << 15)
#define IOREDTBL_POLARITY_LOW (1ul << 13)
#define IOREDTBL_DESTINATION_LOGICAL (1ul << 11)
#define IOREDTBL_DELIVERY_FIXED (0ul << 8)
#define IOREDTBL_DELIVERY_LOWEST_PRIORITY (1ul << 8)

#define ISA_INT_PIT 0
#define ISA_INT_KEYBOARD 1
#define ISA_INT_MOUSE 12

#define INT_VECTOR_PIT 0x20
#define INT_VECTOR_KEYBOARD 0x21
#define INT_VECTOR_MOUSE 0x22

#define CPU_NUM_MAX 256

u8 cpus[CPU_NUM_MAX];
size_t cpu_num;
void *lapic;

static const u8 rsdp_signature[] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};

typedef struct RSDP {
    u8 signature[8];
    u8 checksum;
    u8 oem_id[6];
    u8 revision;
    u32 rsdt_address;
    // The following fields only exist since ACPI version 2
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved1[3];
} __attribute__((packed)) RSDP;

typedef struct ACPIEntry {
    u8 signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    u8 oem_id[6];
    u8 oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
    u8 data[];
} __attribute__((packed)) ACPIEntry;

typedef struct MADT {
    u32 lapic_address;
    u32 flags;
    u8 data[];
} __attribute__((packed)) MADT;

typedef struct MADTRecord {
    u8 type;
    u8 length;
    union {
        // Type 0: Processor Local APIC
        struct {
            u8 cpu_id;
            u8 id;
            u32 flags;
        } __attribute__((packed)) lapic;
        // Type 1: I/O APIC
        struct {
            u8 id;
            u8 reserved1;
            u32 addr;
            u32 int_base;
        } __attribute__((packed)) io_apic;
        // Type 2: I/O APIC Interrupt Source Override
        struct {
            u8 bus;
            u8 source;
            u32 gsi; // Global System Interrupt
            u16 flags;
        } __attribute__((packed)) interrupt_override;
        // Type 5: Local APIC Address Override
        struct {
            u16 reserved1;
            u64 lapic_address;
        } __attribute__((packed)) lapic_address_override;
    };
} __attribute__((packed)) MADTRecord;

// Map a temporary area
// This function is used to temporarily map ACPI tables and the I/O APIC.
// Each temporary mapping is done using two large pages and is therefore at least 2 MiB long.
// This size should be enough for any table.
// `temp_area` is the PDE number of the first page to be mapped.
// The function returns the virtuals address of the mapped area.
static void *map_temporary(u64 phys_addr, u64 temp_area) {
    u64 virt_addr = ASSEMBLE_ADDR_PDE(DEVICES_PML4E, TEMP_PDPTE, temp_area, phys_addr);
    u64 *pde = PDE_PTR(virt_addr);
    pde[0] = ((phys_addr >> LARGE_PAGE_BITS) << LARGE_PAGE_BITS) | PAGE_NX | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
    pde[1] = (((phys_addr >> LARGE_PAGE_BITS) + 1) << LARGE_PAGE_BITS) | PAGE_NX | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT;
    return (void *)virt_addr;
}

// Unmap a temporary area and invalidate the mapping
static void unmap_temporary(u64 temp_area) {
    u64 virt_addr = ASSEMBLE_ADDR_PDE(DEVICES_PML4E, TEMP_PDPTE, temp_area, 0);
    asm volatile ("invlpg [%0]" : : "r"(virt_addr));
    asm volatile ("invlpg [%0]" : : "r"(virt_addr + LARGE_PAGE_SIZE));
}

// Find the RSDP and return its address
// Returns 0 on failure.
static const RSDP *find_rsdp(void) {
    // The EBDA segment address is located at address 0x040E
    u64 ebda_start = ((u64)(*(u16 *)0x040E) << 4);
    // Search through the first 1 KiB of the EBDA for the RSDP
    // It always starts with the "RSD PTR " signature aligned to a 16-byte boundary.
    for (size_t i = 0; i < 1024; i += 16)
        if (memcmp((u8 *)(ebda_start + i), rsdp_signature, sizeof(rsdp_signature)) == 0)
            return (const RSDP *)ebda_start + i;
    // Search through the area from 0xE0000 to 0x100000
    for (size_t i = 0xE0000; i < 0x100000; i += 16)
        if (memcmp((u8 *)i, rsdp_signature, sizeof(rsdp_signature)) == 0)
            return (const RSDP *)i;
    return 0;
}

// Find the RSDT and XSDT and return its physical address
// `is_xsdt` is set depending on which table was found.
static u64 find_rsdt(const RSDP *rsdp, bool *is_xsdt) {
    // For ACPI versions below 2.0 we get the RSDT, and for version 2.0 and above we get the XSDT.
    if (rsdp->revision < 2) {
        *is_xsdt = false;
        return rsdp->rsdt_address;
    } else {
        *is_xsdt = true;
        return rsdp->xsdt_address;
    }
}

static bool parse_madt(const ACPIEntry *madt);

// Parses the RSDT or XSDT and initializes the I/O APICs using the information found in the ACPI tables
static bool parse_rsdt(u64 rsdt_phys, bool is_xsdt) {
    const ACPIEntry *rsdt = map_temporary(rsdt_phys, TEMP_AREA_RSDT);
    size_t num_entries = (rsdt->length - sizeof(ACPIEntry)) / (is_xsdt ? sizeof(u64) : sizeof(u32));
    const u8 *entries = rsdt->data;
    for (size_t i = 0; i < num_entries; i++) {
        u64 entry_phys = is_xsdt ? ((u64 *)entries)[i] : ((u32 *)entries)[i];
        const ACPIEntry *entry = map_temporary(entry_phys, TEMP_AREA_MADT);
        if (memcmp(entry->signature, "APIC", 4) == 0) {
            bool success = parse_madt(entry);
            unmap_temporary(TEMP_AREA_MADT);
            unmap_temporary(TEMP_AREA_RSDT);
            return success;
        }
        unmap_temporary(TEMP_AREA_MADT);
    }
    unmap_temporary(TEMP_AREA_RSDT);
    return false;
}

typedef struct IOAPIC {
    u32 ioregsel;
    u32 reserved1[3];
    u32 iowin;
    u32 reserved2[3];
} __attribute__((packed)) IOAPIC;

// Read an I/O APIC register
static u32 io_apic_read(volatile IOAPIC *io_apic, u32 reg) {
    io_apic->ioregsel = reg;
    return io_apic->iowin;
}

// Write to an I/O APIC register
static void io_apic_write(volatile IOAPIC *io_apic, u32 reg, u32 val) {
    io_apic->ioregsel = reg;
    io_apic->iowin = val;
}

// Describes how an interrupt should be redirected in the I/O APIC
typedef struct InterruptAssignment {
    u32 gsi;
    bool active_low;
    bool active_level;
} InterruptAssignment;

// Set an I/O APIC redirection table entry
static void io_apic_set_redirection(IOAPIC *io_apic, InterruptAssignment interrupt_assignment, bool deliver_to_all, u32 int_base, u8 vector) {
    u64 redtbl_entry =
        IOREDTBL_DESTINATION_ALL |
        (interrupt_assignment.active_level ? IOREDTBL_TRIGGER_LEVEL : 0) |
        (interrupt_assignment.active_low ? IOREDTBL_POLARITY_LOW : 0) |
        IOREDTBL_DESTINATION_LOGICAL |
        (deliver_to_all ? IOREDTBL_DELIVERY_FIXED : IOREDTBL_DELIVERY_LOWEST_PRIORITY) |
        (u64)vector;
    io_apic_write(io_apic, IOREDTBL + (interrupt_assignment.gsi - int_base) * 2, (u32)redtbl_entry);
    io_apic_write(io_apic, IOREDTBL + (interrupt_assignment.gsi - int_base) * 2 + 1, (u32)(redtbl_entry >> 32));
}

// Parse the MADT and initialize the I/O APIC using the information found there
static bool parse_madt(const ACPIEntry *madt) {
    const MADT *madt_data = (const MADT *)madt->data;
    u64 lapic_phys = madt_data->lapic_address;
    // Set the default interrupt assignments
    // These will be changed by the presence I/O APIC source override.
    InterruptAssignment interrupt_assignment_pit = (InterruptAssignment){ISA_INT_PIT, false, false};
    InterruptAssignment interrupt_assignment_keyboard = (InterruptAssignment){ISA_INT_KEYBOARD, false, false};
    InterruptAssignment interrupt_assignment_mouse = (InterruptAssignment){ISA_INT_MOUSE, false, false};
    // Iterate through the MADT records to get the interrupt source override information
    for (size_t i = 0; i < madt->length - sizeof(ACPIEntry) - sizeof(MADT); ) {
        const MADTRecord *madt_record = (const MADTRecord *)&madt_data->data[i];
        switch (madt_record->type) {
        case MADT_INT_SOURCE_OVERRIDE:
            // Only consider ISA interrupts
            if (madt_record->interrupt_override.bus != MADT_INT_BUS_ISA)
                continue;
            InterruptAssignment *interrupt_assignment = NULL;
            // If the override is for one of the interrupts we use, save the override information
            switch (madt_record->interrupt_override.source) {
            case ISA_INT_PIT:
                interrupt_assignment = &interrupt_assignment_pit;
                break;
            case ISA_INT_KEYBOARD:
                interrupt_assignment = &interrupt_assignment_keyboard;
                break;
            case ISA_INT_MOUSE:
                interrupt_assignment = &interrupt_assignment_mouse;
                break;
            }
            if (interrupt_assignment != NULL) {
                *interrupt_assignment = (InterruptAssignment){
                    .gsi = madt_record->interrupt_override.gsi,
                    .active_low = (madt_record->interrupt_override.flags & MADT_INT_POLARITY) == MADT_INT_POLARITY_LOW,
                    .active_level = (madt_record->interrupt_override.flags & MADT_INT_TRIGGER) == MADT_INT_TRIGGER_LEVEL,
                };
            }
            break;
        }
        i += madt_record->length;
    }
    // Iterate through the other MADT records
    for (size_t i = 0; i < madt->length - sizeof(ACPIEntry) - sizeof(MADT); ) {
        const MADTRecord *madt_record = (const MADTRecord *)&madt_data->data[i];
        switch (madt_record->type) {
        case MADT_LAPIC:
            // If the CPU is usable, add it to the list of CPUs
            if ((madt_record->lapic.flags & (MADT_LAPIC_ENABLED | MADT_LAPIC_ONLINE_CAPABLE)) != 0) {
                if (cpu_num < CPU_NUM_MAX) {
                    cpus[cpu_num] = madt_record->lapic.id;
                    cpu_num++;
                }
            }
            break;
        case MADT_IO_APIC: {
            // Configure the I/O APIC, setting redirections according using the interrupt assignment information gathered earlier
            IOAPIC *io_apic = map_temporary(madt_record->io_apic.addr, TEMP_AREA_IOAPIC);
            u32 int_base = madt_record->io_apic.int_base;
            u32 max_redir = (io_apic_read(io_apic, IOAPICVER) & IOAPICVER_MAX_REDIR_MASK) >> IOAPICVER_MAX_REDIR_OFFSET;
            for (u32 i = int_base; i <= int_base + max_redir; i++) {
                if (i == interrupt_assignment_pit.gsi)
                    io_apic_set_redirection(io_apic, interrupt_assignment_pit, true, int_base, INT_VECTOR_PIT);
                else if (i == interrupt_assignment_keyboard.gsi)
                    io_apic_set_redirection(io_apic, interrupt_assignment_keyboard, false, int_base, INT_VECTOR_KEYBOARD);
                else if (i == interrupt_assignment_mouse.gsi)
                    io_apic_set_redirection(io_apic, interrupt_assignment_mouse, false, int_base, INT_VECTOR_MOUSE);
            }
            unmap_temporary(TEMP_AREA_IOAPIC);
            break;
        }
        case MADT_LAPIC_ADDR_OVERRIDE:
            lapic_phys = madt_record->lapic_address_override.lapic_address;
            break;
        }
        i += madt_record->length;
    }
    // Map the LAPIC
    u64 *lapic_pte_ptr = PTE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, LAPIC_PDPTE, 0));
    if (lapic_phys % PAGE_SIZE == 0) {
        *lapic_pte_ptr = lapic_phys | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
    } else {
        u64 lapic_page = (lapic_phys >> PAGE_BITS) << PAGE_BITS;
        lapic_pte_ptr[0] = lapic_page | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
        lapic_pte_ptr[1] = (lapic_page + PAGE_SIZE) | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT;
    }
    lapic = (u64 *)(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, LAPIC_PDPTE, 0) + lapic_phys % PAGE_SIZE);
    return true;
}

// Locate and parse the ACPI tables and set up the I/O APIC according to them
bool acpi_init(void) {
    // Set up page table structures used for mapping the ACPI tables and the LAPIC
    u64 temp_pd_page = page_alloc();
    u64 lapic_pd_page = page_alloc();
    u64 lapic_pt_page = page_alloc();
    if (temp_pd_page == 0 || lapic_pd_page == 0 || lapic_pt_page == 0)
        return NULL;
    *PDPTE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, TEMP_PDPTE, 0)) = temp_pd_page | PAGE_WRITE | PAGE_PRESENT;
    memset(PDE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, TEMP_PDPTE, 0)), 0, PAGE_SIZE);
    *PDPTE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, LAPIC_PDPTE, 0)) = lapic_pd_page | PAGE_WRITE | PAGE_PRESENT;
    memset(PDE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, LAPIC_PDPTE, 0)), 0, PAGE_SIZE);
    *PDE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, LAPIC_PDPTE, 0)) = lapic_pt_page | PAGE_WRITE | PAGE_PRESENT;
    memset(PTE_PTR(ASSEMBLE_ADDR_PDPTE(DEVICES_PML4E, LAPIC_PDPTE, 0)), 0, PAGE_SIZE);
    // Locate and parse the tables
    const RSDP *rsdp_phys = find_rsdp();
    if (rsdp_phys == NULL)
        return false;
    bool is_xsdt;
    u64 rsdt_phys = find_rsdt(rsdp_phys, &is_xsdt);
    if (rsdt_phys == 0)
        return false;
    return parse_rsdt(rsdt_phys, is_xsdt);
}
