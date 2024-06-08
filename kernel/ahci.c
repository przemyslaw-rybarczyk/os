#include "types.h"
#include "ahci.h"

#include "alloc.h"
#include "channel.h"
#include "page.h"
#include "process.h"
#include "spinlock.h"
#include "string.h"

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

volatile HBA *hba = (volatile HBA *)AHCI_MAPPING_AREA;

static u32 command_slots_max;
static CommandHeader (*command_lists)[32];
static CommandTable *command_tables;

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

// Bit set for each connected port
static u32 ports_connected;

// Record for each command slot
static IssuedCommand *issued_commands;

// Sector size and count for each drive
static u64 drive_sector_size[32];
static u64 drive_sector_count[32];

// Bit set if drive supports LBA48
static u32 drive_is_lba48;

// Message queue for each port
static MessageQueue *port_queue[32];

// Number of drives shown to userspace and size of each one
static u32 user_drive_num = 0;
static size_t user_drive_size[32];

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
    if (!(hba->capabilities & HBA_CAP_64_BIT_ADDR))
        return ERR_KERNEL_OTHER;
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
    command_lists = (CommandHeader (*)[32])(AHCI_MAPPING_AREA + 2 * PAGE_SIZE);
    command_tables = (CommandTable *)(u64)(command_lists + ports_connected_num);
    // Allocate buffer for results of IDENTIFY DEVICE command
    u64 identify_buffer_page = page_alloc();
    if (identify_buffer_page == 0)
        return ERR_KERNEL_NO_MEMORY;
    // Initialize connected ports
    u32 ports_initialized = 0;
    for (u32 port_i = 0; port_i < 32; port_i++) {
        // Skip if port not connected
        if (!((ports_connected >> port_i) & 1))
            continue;
        u32 conn_i = ports_initialized++;
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
            size_t command_table_offset = ports_connected_num * 1024 + (conn_i * command_slots_max + j) * 256;
            command_lists[conn_i][j].command_table = ahci_pages[command_table_offset / PAGE_SIZE] + command_table_offset % PAGE_SIZE;
        }
        // Set command list and FIS base to their physical addresses
        size_t command_list_offset = conn_i * 1024;
        size_t fis_offset = ports_connected_num * (1024 + command_slots_max * 256) + conn_i * 256;
        hba->ports[port_i].command_list_base = ahci_pages[command_list_offset / PAGE_SIZE] + command_list_offset % PAGE_SIZE;
        hba->ports[port_i].fis_base = ahci_pages[fis_offset / PAGE_SIZE] + fis_offset % PAGE_SIZE;
        // Reenable FIS receive and command list processing
        hba->ports[port_i].command_status |= PORT_CMD_FIS_RECEIVE_ENABLE;
        hba->ports[port_i].command_status |= PORT_CMD_START;
        // Clear SATA error and interrupt status registers
        hba->ports[port_i].sata_error = UINT32_C(-1);
        hba->ports[port_i].interrupt_status = UINT32_C(-1);
        // Construct IDENTIFY DEVICE command in command slot 0
        command_tables[conn_i * command_slots_max].command_fis.fis_type = FIS_TYPE_HOST_TO_DEVICE;
        command_tables[conn_i * command_slots_max].command_fis.flags = FIS_FLAGS_COMMAND;
        command_tables[conn_i * command_slots_max].command_fis.command = FIS_COMMAND_IDENTIFY_DEVICE;
        command_tables[conn_i * command_slots_max].region[0].data_base = identify_buffer_page;
        command_tables[conn_i * command_slots_max].region[0].byte_count = 511;
        command_lists[conn_i][0].table_length = 1;
        command_lists[conn_i][0].flags = COMMAND_LIST_FIS_LENGTH;
        // Send command and wait for it to be processed
        hba->ports[port_i].command_issue = 1;
        while (hba->ports[port_i].command_issue & 1) {
            while (!hba->ports[port_i].interrupt_status)
                ;
            if (hba->ports[port_i].interrupt_status & PORT_INT_ERROR_ANY)
                goto fail;
        }
        // Clear SATA error and interrupt status registers again
        hba->ports[port_i].sata_error = UINT32_C(-1);
        hba->ports[port_i].interrupt_status = UINT32_C(-1);
        u16 *identify_buffer = PHYS_ADDR(identify_buffer_page);
        // Check transferred byte count for underflow
        if (command_lists[conn_i][0].byte_count != 512)
            goto fail;
        // Check for LBA and DMA in capabilities
        if (!(identify_buffer[IDENTIFY_CAP] & (IDENTIFY_CAP_LBA | IDENTIFY_CAP_DMA)))
            goto fail;
        // Get logical sector size in bytes
        if ((identify_buffer[IDENTIFY_SECTOR_SIZE_FLAGS] & IDENTIFY_FIELD_VALID_MASK) == IDENTIFY_FIELD_VALID
                && (identify_buffer[IDENTIFY_SECTOR_SIZE_FLAGS] & IDENTIFY_SECTOR_SIZE_FLAGS_LOGICAL_SIZE_SUPPORTED))
            drive_sector_size[conn_i] =
                2 * ((u32)identify_buffer[IDENTIFY_LOGICAL_SECTOR_SIZE]
                    | ((u32)identify_buffer[IDENTIFY_LOGICAL_SECTOR_SIZE + 1] << 16));
        else
            drive_sector_size[conn_i] = 512;
        // Require sector size to be power of two fitting in page
        if (drive_sector_size[conn_i] == 0 || drive_sector_size[conn_i] > PAGE_SIZE
                || (drive_sector_size[conn_i] & (drive_sector_size[conn_i] - 1)) != 0)
            goto fail;
        // Determine if drive supports LBA48
        bool is_lba48 = (identify_buffer[IDENTIFY_COM_SUP_2] & IDENTIFY_FIELD_VALID_MASK) == IDENTIFY_FIELD_VALID
            && (identify_buffer[IDENTIFY_COM_SUP_2] & IDENTIFY_COM_SUP_2_LBA_48);
        drive_is_lba48 |= (u32)is_lba48 << conn_i;
        // Get logical sector count
        if (is_lba48)
            drive_sector_count[conn_i] =
                (u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48]
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 1] << 16)
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 2] << 32)
                | ((u64)identify_buffer[IDENTIFY_SECTOR_COUNT_48 + 3] << 48);
        else
            drive_sector_count[conn_i] =
                (u32)identify_buffer[IDENTIFY_SECTOR_COUNT_28]
                | ((u32)identify_buffer[IDENTIFY_SECTOR_COUNT_28 + 1] << 16);
        if (drive_sector_count[conn_i] == 0)
            goto fail;
        // Allocate message queue
        port_queue[conn_i] = mqueue_alloc();
        if (port_queue[conn_i] == NULL)
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
        user_drive_size[user_drive_num] = drive_sector_size[conn_i] * drive_sector_count[conn_i];
        user_drive_num++;
        // Enable interrupts for port
        hba->ports[port_i].interrupt_enable = UINT32_C(-1);
        continue;
fail:
        // Set sector size to zero to indicate that the port failed to initialize
        drive_sector_size[conn_i] = 0;
    }
    page_free(identify_buffer_page);
    // Clear interrupts
    hba->interrupt_status = UINT32_C(-1);
    // Allocate memory for issued commands
    issued_commands = malloc(sizeof(IssuedCommand) * ports_connected_num * command_slots_max);
    if (issued_commands == NULL)
        return ERR_NO_MEMORY;
    return 0;
}

// Lock for the variables relating to each port
static spinlock_t port_lock[32];

// Pointers to the two threads created for each drive
static Process *drive_receive_thread[32];
static Process *drive_reply_thread[32];

// Set if receive thread is waiting for a command slot to be freed up
static bool receive_thread_blocked[32];

// Set if reply thread is waiting a command to be completed
static bool reply_thread_blocked[32];

// Set if receive thread should check slot status again instead of blocking
static bool receive_thread_repeat[32];

// Set if reply thread should check drive status again instead of blocking
static bool reply_thread_repeat[32];

// Bitmask for commands that have been issued for a given port
u32 port_commands_issued[32];

// Used to initialize connection number for port threads
static volatile _Atomic u32 ahci_receive_threads_initialized = 0;
static volatile _Atomic u32 ahci_reply_threads_initialized = 0;

// Get port numbers from drive ID
static err_t get_port_number(u32 drive_id, u32 *conn_i_ptr, u32 *port_i_ptr) {
    u32 ports_skipped = 0;
    u32 conn_i = 0;
    for (u32 port_i = 0; port_i < 32; port_i++) {
        if (!((ports_connected >> port_i) & 1))
            continue;
        if (drive_sector_size[conn_i] == 0) {
            conn_i++;
            continue;
        }
        if (ports_skipped++ == drive_id) {
            *conn_i_ptr = conn_i;
            *port_i_ptr = port_i;
            return 0;
        }
        conn_i++;
    }
    return ERR_INVALID_ARG;
}

_Noreturn void ahci_drive_receive_kernel_thread_main(void) {
    err_t err;
    // Get port number
    u32 conn_i, port_i;
    get_port_number(atomic_fetch_add(&ahci_receive_threads_initialized, 1), &conn_i, &port_i);
    u32 sectors_per_page = PAGE_SIZE / drive_sector_size[conn_i];
    drive_receive_thread[conn_i] = cpu_local->current_process;
    while (1) {
        Message *message;
        // Get message from user process
        mqueue_receive(port_queue[conn_i], &message, false, false, TIMEOUT_NONE);
        if (message->data_size != 2 * sizeof(u64) || message->handles_size != 0) {
            err = ERR_INVALID_ARG;
            goto fail;
        }
        u64 offset = *(u64 *)message->data;
        u64 length = *(u64 *)(message->data + sizeof(u64));
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
        if (offset + length > drive_sector_size[conn_i] * drive_sector_count[conn_i] || offset + length < offset) {
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
            spinlock_acquire(&port_lock[port_i]);
            // Get next empty slot
            u32 slot_i;
            while (1) {
                for (slot_i = 0; slot_i < command_slots_max; slot_i++)
                    if (!((port_commands_issued[conn_i] >> slot_i) & 1))
                        goto slot_found;
                if (receive_thread_repeat[conn_i]) {
                    receive_thread_repeat[conn_i] = false;
                } else {
                    // Block if no free slots available
                    receive_thread_blocked[conn_i] = true;
                    process_block(&port_lock[port_i]);
                    spinlock_acquire(&port_lock[port_i]);
                }
            }
slot_found:;
            // Construct request
            CommandHeader *command_header = &command_lists[conn_i][slot_i];
            CommandTable *command_table = &command_tables[conn_i * command_slots_max + slot_i];
            bool is_lba48 = (bool)((drive_is_lba48 >> conn_i) & 1);
            u64 lba = (offset_page + i) * sectors_per_page;
            command_table->command_fis.fis_type = FIS_TYPE_HOST_TO_DEVICE;
            command_table->command_fis.flags = FIS_FLAGS_COMMAND;
            command_table->command_fis.command = is_lba48 ? FIS_COMMAND_READ_DMA_EXT : FIS_COMMAND_READ_DMA;
            command_table->command_fis.device = is_lba48 ? (1 << 6) : 0;
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
            IssuedCommand *issued_command = &issued_commands[conn_i * command_slots_max + slot_i];
            issued_command->request = issued_request;
            issued_command->offset = (offset_page + i) * PAGE_SIZE - offset;
            // Mark command as issued internally
            port_commands_issued[conn_i] |= UINT32_C(1) << slot_i;
            spinlock_release(&port_lock[port_i]);
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
    u32 conn_i, port_i;
    get_port_number(atomic_fetch_add(&ahci_reply_threads_initialized, 1), &conn_i, &port_i);
    drive_reply_thread[port_i] = cpu_local->current_process;
    while (1) {
        // Block until an interrupt from the drive
        spinlock_acquire(&port_lock[port_i]);
        if (reply_thread_repeat[port_i]) {
            reply_thread_repeat[port_i] = false;
        } else {
            reply_thread_blocked[port_i] = true;
            process_block(&port_lock[port_i]);
            spinlock_acquire(&port_lock[port_i]);
        }
        // Get interrupt status and clear it
        u32 interrupt_status = hba->ports[port_i].interrupt_status;
        hba->ports[port_i].interrupt_status = interrupt_status;
        hba->interrupt_status = UINT32_C(1) << port_i;
        // Go over all commands that have been completed
        u32 commands_completed = port_commands_issued[conn_i] & ~hba->ports[port_i].command_issue;
        spinlock_release(&port_lock[port_i]);
        for (u32 slot_i = 0; slot_i < command_slots_max; slot_i++) {
            if (!((commands_completed >> slot_i) & 1))
                continue;
            CommandHeader *command_header = &command_lists[conn_i][slot_i];
            CommandTable *command_table = &command_tables[conn_i * command_slots_max + slot_i];
            void *buffer = PHYS_ADDR(command_table->region[0].data_base);
            IssuedCommand *issued_command = &issued_commands[conn_i * command_slots_max + slot_i];
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
                spinlock_acquire(&port_lock[port_i]);
            } else {
                // If we received the wrong number of bytes, signal an error and mark request as failed
                spinlock_acquire(&port_lock[port_i]);
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
            port_commands_issued[conn_i] &= ~(UINT32_C(1) << slot_i);
            if (receive_thread_blocked[conn_i]) {
                receive_thread_blocked[conn_i] = false;
                process_enqueue(drive_receive_thread[conn_i]);
            }
            spinlock_release(&port_lock[port_i]);
        }
    }
}

void drive_process_irq(void) {
    // Wake up the blocked reply threads for every port that has an interrupt pending
    u32 interrupt_status = hba->interrupt_status & ports_connected;
    for (u32 port_i = 0; port_i < 32; port_i++) {
        if (!((interrupt_status >> port_i) & 1))
            continue;
        spinlock_acquire(&port_lock[port_i]);
        if (reply_thread_blocked[port_i]) {
            reply_thread_blocked[port_i] = false;
            process_enqueue(drive_reply_thread[port_i]);
        } else {
            reply_thread_repeat[port_i] = true;
        }
        spinlock_release(&port_lock[port_i]);
    }
}

MessageQueue *ahci_main_mqueue;
Channel *drive_info_channel;
Channel *drive_open_channel;

_Noreturn void ahci_main_kernel_thread_main(void) {
    err_t err;
    while (1) {
        Message *message;
        // Get message from user process
        mqueue_receive(ahci_main_mqueue, &message, false, false, TIMEOUT_NONE);
        switch (message->tag.data[0]) {
        case AHCI_MAIN_TAG_DRIVE_INFO: {
            // Check message size
            if (message->data_size != 0 || message->handles_size != 0) {
                err = ERR_INVALID_ARG;
                goto fail;
            }
            Message *reply = message_alloc_copy(user_drive_num * sizeof(size_t), user_drive_size);
            if (reply == NULL) {
                err = ERR_NO_MEMORY;
                goto fail;
            }
            message_reply(message, reply);
            message_free(message);
            break;
        }
        case AHCI_MAIN_TAG_DRIVE_OPEN: {
            // Check message size
            if (message->data_size != sizeof(size_t) || message->handles_size != 0) {
                err = ERR_INVALID_ARG;
                goto fail;
            }
            size_t drive_id = *(size_t *)message->data;
            u32 port_i, conn_i;
            err = get_port_number(drive_id, &port_i, &conn_i);
            if (err) {
                err = ERR_DOES_NOT_EXIST;
                goto fail;
            }
            Message *reply = message_alloc(0);
            if (reply == NULL) {
                err = ERR_NO_MEMORY;
                goto fail;
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
            reply->handles_size = 1;
            channel_set_mqueue(drive_channel, port_queue[conn_i], (MessageTag){conn_i, 0});
            channel_add_ref(drive_channel);
            reply->handles[0] = (AttachedHandle){ATTACHED_HANDLE_TYPE_CHANNEL_SEND, {.channel = drive_channel}};
            message_reply(message, reply);
            message_free(message);
            break;
fail_handles_alloc:
            channel_del_ref(drive_channel);
fail_channel_alloc:
            message_free(reply);
fail:
            message_reply_error(message, err);
            message_free(message);
        }
        }
    }
}
