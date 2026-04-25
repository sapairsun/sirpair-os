// PCI bus driver
// Provides PCI config space access, EHCI controller discovery,
// and power management.
//
// Based on reference code proven on ThinkPad X220.
// NOTE: NO VT-d handling needed! The reference code works on X220
// without any VT-d manipulation. DMA buffers in static BSS work
// because BIOS VT-d (if active) allows DMA from kernel memory.

#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "pci.h"

// ============================================================================
// PCI Config Space Access
// ============================================================================

static uint
pci_addr(uint bus, uint dev, uint func, uint offset)
{
  return (1 << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
}

uint
pci_read32(uint bus, uint dev, uint func, uint offset)
{
  outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
  return inl(PCI_CONFIG_DATA);
}

void
pci_write32(uint bus, uint dev, uint func, uint offset, uint val)
{
  outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
  outl(PCI_CONFIG_DATA, val);
}

ushort
pci_read16(uint bus, uint dev, uint func, uint offset)
{
  uint val = pci_read32(bus, dev, func, offset & ~3);
  return (ushort)(val >> ((offset & 2) * 8));
}

void
pci_write16(uint bus, uint dev, uint func, uint offset, ushort val)
{
  uint aligned = offset & ~3;
  int shift = (offset & 2) * 8;
  uint old = pci_read32(bus, dev, func, aligned);
  uint mask = 0xFFFF << shift;
  old = (old & ~mask) | ((uint)val << shift);
  pci_write32(bus, dev, func, aligned, old);
}

static void
pci_write8(uint bus, uint dev, uint func, uint offset, uchar val)
{
  uint aligned = offset & ~3;
  int shift = (offset & 3) * 8;
  uint old = pci_read32(bus, dev, func, aligned);
  uint mask = 0xFF << shift;
  old = (old & ~mask) | ((uint)val << shift);
  pci_write32(bus, dev, func, aligned, old);
}

static uchar
pci_read8(uint bus, uint dev, uint func, uint offset)
{
  uint val = pci_read32(bus, dev, func, offset & ~3);
  return (uchar)(val >> ((offset & 3) * 8));
}

// ============================================================================
// PCI Capability and Power Management
// ============================================================================

uint
pci_find_cap(uint bus, uint dev, uint func, uchar cap_id)
{
  uint status = pci_read16(bus, dev, func, PCI_STATUS);
  if(!(status & 0x10))
    return 0;

  uint ptr = pci_read8(bus, dev, func, PCI_CAP_PTR) & 0xFC;
  int limit = 48;
  while(ptr >= 0x40 && limit > 0){
    uchar id = pci_read8(bus, dev, func, ptr);
    if(id == cap_id)
      return ptr;
    ptr = pci_read8(bus, dev, func, ptr + 1) & 0xFC;
    limit--;
  }
  return 0;
}

int
pci_set_power_d0(uint bus, uint dev, uint func)
{
  uint pm_cap = pci_find_cap(bus, dev, func, PCI_CAP_PM);
  if(!pm_cap)
    return 0;

  ushort pmcsr = pci_read16(bus, dev, func, pm_cap + 4);
  int old_state = pmcsr & 0x03;

  if(old_state != 0){
    pmcsr &= ~0x03;
    pci_write16(bus, dev, func, pm_cap + 4, pmcsr);
    volatile int i;
    for(i = 0; i < 10000000; i++)
      ;
  }
  return old_state;
}

void
pci_setup_device(uint bus, uint dev, uint func)
{
  pci_set_power_d0(bus, dev, func);
  pci_write8(bus, dev, func, PCI_CACHE_LINE_SIZE, 16);
  pci_write8(bus, dev, func, PCI_LATENCY_TIMER, 64);

  uint cmd = pci_read32(bus, dev, func, PCI_COMMAND);
  uint newcmd = cmd;
  newcmd |= PCI_CMD_MEM | PCI_CMD_MASTER;
  newcmd &= ~PCI_CMD_INTDIS;
  newcmd &= ~PCI_CMD_SERR;
  pci_write32(bus, dev, func, PCI_COMMAND, newcmd);

  uint status = pci_read16(bus, dev, func, PCI_STATUS);
  if(status & 0xF900)
    pci_write16(bus, dev, func, PCI_STATUS, status & 0xF900);
}

// ============================================================================
// EHCI Controller Discovery
// ============================================================================

int
pci_find_ehci(struct pci_device *devs, int maxdevs)
{
  uint bus, dev, func;
  int count = 0;

  for(bus = 0; bus < 256 && count < maxdevs; bus++){
    for(dev = 0; dev < 32 && count < maxdevs; dev++){
      for(func = 0; func < 8 && count < maxdevs; func++){
        ushort vendor = pci_read16(bus, dev, func, PCI_VENDOR_ID);
        if(vendor == 0xFFFF)
          continue;

        uint classrev = pci_read32(bus, dev, func, PCI_CLASS_REV);
        uchar class = (classrev >> 24) & 0xFF;
        uchar subclass = (classrev >> 16) & 0xFF;
        uchar progif = (classrev >> 8) & 0xFF;

        if(class == PCI_CLASS_SERIAL &&
           subclass == PCI_SUBCLASS_USB &&
           progif == PCI_PROGIF_EHCI){
          struct pci_device *p = &devs[count];
          p->bus = bus;
          p->dev = dev;
          p->func = func;
          p->vendor_id = vendor;
          p->device_id = pci_read16(bus, dev, func, PCI_DEVICE_ID);
          p->bar0 = pci_read32(bus, dev, func, PCI_BAR0) & ~0xF;
          p->irq = pci_read32(bus, dev, func, PCI_INTERRUPT) & 0xFF;

          pci_setup_device(bus, dev, func);
          count++;
        }

        if(func == 0){
          uint hdr = pci_read32(bus, dev, func, 0x0C);
          if(!((hdr >> 16) & 0x80))
            break;
        }
      }
    }
  }
  return count;
}

// ============================================================================
// PCI Initialization
//
// NOTE: NO VT-d handling is needed!
// The reference code (proven on ThinkPad X220) has NO VT-d code at all.
// With VT-d disabled in BIOS, EHCI DMA works normally to any address.
// Previous VT-d code was HARMFUL: it probed 0xFED40000 (TPM, not VT-d!),
// corrupted TPM state, and caused ALL subsequent DMA to fail.
// ============================================================================

void
pciinit(void)
{
  // PCI bus scan
}
