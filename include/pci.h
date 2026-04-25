// PCI bus driver header
// PCI config space access, device enumeration, power management

#ifndef PCI_H
#define PCI_H

#include "types.h"

// PCI config space register offsets
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_CLASS_REV       0x08
#define PCI_CACHE_LINE_SIZE 0x0C  // Cache Line Size (8-bit at offset 0x0C)
#define PCI_LATENCY_TIMER   0x0D  // Latency Timer (8-bit at offset 0x0D)
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_CAP_PTR         0x34  // Capabilities Pointer
#define PCI_INTERRUPT       0x3C

// PCI command register flags
#define PCI_CMD_IO          0x0001
#define PCI_CMD_MEM         0x0002
#define PCI_CMD_MASTER      0x0004
#define PCI_CMD_SERR        0x0100
#define PCI_CMD_INTDIS      0x0400

// PCI capability IDs
#define PCI_CAP_PM          0x01  // Power Management
#define PCI_CAP_MSI         0x05  // Message Signaled Interrupts

// PCI config space I/O ports
#define PCI_CONFIG_ADDR     0x0CF8
#define PCI_CONFIG_DATA     0x0CFC

// USB controller class codes
#define PCI_CLASS_SERIAL    0x0C
#define PCI_SUBCLASS_USB    0x03
#define PCI_PROGIF_EHCI     0x20

// PCI device info structure
struct pci_device {
  uint bus;
  uint dev;
  uint func;
  ushort vendor_id;
  ushort device_id;
  uchar irq;
  uint bar0;
};

// PCI function declarations
void   pciinit(void);
uint   pci_read32(uint bus, uint dev, uint func, uint offset);
ushort pci_read16(uint bus, uint dev, uint func, uint offset);
void   pci_write32(uint bus, uint dev, uint func, uint offset, uint val);
void   pci_write16(uint bus, uint dev, uint func, uint offset, ushort val);
int    pci_find_ehci(struct pci_device *devs, int maxdevs);
uint   pci_find_cap(uint bus, uint dev, uint func, uchar cap_id);
int    pci_set_power_d0(uint bus, uint dev, uint func);
void   pci_setup_device(uint bus, uint dev, uint func);
#endif
