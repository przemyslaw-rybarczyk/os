#include "types.h"
#include "pci.h"

#define VENDOR_ID_INVALID 0xFFFF
#define CLASS_SUBCLASS_SATA 0x0106
#define CLASS_SUBCLASS_PCI_BRIDGE 0x0604
#define HEADER_TYPE_GENERAL 0x00
#define HEADER_TYPE_PCI_BRIDGE 0x01

u32 ahci_base;

// Read a byte from the PCI configuration space
static u32 pci_read_u32(u32 address) {
    u32 data;
    // Write the address to port 0xCF8 and read data from port 0xCFC
    asm ("mov dx, 0x0CF8; out dx, eax; mov dx, 0x0CFC; in eax, dx"
        : "=a"(data) : "a"(address) : "rdx"
    );
    return data;
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
            // If the device is an AHCI controller, get the base address from the BAR5 field
            ahci_base = pci_read_u32(base + 0x24);
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
            if (function == 0)
                return ERR_KERNEL_OTHER;
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
    if (ahci_base == 0)
        return ERR_KERNEL_OTHER;
    return 0;
}
