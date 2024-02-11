#include "types.h"
#include "ahci.h"

#include "page.h"

#define HBA_CAP_64_BIT_ADDR (UINT32_C(1) << 31)
#define HBA_CAP_NUM_COMMAND_SLOTS_OFFSET 8
#define HBA_CONTROL_INTERRUPT (UINT32_C(1) << 1)
#define HBA_CONTROL_AHCI (UINT32_C(1) << 31)
#define HBA_CAP_EXT_BIOS_OS_HANDOFF (UINT32_C(1) << 0)
#define HBA_BOHC_BIOS_OWNERSHIP (UINT32_C(1) << 0)
#define HBA_BOHC_OS_OWNERSHIP (UINT32_C(1) << 1)
#define HBA_BOHC_BIOS_BUSY (UINT32_C(1) << 4)

#define PORT_CMD_START (UINT32_C(1) << 0)
#define PORT_CMD_SPIN_UP (UINT32_C(1) << 1)
#define PORT_CMD_POWER_ON (UINT32_C(1) << 2)
#define PORT_CMD_FIS_RECEIVE_ENABLE (UINT32_C(1) << 4)
#define PORT_CMD_FIS_RECEIVE_RUNNING (UINT32_C(1) << 14)
#define PORT_CMD_CMD_LIST_RUNNING (UINT32_C(1) << 15)
#define PORT_SIGNATURE_ATA_DEVICE UINT32_C(0x00000101)
#define PORT_SATA_STATUS_DET (UINT32_C(0xF) << 0)
#define PORT_SATA_STATUS_DET_DETECTED (UINT32_C(1) << 0)
#define PORT_SATA_STATUS_DET_ESTABLISHED (UINT32_C(3) << 0)
#define PORT_INT_ERROR_ANY UINT32_C(0xFDC00010)

#define COMMAND_LIST_FIS_LENGTH 5
#define FIS_TYPE_HOST_TO_DEVICE 0x27
#define FIS_FLAGS_COMMAND 0x80
#define FIS_COMMAND_IDENTIFY_DEVICE 0xEC

#define IDENTIFY_FIELD_VALID_MASK UINT32_C(0xC000)
#define IDENTIFY_FIELD_VALID UINT32_C(0x4000)
#define IDENTIFY_CAP 49
#define IDENTIFY_CAP_LBA (UINT32_C(1) << 9)
#define IDENTIFY_CAP_DMA (UINT32_C(1) << 8)
#define IDENTIFY_SECTOR_COUNT_28 60
#define IDENTIFY_COM_SUP_2 83
#define IDENTIFY_COM_SUP_2_LBA_48 (UINT32_C(1) << 10)
#define IDENTIFY_SECTOR_COUNT_48 100
#define IDENTIFY_SECTOR_SIZE_FLAGS 106
#define IDENTIFY_SECTOR_SIZE_FLAGS_LOGICAL_SIZE_SUPPORTED (UINT32_C(1) << 12)
#define IDENTIFY_LOGICAL_SECTOR_SIZE 117

#define AHCI_PDE 0x001
#define AHCI_MAPPING_AREA ASSEMBLE_ADDR_PDE(0x1FD, 0x002, 0x001, 0)

// Number of PIT cycles in 25 ms
#define WAIT_BEFORE_BIOS_BUSY_PIT_CYCLES 29830
// Number of PIT cycles in 2 s
#define WAIT_AFTER_BIOS_BUSY_PIT_CYCLES 2386364

extern void pit_wait(u32 cycles);

extern u64 pd_devices_other[];

extern u32 ahci_base;

typedef struct HBAPort {
    u64 command_list_base;
    u64 fis_base;
    u32 interrupt_status;
    u32 interrupt_enable;
    u32 command_status;
    u32 reserved1;
    u32 task_file_data;
    u32 signature;
    u32 sata_status;
    u32 sata_control;
    u32 sata_error;
    u32 sata_active;
    u32 command_issue;
    u32 sata_notification;
    u32 switching_control;
    u32 device_sleep;
    u32 reserved2[14];
} __attribute__((packed)) HBAPort;

typedef struct HBA {
    u32 capabilities;
    u32 control;
    u32 interrupt_status;
    u32 ports_implemented;
    u32 version;
    u32 ccc_control;
    u32 ccc_ports;
    u32 em_location;
    u32 em_control;
    u32 capabilities_extended;
    u32 bios_os_handoff;
    u32 reserved1[53];
    HBAPort ports[32];
} __attribute__((packed)) HBA;

typedef struct CommandHeader {
    u16 flags;
    u16 table_length;
    volatile u32 byte_count;
    u64 command_table;
    u32 reserved1[4];
} __attribute__((packed)) CommandHeader;

typedef struct CommandFIS {
    u8 fis_type;
    u8 flags;
    u8 command;
    u8 features0;
    u8 lba0;
    u8 lba1;
    u8 lba2;
    u8 device;
    u8 lba3;
    u8 lba4;
    u8 lba5;
    u8 features1;
    u16 sector_count;
    u8 icc;
    u8 control;
    u8 reserved[4];
} __attribute__((packed)) CommandFIS;

typedef struct CommandTable {
    CommandFIS command_fis;
    u32 reserved1[11];
    u32 atapi_command[4];
    u32 reserved2[12];
    struct {
        u64 data_base;
        u32 reserved1;
        u32 byte_count;
    } __attribute__((packed)) region[8];
} __attribute__((packed)) CommandTable;

typedef struct ReceivedFIS {
    u8 reserved1[256];
} __attribute__((packed)) ReceivedFIS;

static u32 command_slots_max;
static CommandHeader (*command_lists)[32];
static CommandTable *command_tables;
static ReceivedFIS *received_fis_structs;

// Initialize the AHCI controller
err_t ahci_init(void) {
    // Create PT for AHCI mappings
    u64 pt_ahci_phys = page_alloc_clear();
    if (pt_ahci_phys == 0)
        return ERR_KERNEL_NO_MEMORY;
    pd_devices_other[AHCI_PDE] = pt_ahci_phys | PAGE_WRITE | PAGE_PRESENT;
    u64 *pt_ahci = PHYS_ADDR(pt_ahci_phys);
    // Map HBA memory space as uncachable
    pt_ahci[0] = ahci_base | PAGE_GLOBAL | PAGE_PCD | PAGE_WRITE | PAGE_PRESENT;
    pt_ahci[1] = (ahci_base + PAGE_SIZE) | PAGE_GLOBAL | PAGE_PCD | PAGE_WRITE | PAGE_PRESENT;
    volatile HBA *hba = (volatile HBA *)AHCI_MAPPING_AREA;
    // Perform BIOS/OS handoff if necessary
    if (hba->capabilities_extended & HBA_CAP_EXT_BIOS_OS_HANDOFF) {
        hba->bios_os_handoff |= HBA_BOHC_OS_OWNERSHIP;
        while (hba->bios_os_handoff & HBA_BOHC_BIOS_OWNERSHIP)
            ;
        pit_wait(WAIT_BEFORE_BIOS_BUSY_PIT_CYCLES);
        if (hba->bios_os_handoff & HBA_BOHC_BIOS_BUSY)
            pit_wait(WAIT_AFTER_BIOS_BUSY_PIT_CYCLES);
    }
    // Check 64-bit addressing is supported
    if (!(hba->capabilities & HBA_CAP_64_BIT_ADDR))
        return ERR_KERNEL_OTHER;
    // Find which ports are connected
    u32 ports_connected = hba->ports_implemented;
    u32 ports_connected_num = 0;
    for (u32 i = 0; i < 32; i++) {
        // Skip if port not implemented
        if (!((ports_connected >> i) & 1))
            continue;
        // Determine if device is present
        bool device_present =
            (hba->ports[i].sata_status & PORT_SATA_STATUS_DET) == PORT_SATA_STATUS_DET_DETECTED ||
            (hba->ports[i].sata_status & PORT_SATA_STATUS_DET) == PORT_SATA_STATUS_DET_ESTABLISHED;
        // Mark port as connected if its device is present and is an ATA device
        if (device_present && hba->ports[i].signature == PORT_SIGNATURE_ATA_DEVICE)
            ports_connected_num++;
        else
            ports_connected &= ~(UINT32_C(1) << i);
    }
    // Enable AHCI and interrupts
    hba->control |= HBA_CONTROL_INTERRUPT | HBA_CONTROL_AHCI;
    // Get the number of command slots supported
    command_slots_max = ((hba->capabilities >> HBA_CAP_NUM_COMMAND_SLOTS_OFFSET) & 0x1F) + 1;
    // Calculate the number of pages needed for memory for the received FIS structures, command lists and command tables
    // There is one received FIS structure and command list per port and one command table per command slot.
    // A command list takes up 1 KiB, while a received FIS structure and command table take up 256 B each.
    u32 pages_to_map = (ports_connected_num * (5 + command_slots_max) + 15) / 16;
    u64 ahci_pages[pages_to_map];
    // Map the pages as uncached
    for (u32 i = 0; i < pages_to_map; i++) {
        u64 page = page_alloc_clear();
        if (page == 0)
            return ERR_KERNEL_NO_MEMORY;
        pt_ahci[2 + i] = page | PAGE_GLOBAL | PAGE_PCD | PAGE_WRITE | PAGE_PRESENT;
        ahci_pages[i] = page;
    }
    // Create pointers to allocated structures
    // They are laid out so that none cross a page boundary.
    command_lists = (CommandHeader (*)[32])(AHCI_MAPPING_AREA + 2 * PAGE_SIZE);
    command_tables = (CommandTable *)(u64)(command_lists + ports_connected_num);
    received_fis_structs = (ReceivedFIS *)(u64)(command_tables + ports_connected_num * command_slots_max);
    // Allocate buffer for results of IDENTIFY DEVICE command
    u64 identify_buffer_page = page_alloc();
    if (identify_buffer_page == 0)
        return ERR_KERNEL_NO_MEMORY;
    // Initialize connected ports
    u32 ports_initialized = 0;
    for (u32 i = 0; i < 32; i++) {
        // Skip if port not connected
        if (!((ports_connected >> i) & 1))
            continue;
        u32 conn_i = ports_initialized++;
        // Spin up device
        hba->ports[i].command_status |= PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP;
        while ((hba->ports[i].sata_status & PORT_SATA_STATUS_DET) != PORT_SATA_STATUS_DET_ESTABLISHED)
            ;
        // Stop command list processing
        hba->ports[i].command_status &= ~PORT_CMD_START;
        while (hba->ports[i].command_status & PORT_CMD_CMD_LIST_RUNNING)
            ;
        // Stop FIS receive
        hba->ports[i].command_status &= ~PORT_CMD_FIS_RECEIVE_ENABLE;
        while (hba->ports[i].command_status & PORT_CMD_FIS_RECEIVE_RUNNING)
            ;
        // Set up command list to point to command tables
        for (u32 j = 0; j < command_slots_max; j++) {
            size_t command_table_offset = ports_connected_num * 1024 + (i * command_slots_max + j) * 256;
            command_lists[i][j].command_table = ahci_pages[command_table_offset / PAGE_SIZE] + command_table_offset % PAGE_SIZE;
        }
        // Set command list and FIS base to their physical addresses
        size_t command_list_offset = i * 1024;
        size_t fis_offset = ports_connected_num * (1024 + command_slots_max * 256) + i * 256;
        hba->ports[i].command_list_base = ahci_pages[command_list_offset / PAGE_SIZE] + command_list_offset % PAGE_SIZE;
        hba->ports[i].fis_base = ahci_pages[fis_offset / PAGE_SIZE] + fis_offset % PAGE_SIZE;
        // Reenable FIS receive and command list processing
        hba->ports[i].command_status |= PORT_CMD_FIS_RECEIVE_ENABLE;
        hba->ports[i].command_status |= PORT_CMD_START;
        // Clear SATA error and interrupt status registers
        hba->ports[i].sata_error = UINT32_C(-1);
        hba->ports[i].interrupt_status = UINT32_C(-1);
        // Construct IDENTIFY DEVICE command in command slot 0
        command_tables[conn_i * command_slots_max].command_fis.fis_type = FIS_TYPE_HOST_TO_DEVICE;
        command_tables[conn_i * command_slots_max].command_fis.flags = FIS_FLAGS_COMMAND;
        command_tables[conn_i * command_slots_max].command_fis.command = FIS_COMMAND_IDENTIFY_DEVICE;
        command_tables[conn_i * command_slots_max].region[0].data_base = identify_buffer_page;
        command_tables[conn_i * command_slots_max].region[0].byte_count = 511;
        command_lists[conn_i][0].table_length = 1;
        command_lists[conn_i][0].flags = COMMAND_LIST_FIS_LENGTH;
        // Send command and wait for it to be processed
        hba->ports[i].command_issue = 1;
        while (hba->ports[i].command_issue & 1) {
            while (!hba->ports[i].interrupt_status)
                ;
            if (hba->ports[i].interrupt_status & PORT_INT_ERROR_ANY)
                goto drive_fail;
        }
        u16 *identify_buffer = PHYS_ADDR(identify_buffer_page);
        // Check transferred byte count for underflow
        if (command_lists[conn_i * command_slots_max][0].byte_count != 512)
            goto drive_fail;
        // Check for LBA and DMA in capabilities
        if (!(identify_buffer[IDENTIFY_CAP] & (IDENTIFY_CAP_LBA | IDENTIFY_CAP_DMA)))
            goto drive_fail;
        // Get logical sector size in bytes
        u32 logical_sector_size = 512;
        if ((identify_buffer[IDENTIFY_SECTOR_SIZE_FLAGS] & IDENTIFY_FIELD_VALID_MASK) == IDENTIFY_FIELD_VALID
                && (identify_buffer[IDENTIFY_SECTOR_SIZE_FLAGS] & IDENTIFY_SECTOR_SIZE_FLAGS_LOGICAL_SIZE_SUPPORTED))
            logical_sector_size =
                2 * ((u32)identify_buffer[IDENTIFY_LOGICAL_SECTOR_SIZE]
                    | ((u32)identify_buffer[IDENTIFY_LOGICAL_SECTOR_SIZE + 1] << 16));
        // Get logical sector count
        u64 logical_sector_count;
        if ((identify_buffer[IDENTIFY_COM_SUP_2] & IDENTIFY_FIELD_VALID_MASK) == IDENTIFY_FIELD_VALID
                && (identify_buffer[IDENTIFY_COM_SUP_2] & IDENTIFY_COM_SUP_2_LBA_48))
            logical_sector_count = 
                (u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48]
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 1] << 16)
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 2] << 32)
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 3] << 48);
        else
            logical_sector_count =
                (u32)identify_buffer[IDENTIFY_SECTOR_COUNT_28]
                | ((u32)identify_buffer[IDENTIFY_SECTOR_COUNT_28 + 1] << 16);
        continue;
drive_fail:
        ;
    }
    page_free(identify_buffer_page);
    return 0;
}
