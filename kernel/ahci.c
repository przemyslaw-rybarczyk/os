#include "types.h"
#include "ahci.h"

#include "alloc.h"
#include "channel.h"
#include "framebuffer.h"
#include "page.h"
#include "process.h"
#include "spinlock.h"
#include "string.h"

#include <zr/drive.h>

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
#define PORT_INT_ERROR_ANY UINT32_C(0xF9C00010)

#define COMMAND_LIST_FIS_LENGTH 5
#define FIS_TYPE_HOST_TO_DEVICE 0x27
#define FIS_FLAGS_COMMAND 0x80
#define FIS_COMMAND_READ_DMA_EXT 0x25
#define FIS_COMMAND_READ_DMA 0xC8
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
} HBAPort;

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
} HBA;

typedef struct CommandHeader {
    u16 flags;
    u16 table_length;
    volatile u32 byte_count;
    u64 command_table;
    u32 reserved1[4];
} CommandHeader;

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
} CommandFIS;

typedef struct CommandTable {
    CommandFIS command_fis;
    u32 reserved1[11];
    u32 atapi_command[4];
    u32 reserved2[12];
    struct {
        u64 data_base;
        u32 reserved1;
        u32 byte_count;
    } region[8];
} CommandTable;

typedef struct ReceivedFIS {
    u8 reserved1[256];
} ReceivedFIS;

typedef struct IssuedRequest {
    Message *message;
    Message *reply;
    size_t outstanding_commands;
    bool failed;
} IssuedRequest;

typedef struct IssuedCommand {
    IssuedRequest *request;
    i64 offset;
} IssuedCommand;

typedef struct Drive {
    // Lock for variables related to the port
    spinlock_t lock;
    // Size of sectors on the drive
    u64 sector_size;
    // Number of sectors on the drive
    u64 sector_count;
    // Set if drive supports LBA48
    bool is_lba48;
    // Set if receive thread is waiting for a command slot to be freed up
    bool receive_thread_blocked;
    // Set if reply thread is waiting a command to be completed
    bool reply_thread_blocked;
    // Set if reply thread should check drive status again instead of blocking
    bool reply_thread_repeat;
    // Bitmask for commands that have been issued
    u32 commands_issued;
    // Pointer to thread responsible for receiving messages from userspace and issuing requests
    Process *receive_thread;
    // Pointer to thread responsible for receiving replies from the drive and passing them to userspace
    Process *reply_thread;
    // Queue for requests from userspace
    MessageQueue *queue;
    // AHCI command list
    CommandHeader *command_list;
    // List of AHCI command tables for each slot
    CommandTable *command_tables;
    // List of commands issued for each slot
    IssuedCommand *issued_commands;
} Drive;

static Drive *drives[32] = {};

volatile HBA *hba = (volatile HBA *)AHCI_MAPPING_AREA;
static u32 command_slots_max;

// Bit set for each connected port
static u32 ports_connected;

// Number of drives shown to userspace and port number of each one
static u32 user_drive_num = 0;
static u32 user_drive_port[32];

// Used to initialize connection number for port threads
static volatile _Atomic u32 ahci_receive_threads_initialized = 0;
static volatile _Atomic u32 ahci_reply_threads_initialized = 0;

Message *drive_info_msg;

// Initialize the AHCI controller
err_t ahci_init(void) {
    err_t err;
    // Create PT for AHCI mappings
    u64 pt_ahci_phys = page_alloc_clear();
    if (pt_ahci_phys == 0)
        return ERR_KERNEL_NO_MEMORY;
    pd_devices_other[AHCI_PDE] = pt_ahci_phys | PAGE_WRITE | PAGE_PRESENT;
    u64 *pt_ahci = PHYS_ADDR(pt_ahci_phys);
    // Map HBA memory space as uncachable
    pt_ahci[0] = ahci_base | PAGE_GLOBAL | PAGE_PCD | PAGE_WRITE | PAGE_PRESENT;
    pt_ahci[1] = (ahci_base + PAGE_SIZE) | PAGE_GLOBAL | PAGE_PCD | PAGE_WRITE | PAGE_PRESENT;
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
    if (!(hba->capabilities & HBA_CAP_64_BIT_ADDR)) {
        print_string("HBA does not support 64-bit addressing\n");
        return ERR_KERNEL_OTHER;
    }
    // Find which ports are connected
    ports_connected = hba->ports_implemented;
    u32 ports_connected_num = 0;
    for (u32 port_i = 0; port_i < 32; port_i++) {
        // Skip if port not implemented
        if (!((ports_connected >> port_i) & 1))
            continue;
        // Determine if device is present
        bool device_present =
            (hba->ports[port_i].sata_status & PORT_SATA_STATUS_DET) == PORT_SATA_STATUS_DET_DETECTED ||
            (hba->ports[port_i].sata_status & PORT_SATA_STATUS_DET) == PORT_SATA_STATUS_DET_ESTABLISHED;
        // Mark port as connected if its device is present and is an ATA device
        if (device_present && hba->ports[port_i].signature == PORT_SIGNATURE_ATA_DEVICE)
            ports_connected_num++;
        else
            ports_connected &= ~(UINT32_C(1) << port_i);
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
    CommandHeader *command_headers = (CommandHeader *)(AHCI_MAPPING_AREA + 2 * PAGE_SIZE);
    CommandTable *command_tables_all = (CommandTable *)(u64)(command_headers + 32 * ports_connected_num);
    // Allocate buffer for results of IDENTIFY DEVICE command
    u64 identify_buffer_page = page_alloc();
    if (identify_buffer_page == 0)
        return ERR_KERNEL_NO_MEMORY;
    // Initialize connected ports
    u32 drive_id = 0;
    for (u32 port_i = 0; port_i < 32; port_i++) {
        // Skip if port not connected
        if (!((ports_connected >> port_i) & 1))
            continue;
        CommandHeader *command_list = &command_headers[32 * drive_id];
        CommandTable *command_tables = &command_tables_all[command_slots_max * drive_id];
        // Spin up device
        hba->ports[port_i].command_status |= PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP;
        while ((hba->ports[port_i].sata_status & PORT_SATA_STATUS_DET) != PORT_SATA_STATUS_DET_ESTABLISHED)
            ;
        // Stop command list processing
        hba->ports[port_i].command_status &= ~PORT_CMD_START;
        while (hba->ports[port_i].command_status & PORT_CMD_CMD_LIST_RUNNING)
            ;
        // Stop FIS receive
        hba->ports[port_i].command_status &= ~PORT_CMD_FIS_RECEIVE_ENABLE;
        while (hba->ports[port_i].command_status & PORT_CMD_FIS_RECEIVE_RUNNING)
            ;
        // Set up command list to point to command tables
        for (u32 j = 0; j < command_slots_max; j++) {
            size_t command_table_offset = ports_connected_num * 1024 + (drive_id * command_slots_max + j) * 256;
            command_list[j].command_table = ahci_pages[command_table_offset / PAGE_SIZE] + command_table_offset % PAGE_SIZE;
        }
        // Set command list and FIS base to their physical addresses
        size_t command_list_offset = drive_id * 1024;
        size_t fis_offset = ports_connected_num * (1024 + command_slots_max * 256) + drive_id * 256;
        hba->ports[port_i].command_list_base = ahci_pages[command_list_offset / PAGE_SIZE] + command_list_offset % PAGE_SIZE;
        hba->ports[port_i].fis_base = ahci_pages[fis_offset / PAGE_SIZE] + fis_offset % PAGE_SIZE;
        // Reenable FIS receive and command list processing
        hba->ports[port_i].command_status |= PORT_CMD_FIS_RECEIVE_ENABLE;
        hba->ports[port_i].command_status |= PORT_CMD_START;
        // Clear SATA error and interrupt status registers
        hba->ports[port_i].sata_error = UINT32_C(-1);
        hba->ports[port_i].interrupt_status = UINT32_C(-1);
        // Construct IDENTIFY DEVICE command in command slot 0
        command_tables[0].command_fis.fis_type = FIS_TYPE_HOST_TO_DEVICE;
        command_tables[0].command_fis.flags = FIS_FLAGS_COMMAND;
        command_tables[0].command_fis.command = FIS_COMMAND_IDENTIFY_DEVICE;
        command_tables[0].region[0].data_base = identify_buffer_page;
        command_tables[0].region[0].byte_count = 511;
        command_list[0].table_length = 1;
        command_list[0].flags = COMMAND_LIST_FIS_LENGTH;
        // Send command and wait for it to be processed
        hba->ports[port_i].command_issue = 1;
        while (hba->ports[port_i].command_issue & 1) {
            while (!hba->ports[port_i].interrupt_status)
                ;
            if (hba->ports[port_i].interrupt_status & PORT_INT_ERROR_ANY)
                continue;
        }
        // Clear SATA error and interrupt status registers again
        hba->ports[port_i].sata_error = UINT32_C(-1);
        hba->ports[port_i].interrupt_status = UINT32_C(-1);
        u16 *identify_buffer = PHYS_ADDR(identify_buffer_page);
        // Check transferred byte count for underflow
        if (command_list[0].byte_count != 512)
            continue;
        // Check for LBA and DMA in capabilities
        if (!(identify_buffer[IDENTIFY_CAP] & (IDENTIFY_CAP_LBA | IDENTIFY_CAP_DMA)))
            continue;
        u64 sector_size = 512;
        // Get logical sector size in bytes
        if ((identify_buffer[IDENTIFY_SECTOR_SIZE_FLAGS] & IDENTIFY_FIELD_VALID_MASK) == IDENTIFY_FIELD_VALID
                && (identify_buffer[IDENTIFY_SECTOR_SIZE_FLAGS] & IDENTIFY_SECTOR_SIZE_FLAGS_LOGICAL_SIZE_SUPPORTED))
            sector_size =
                2 * ((u32)identify_buffer[IDENTIFY_LOGICAL_SECTOR_SIZE]
                    | ((u32)identify_buffer[IDENTIFY_LOGICAL_SECTOR_SIZE + 1] << 16));
        // Require sector size to be power of two fitting in page
        if (sector_size == 0 || sector_size > PAGE_SIZE
                || (sector_size & (sector_size - 1)) != 0)
            continue;
        // Determine if drive supports LBA48
        bool is_lba48 = (identify_buffer[IDENTIFY_COM_SUP_2] & IDENTIFY_FIELD_VALID_MASK) == IDENTIFY_FIELD_VALID
            && (identify_buffer[IDENTIFY_COM_SUP_2] & IDENTIFY_COM_SUP_2_LBA_48);
        // Get logical sector count
        u64 sector_count = 0;
        if (is_lba48)
            sector_count =
                (u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48]
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 1] << 16)
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 2] << 32)
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 3] << 48);
        else
            sector_count =
                (u32)identify_buffer[IDENTIFY_SECTOR_COUNT_28]
                | ((u32)identify_buffer[IDENTIFY_SECTOR_COUNT_28 + 1] << 16);
        if (sector_count == 0)
            continue;
        // Allocate message queue
        MessageQueue *port_queue = mqueue_alloc();
        if (port_queue == NULL)
            return ERR_KERNEL_NO_MEMORY;
        // Spawn receive and reply thread
        Process *receive_thread;
        Process *reply_thread;
        err = process_create(&receive_thread, (ResourceList){0, NULL});
        if (err)
            return err;
        err = process_create(&reply_thread, (ResourceList){0, NULL});
        if (err)
            return err;
        process_set_kernel_stack(receive_thread, ahci_drive_receive_kernel_thread_main);
        process_set_kernel_stack(reply_thread, ahci_drive_reply_kernel_thread_main);
        process_enqueue(receive_thread);
        process_enqueue(reply_thread);
        user_drive_num++;
        // Enable interrupts for port
        hba->ports[port_i].interrupt_enable = UINT32_C(-1);
        // Allocate drive structure
        drives[port_i] = malloc(sizeof(Drive));
        if (drives[port_i] == NULL)
            return ERR_KERNEL_NO_MEMORY;
        memset(drives[port_i], 0, sizeof(Drive));
        drives[port_i]->sector_size = sector_size;
        drives[port_i]->sector_count = sector_count;
        drives[port_i]->is_lba48 = is_lba48;
        drives[port_i]->queue = port_queue;
        drives[port_i]->command_list = command_list;
        drives[port_i]->command_tables = command_tables;
        // Allocate memory for issued commands
        drives[port_i]->issued_commands = malloc(sizeof(IssuedCommand) * command_slots_max);
        if (drives[port_i]->issued_commands == NULL)
            return ERR_KERNEL_NO_MEMORY;
        user_drive_port[drive_id] = port_i;
        drive_id++;
    }
    page_free(identify_buffer_page);
    // Create message with drive info that will be passed to init process
    drive_info_msg = message_alloc(sizeof(PhysDriveInfo) * user_drive_num);
    if (user_drive_num != 0 && drive_info_msg == NULL)
        return ERR_KERNEL_NO_MEMORY;
    PhysDriveInfo *drive_info = drive_info_msg->data;
    for (u32 drive_id = 0; drive_id < user_drive_num; drive_id++) {
        u32 port_i = user_drive_port[drive_id];
        drive_info[drive_id].sector_size = drives[port_i]->sector_size;
        drive_info[drive_id].sector_count = drives[port_i]->sector_count;
    }
    // Clear interrupts
    hba->interrupt_status = UINT32_C(-1);
    return 0;
}

_Noreturn void ahci_drive_receive_kernel_thread_main(void) {
    err_t err;
    // Get port number
    u32 port_i = user_drive_port[atomic_fetch_add(&ahci_receive_threads_initialized, 1)];
    u32 sectors_per_page = PAGE_SIZE / drives[port_i]->sector_size;
    drives[port_i]->receive_thread = cpu_local->current_process;
    while (1) {
        Message *message;
        // Get message from user process
        mqueue_receive(drives[port_i]->queue, &message, false, false, TIMEOUT_NONE);
        if (message->data_size != sizeof(FileRange) || message->handles_size != 0) {
            err = ERR_INVALID_ARG;
            goto fail;
        }
        FileRange *request = (FileRange *)message->data;
        FileRange *bounds = (FileRange *)message->tag.data[1];
        u64 offset = request->offset + bounds->offset;
        u64 length = request->length;
        // Handle case of zero length separately
        if (length == 0) {
            Message *reply = message_alloc_copy(0, NULL);
            if (reply == NULL) {
                err = ERR_NO_MEMORY;
                goto fail;
            }
            message_reply(message, reply);
            message_free(message);
            continue;
        }
        // Check bounds
        if (request->offset + request->length >= bounds->length || request->offset + request->length < request->offset
                || offset + length > drives[port_i]->sector_size * drives[port_i]->sector_count || offset + length < offset) {
            err = ERR_OUT_OF_RANGE;
            goto fail;
        }
        // Convert offset and length into pages
        u64 offset_page = offset / PAGE_SIZE;
        u64 length_pages = (offset + length - 1) / PAGE_SIZE - offset / PAGE_SIZE + 1;
        // Allocate issued request structure
        IssuedRequest *issued_request = malloc(sizeof(IssuedRequest));
        if (issued_request == NULL) {
            err = ERR_NO_MEMORY;
            goto fail;
        }
        issued_request->message = message;
        issued_request->outstanding_commands = length_pages;
        issued_request->failed = false;
        // Allocate reply
        issued_request->reply = message_alloc(length);
        if (issued_request->reply == NULL) {
            err = ERR_NO_MEMORY;
            goto fail_reply_alloc;
        }
        // Issue a request for each page
        for (u64 i = 0; i < length_pages; i++) {
            // Allocate buffer page
            u64 buffer_page;
            buffer_page = page_alloc();
            if (buffer_page == 0) {
                err = ERR_NO_MEMORY;
                goto fail_buffer_page_alloc;
            }
            spinlock_acquire(&drives[port_i]->lock);
            // Get next empty slot
            u32 slot_i;
            while (1) {
                for (slot_i = 0; slot_i < command_slots_max; slot_i++)
                    if (!((drives[port_i]->commands_issued >> slot_i) & 1))
                        goto slot_found;
                // Block if no free slots available
                drives[port_i]->receive_thread_blocked = true;
                process_block(&drives[port_i]->lock);
                spinlock_acquire(&drives[port_i]->lock);
            }
slot_found:;
            // Construct request
            CommandHeader *command_header = &drives[port_i]->command_list[slot_i];
            CommandTable *command_table = &drives[port_i]->command_tables[slot_i];
            u64 lba = (offset_page + i) * sectors_per_page;
            command_table->command_fis.fis_type = FIS_TYPE_HOST_TO_DEVICE;
            command_table->command_fis.flags = FIS_FLAGS_COMMAND;
            command_table->command_fis.command = drives[port_i]->is_lba48 ? FIS_COMMAND_READ_DMA_EXT : FIS_COMMAND_READ_DMA;
            command_table->command_fis.device = drives[port_i]->is_lba48 ? (1 << 6) : 0;
            command_table->command_fis.lba0 = (u8)lba;
            command_table->command_fis.lba1 = (u8)(lba >> 8);
            command_table->command_fis.lba2 = (u8)(lba >> 16);
            command_table->command_fis.lba3 = (u8)(lba >> 24);
            command_table->command_fis.lba4 = (u8)(lba >> 32);
            command_table->command_fis.lba5 = (u8)(lba >> 40);
            command_table->command_fis.sector_count = sectors_per_page;
            command_table->region[0].data_base = buffer_page;
            command_table->region[0].byte_count = PAGE_SIZE - 1;
            command_header->table_length = 1;
            command_header->flags = COMMAND_LIST_FIS_LENGTH;
            command_header->byte_count = 0;
            // Issue request
            hba->ports[port_i].command_issue = UINT32_C(1) << slot_i;
            // Construct issued command structure
            IssuedCommand *issued_command = &drives[port_i]->issued_commands[slot_i];
            issued_command->request = issued_request;
            issued_command->offset = (offset_page + i) * PAGE_SIZE - offset;
            // Mark command as issued internally
            drives[port_i]->commands_issued |= UINT32_C(1) << slot_i;
            spinlock_release(&drives[port_i]->lock);
        }
        continue;
fail_buffer_page_alloc:
        message_free(issued_request->reply);
fail_reply_alloc:
        free(issued_request);
fail:
        message_reply_error(message, err);
        message_free(message);
    }
}

_Noreturn void ahci_drive_reply_kernel_thread_main(void) {
    // Get port number
    u32 port_i = user_drive_port[atomic_fetch_add(&ahci_reply_threads_initialized, 1)];
    drives[port_i]->reply_thread = cpu_local->current_process;
    while (1) {
        // Block until an interrupt from the drive
        spinlock_acquire(&drives[port_i]->lock);
        if (drives[port_i]->reply_thread_repeat) {
            drives[port_i]->reply_thread_repeat = false;
        } else {
            drives[port_i]->reply_thread_blocked = true;
            process_block(&drives[port_i]->lock);
            spinlock_acquire(&drives[port_i]->lock);
        }
        // Get interrupt status and clear it
        u32 interrupt_status = hba->ports[port_i].interrupt_status;
        hba->ports[port_i].interrupt_status = interrupt_status;
        hba->interrupt_status = UINT32_C(1) << port_i;
        // Go over all commands that have been completed
        u32 commands_completed = drives[port_i]->commands_issued & ~hba->ports[port_i].command_issue;
        spinlock_release(&drives[port_i]->lock);
        for (u32 slot_i = 0; slot_i < command_slots_max; slot_i++) {
            if (!((commands_completed >> slot_i) & 1))
                continue;
            CommandHeader *command_header = &drives[port_i]->command_list[slot_i];
            CommandTable *command_table = &drives[port_i]->command_tables[slot_i];
            void *buffer = PHYS_ADDR(command_table->region[0].data_base);
            IssuedCommand *issued_command = &drives[port_i]->issued_commands[slot_i];
            IssuedRequest *issued_request = issued_command->request;
            if (command_header->byte_count == PAGE_SIZE) {
                // Copy data from buffer to reply data
                if (issued_command->offset >= 0 && issued_command->offset + PAGE_SIZE <= issued_request->reply->data_size)
                    memcpy(issued_request->reply->data + issued_command->offset, buffer, PAGE_SIZE);
                else if (issued_command->offset >= 0)
                    memcpy(issued_request->reply->data + issued_command->offset, buffer, issued_request->reply->data_size - issued_command->offset);
                else if (issued_command->offset + PAGE_SIZE <= issued_request->reply->data_size)
                    memcpy(issued_request->reply->data, buffer - issued_command->offset, PAGE_SIZE + issued_command->offset);
                else
                    memcpy(issued_request->reply->data, buffer - issued_command->offset, issued_request->reply->data_size);
                spinlock_acquire(&drives[port_i]->lock);
            } else {
                // If we received the wrong number of bytes, signal an error and mark request as failed
                spinlock_acquire(&drives[port_i]->lock);
                message_free(issued_request->reply);
                message_reply_error(issued_request->message, ERR_IO_INTERNAL);
                message_free(issued_request->message);
                issued_request->failed = true;
            }
            // Free data buffer page
            page_free(command_table->region[0].data_base);
            // Decrement number of outstanding commands
            // If this is the last one, send a reply
            issued_request->outstanding_commands--;
            if (issued_request->outstanding_commands == 0) {
                // Don't send a reply if the request has failed
                if (!issued_request->failed) {
                    message_reply(issued_request->message, issued_request->reply);
                    message_free(issued_request->message);
                }
                free(issued_request);
            }
            // Mark command slot as free
            drives[port_i]->commands_issued &= ~(UINT32_C(1) << slot_i);
            if (drives[port_i]->receive_thread_blocked) {
                drives[port_i]->receive_thread_blocked = false;
                process_enqueue(drives[port_i]->receive_thread);
            }
            spinlock_release(&drives[port_i]->lock);
        }
    }
}

void drive_process_irq(void) {
    // Wake up the blocked reply threads for every port that has an interrupt pending
    u32 interrupt_status = hba->interrupt_status & ports_connected;
    for (u32 port_i = 0; port_i < 32; port_i++) {
        if (!((interrupt_status >> port_i) & 1))
            continue;
        spinlock_acquire(&drives[port_i]->lock);
        if (drives[port_i]->reply_thread_blocked) {
            drives[port_i]->reply_thread_blocked = false;
            process_enqueue(drives[port_i]->reply_thread);
        } else {
            drives[port_i]->reply_thread_repeat = true;
        }
        spinlock_release(&drives[port_i]->lock);
    }
}

MessageQueue *ahci_main_mqueue;
Channel *drive_open_channel;

_Noreturn void ahci_main_kernel_thread_main(void) {
    err_t err;
    while (1) {
        Message *message;
        // Get message from user process
        mqueue_receive(ahci_main_mqueue, &message, false, false, TIMEOUT_NONE);
        // Check message size
        if (message->data_size != sizeof(PhysDriveOpenArgs) || message->handles_size != 0) {
            err = ERR_INVALID_ARG;
            goto fail;
        }
        PhysDriveOpenArgs *args = (PhysDriveOpenArgs *)message->data;
        if (args->drive_id > user_drive_num) {
            err = ERR_DOES_NOT_EXIST;
            goto fail;
        }
        u32 port_i = user_drive_port[args->drive_id];
        FileRange *bounds = malloc(sizeof(FileRange));
        if (bounds == NULL) {
            err = ERR_NO_MEMORY;
            goto fail;
        }
        bounds->offset = args->offset;
        bounds->length = args->length;
        Message *reply = message_alloc(0);
        if (reply == NULL) {
            err = ERR_NO_MEMORY;
            goto fail_reply_alloc;
        }
        Channel *drive_channel = channel_alloc();
        if (drive_channel == NULL) {
            err = ERR_NO_MEMORY;
            goto fail_channel_alloc;
        }
        reply->handles = malloc(sizeof(AttachedHandle));
        if (reply->handles == NULL) {
            err = ERR_NO_MEMORY;
            goto fail_handles_alloc;
        }
        channel_set_mqueue(drive_channel, drives[port_i]->queue, (MessageTag){0, (u64)bounds});
        reply->handles_size = 1;
        reply->handles[0] = (AttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_SEND, {.channel = drive_channel}};
        message_reply(message, reply);
        message_free(message);
        continue;
fail_handles_alloc:
        channel_del_ref(drive_channel);
fail_channel_alloc:
        message_free(reply);
fail_reply_alloc:
        free(bounds);
fail:
        message_reply_error(message, err);
        message_free(message);
    }
}
