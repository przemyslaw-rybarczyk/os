#include "types.h"
#include "pci.h"

#include "framebuffer.h"

#define VENDOR_ID_INVALID 0xFFFF
#define CLASS_SUBCLASS_SATA 0x0106
#define CLASS_SUBCLASS_PCI_BRIDGE 0x0604
#define HEADER_TYPE_GENERAL 0x00
#define HEADER_TYPE_PCI_BRIDGE 0x01
#define CAPABILITY_ID_MSI 0x05

#define COMMAND_INTERRUPT_DISABLE (UINT32_C(1) << 10)
#define COMMAND_BUS_MASTER_ENABLE (UINT32_C(1) << 2)
#define COMMAND_MEMORY_SPACE_ENABLE (UINT32_C(1) << 1)
#define STATUS_CAPABILITIES_LIST (UINT32_C(1) << 20)
#define MSI_CONTROL_ENABLE (UINT32_C(1) << 16)
#define MSI_CONTROL_64_BIT (UINT32_C(1) << 23)

#define MSI_ADDR_BASE UINT32_C(0xFEE00000)
#define MSI_ADDR_DESTINATION_ALL (UINT32_C(0xFF) << 12)
#define MSI_ADDR_REDIRECTION_HINT (UINT32_C(1) << 3)
#define MSI_ADDR_DESTINATION_LOGICAL (UINT32_C(1) << 2)
#define MSI_DATA_DELIVERY_LOWEST_PRIORITY (UINT32_C(1) << 8)

#define INT_VECTOR_AHCI 0x23

u32 ahci_base;

// Read a u32 from the PCI configuration space
static u32 pci_read_u32(u32 address) {
    u32 data;
    asm (
            // Set address by writing to port 0xCF8
            "mov dx, 0x0CF8;"
            "out dx, eax;"
            // Get data by reading from port 0xCFC
            "mov dx, 0x0CFC;"
            "in eax, dx;"
        : "=a"(data) : "a"(address) : "rdx"
    );
    return data;
}

// Write a u32 to the PCI configuration space
static void pci_write_u32(u32 address, u32 value) {
    asm (
            // Set address by writing to port 0xCF8
            "mov dx, 0x0CF8;"
            "out dx, eax;"
            // Write data to port 0xCFC
            "mov dx, 0x0CFC;"
            "mov eax, %[v];"
            "out dx, eax;"
        : : "a"(address), [v] "g"(value) : "rdx"
    );
}

// Scan all PCI devices starting from a given device
static void pci_check_device(u32 bus, u32 device) {
    // Go over every function of the device
    for (u32 function = 0; function < 8; function++) {
        u32 base = UINT32_C(0x80000000) | (bus << 16) | (device << 11) | (function << 8);
        // Skip if the vendor ID is not valid
        u16 vendor_id = (u16)pci_read_u32(base + 0x00);
        if (vendor_id == VENDOR_ID_INVALID) {
            if (function == 0)
                break;
            continue;
        }
        // Get class, subclass, and header type
        u16 class_subclass = (u16)(pci_read_u32(base + 0x08) >> 16);
        u8 header_type = (u8)(pci_read_u32(base + 0x0C) >> 16);
        bool multiple_functions = (header_type) & 0x80;
        header_type &= 0x7F;
        // Check for specific device types
        if (class_subclass == CLASS_SUBCLASS_PCI_BRIDGE && header_type == HEADER_TYPE_PCI_BRIDGE) {
            // If the device is a PCI-to-PCI bridge, get the secondary bus number and scan all devices under it
            u8 bus_ = (u8)(pci_read_u32(base + 0x18) >> 8);
            for (u32 device_ = 0; device_ < 32; device_++)
                pci_check_device(bus_, device_);
        } else if (class_subclass == CLASS_SUBCLASS_SATA && header_type == HEADER_TYPE_GENERAL && ahci_base == 0) {
            // Search for MSI capability in capability list
            u8 msi_offset = 0;
            u32 msi_data_0;
            if (pci_read_u32(base + 0x04) & STATUS_CAPABILITIES_LIST) {
                u8 cap_offset = (u8)pci_read_u32(base + 0x34);
                while (cap_offset != 0) {
                    u32 cap_data_0 = pci_read_u32(base + cap_offset);
                    u8 cap_id = (u8)cap_data_0;
                    if (cap_id == CAPABILITY_ID_MSI) {
                        msi_offset = cap_offset;
                        msi_data_0 = cap_data_0;
                        break;
                    }
                    cap_offset = (u8)(cap_data_0 >> 8);
                }
            }
            if (msi_offset != 0) {
                // If the device is an AHCI controller, get the base address from the BAR5 field
                ahci_base = pci_read_u32(base + 0x24);
                // Set MSI message address and data
                u32 msi_msg_addr = MSI_ADDR_BASE | MSI_ADDR_DESTINATION_ALL | MSI_ADDR_REDIRECTION_HINT | MSI_ADDR_DESTINATION_LOGICAL;
                pci_write_u32(base + msi_offset + 0x04, msi_msg_addr);
                if (msi_data_0 & MSI_CONTROL_64_BIT)
                    pci_write_u32(base + msi_offset + 0x08, 0);
                u32 msi_msg_data_addr = base + msi_offset + (msi_data_0 & MSI_CONTROL_64_BIT ? 0x0C : 0x08);
                u32 msi_msg_data = MSI_DATA_DELIVERY_LOWEST_PRIORITY | INT_VECTOR_AHCI;
                pci_write_u32(msi_msg_data_addr, (pci_read_u32(msi_msg_data_addr) & UINT32_C(0xFFFF0000)) | msi_msg_data);
                // Enable MSI
                pci_write_u32(base + msi_offset, msi_data_0 | MSI_CONTROL_ENABLE);
                // Enable interrupts, bus master, and memory space in the command register
                pci_write_u32(base + 0x04, (pci_read_u32(base + 0x04) & ~COMMAND_INTERRUPT_DISABLE) | COMMAND_BUS_MASTER_ENABLE | COMMAND_MEMORY_SPACE_ENABLE | 0xFF00);
            }
        }
        // If the device reports having only one function, don't check the other ones
        if (function == 0 && !multiple_functions)
            break;
    }
}

// Scan all PCI devices
err_t pci_init(void) {
    // Device 0 at bus 0 is the host bridge.
    // It may have multiple functions, so we check each one.
    for (u32 function = 0; function < 8; function++) {
        u32 base = UINT32_C(0x80000000) | (function << 8);
        // Skip if the vendor ID is not valid
        u16 vendor_id = (u16)pci_read_u32(base);
        if (vendor_id == VENDOR_ID_INVALID) {
            if (function == 0) {
                print_string("Could not find AHCI controller\n");
                return ERR_KERNEL_OTHER;
            }
            continue;
        }
        // Check all devices under this bus
        // The bus number is the function number.
        for (u32 device = 0; device < 32; device++)
            pci_check_device(function, device);
        if (function == 0) {
            // If the device reports having only one function, don't check the other ones
            bool multiple_functions = (pci_read_u32(base + 0x0C) >> 16) & 0x80;
            if (!multiple_functions)
                break;
        }
    }
    // Return error if AHCI controller was not found
    if (ahci_base == 0) {
        print_string("Could not find AHCI controller\n");
        return ERR_KERNEL_OTHER;
    }
    return 0;
}
