// Multiprocessor support
// Search memory for MP description structures.
// http://developer.intel.com/design/pentium/datashts/24201606.pdf
// Also parses ACPI MADT as fallback for QEMU (which only lists BSP in MP table).

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mp.h"
#include "x86.h"
#include "mmu.h"
#include "proc.h"

struct cpu cpus[NCPU];
static struct cpu *bcpu;
int ismp;
int ncpu;
uchar ioapicid;

// ACPI table structures for MADT parsing
struct acpi_rsdp {
  uchar signature[8];    // "RSD PTR "
  uchar checksum;
  uchar oemid[6];
  uchar revision;
  uint rsdtaddr;
};

struct acpi_header {
  uchar signature[4];
  uint length;
  uchar revision;
  uchar checksum;
  uchar oemid[6];
  uchar oemtableid[8];
  uint oemrevision;
  uint creatorid;
  uint creatorrevision;
};

struct acpi_madt {
  struct acpi_header header;
  uint lapicaddr;
  uint flags;
};

// MADT entry types
#define MADT_LAPIC    0
#define MADT_IOAPIC   1

struct madt_lapic {
  uchar type;
  uchar length;
  uchar acpi_id;
  uchar apic_id;
  uint flags;
};

struct madt_ioapic {
  uchar type;
  uchar length;
  uchar ioapic_id;
  uchar reserved;
  uint addr;
  uint gsi_base;
};

// entrypgdir is the boot page table with 4MB pages; defined in main.c.
extern pde_t entrypgdir[];
extern pde_t *kpgdir;

// Temporary mapping window for ACPI tables at high physical addresses.
// PDE 959 maps VA 0xEFC00000-0xEFFFFFFF (4MB window just below DEVSPACE).
// This is in kernel space (above KERNBASE) so it's safe for setupkvm cloning.
#define ACPI_TEMP_VA    0xEFC00000
#define ACPI_TEMP_PDE   (ACPI_TEMP_VA >> PDXSHIFT)
static uint acpi_temp_pa_base;  // currently mapped 4MB physical base

// Temporarily map a physical address into kernel virtual space for reading
// ACPI tables during boot. Handles all physical address ranges correctly:
//   PA < phystop:                          already mapped via kmap (P2V)
//   PA in [phystop, DEVSPACE-KERNBASE):    use KERNBASE+PA (safe, no overflow)
//   PA >= DEVSPACE:                        already mapped via DEVSPACE identity map
//   PA in [DEVSPACE-KERNBASE, DEVSPACE):   use temporary 4MB window at PDE 959
// Returns a kernel virtual pointer, or 0 on failure.
void*
acpi_map_phys(uint pa)
{
  extern uint phystop;
  uint ptop = phystop ? phystop : PHYSTOP;

  // Range 1: Low memory (below phystop) - already mapped by kmap
  if(pa < ptop)
    return (void*)P2V(pa);

  // Range 2: DEVSPACE (0xF0000000+) - identity mapped by kmap
  if(pa >= DEVSPACE)
    return (void*)pa;

  // Range 3: PA in [phystop, DEVSPACE-KERNBASE) where KERNBASE+PA doesn't
  // overflow 32-bit and doesn't collide with DEVSPACE mapping.
  // DEVSPACE - KERNBASE = 0xF0000000 - 0x80000000 = 0x70000000
  if(pa < (DEVSPACE - KERNBASE)){
    uint va = KERNBASE + pa;
    uint pa_base = pa & ~0x3FFFFF;  // 4MB aligned
    uint pde_idx = va >> PDXSHIFT;

    if(kpgdir[pde_idx] & PTE_P){
      // PDE exists - check if the specific PTE is also present
      // (kmap may have partially mapped this 4MB range)
      pte_t *pgtab = (pte_t*)p2v(PTE_ADDR(kpgdir[pde_idx]));
      uint pte_idx = (va >> PTXSHIFT) & 0x3FF;
      if(pgtab[pte_idx] & PTE_P)
        return (void*)va;  // Fully mapped, safe to use
      // PDE exists but this specific PTE is missing - add it
      pgtab[pte_idx] = (pa & ~0xFFF) | PTE_P | PTE_W;
      lcr3(v2p(kpgdir));  // Flush TLB
      return (void*)va;
    }

    // PDE not present - create full 4KB page table for this 4MB range
    {
      char *pgtab = kalloc();
      if(!pgtab)
        return 0;
      memset(pgtab, 0, PGSIZE);

      pte_t *pt = (pte_t*)pgtab;
      uint i;
      for(i = 0; i < 1024; i++)
        pt[i] = (pa_base + i * PGSIZE) | PTE_P | PTE_W;

      kpgdir[pde_idx] = v2p(pgtab) | PTE_P | PTE_W;
      lcr3(v2p(kpgdir));
      return (void*)va;
    }
  }

  // Range 4: PA in [DEVSPACE-KERNBASE, DEVSPACE) = [0x70000000, 0xF0000000)
  // KERNBASE+PA would overflow to user space or collide with DEVSPACE.
  // Use temporary mapping window at PDE 959 (VA 0xEFC00000).
  {
    uint pa_base = pa & ~0x3FFFFF;  // 4MB aligned
    uint offset = pa & 0x3FFFFF;    // offset within 4MB page

    // Check if we need to remap (different 4MB base)
    if(acpi_temp_pa_base != pa_base || !(kpgdir[ACPI_TEMP_PDE] & PTE_P)){
      char *pgtab;

      // Reuse existing page table or allocate new one
      if(kpgdir[ACPI_TEMP_PDE] & PTE_P){
        pgtab = (char*)p2v(PTE_ADDR(kpgdir[ACPI_TEMP_PDE]));
      } else {
        pgtab = kalloc();
        if(!pgtab)
          return 0;
      }
      memset(pgtab, 0, PGSIZE);

      // Fill 4KB page table entries for the 4MB range
      pte_t *pt = (pte_t*)pgtab;
      uint i;
      for(i = 0; i < 1024; i++)
        pt[i] = (pa_base + i * PGSIZE) | PTE_P | PTE_W;

      kpgdir[ACPI_TEMP_PDE] = v2p(pgtab) | PTE_P | PTE_W;
      acpi_temp_pa_base = pa_base;
      lcr3(v2p(kpgdir));
    }

    return (void*)(ACPI_TEMP_VA + offset);
  }
}

int
mpbcpu(void)
{
  return bcpu-cpus;
}

static uchar
sum(uchar *addr, int len)
{
  int i, sum;

  sum = 0;
  for(i=0; i<len; i++)
    sum += addr[i];
  return sum;
}

// Look for an MP structure in the len bytes at addr.
static struct mp*
mpsearch1(uint a, int len)
{
  uchar *e, *p, *addr;

  addr = p2v(a);
  e = addr+len;
  for(p = addr; p < e; p += sizeof(struct mp))
    if(memcmp(p, "_MP_", 4) == 0 && sum(p, sizeof(struct mp)) == 0)
      return (struct mp*)p;
  return 0;
}

// Search for the MP Floating Pointer Structure, which according to the
// spec is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) in the last KB of system base memory;
// 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
static struct mp*
mpsearch(void)
{
  uchar *bda;
  uint p;
  struct mp *mp;

  bda = (uchar *) P2V(0x400);
  if((p = ((bda[0x0F]<<8)| bda[0x0E]) << 4)){
    if((mp = mpsearch1(p, 1024)))
      return mp;
  } else {
    p = ((bda[0x14]<<8)|bda[0x13])*1024;
    if((mp = mpsearch1(p-1024, 1024)))
      return mp;
  }
  return mpsearch1(0xF0000, 0x10000);
}

// Search for an MP configuration table.  For now,
// don't accept the default configurations (physaddr == 0).
// Check for correct signature, calculate the checksum and,
// if correct, check the version.
// To do: check extended table checksum.
static struct mpconf*
mpconfig(struct mp **pmp)
{
  struct mpconf *conf;
  struct mp *mp;

  if((mp = mpsearch()) == 0 || mp->physaddr == 0)
    return 0;
  conf = (struct mpconf*) p2v((uint) mp->physaddr);
  if(memcmp(conf, "PCMP", 4) != 0)
    return 0;
  if(conf->version != 1 && conf->version != 4)
    return 0;
  if(sum((uchar*)conf, conf->length) != 0)
    return 0;
  *pmp = mp;
  return conf;
}

// Search for ACPI RSDP in the given memory range.
static struct acpi_rsdp*
acpi_search_rsdp(uint start, uint len)
{
  uchar *p, *e;

  p = p2v(start);
  e = p + len;
  for(; p < e; p += 16){
    if(memcmp(p, "RSD PTR ", 8) == 0 && sum(p, 20) == 0)
      return (struct acpi_rsdp*)p;
  }
  return 0;
}

// Find the ACPI RSDP by scanning standard locations.
static struct acpi_rsdp*
acpi_find_rsdp(void)
{
  struct acpi_rsdp *rsdp;
  uchar *bda;
  uint ebda;

  // Search in EBDA first
  bda = (uchar *) P2V(0x400);
  ebda = ((bda[0x0F]<<8) | bda[0x0E]) << 4;
  if(ebda){
    rsdp = acpi_search_rsdp(ebda, 1024);
    if(rsdp)
      return rsdp;
  }

  // Search in BIOS ROM area
  return acpi_search_rsdp(0xE0000, 0x20000);
}

// Parse ACPI MADT to find additional CPUs.
// Returns number of CPUs found, or 0 on failure.
static int
acpi_parse_madt(void)
{
  struct acpi_rsdp *rsdp;
  struct acpi_header *rsdt;
  struct acpi_madt *madt;
  uint *entry;
  int nentry, i;
  int found_cpus = 0;

  rsdp = acpi_find_rsdp();
  if(!rsdp){
    cprintf("smp: ACPI RSDP not found");
    boot_fail();
    return 0;
  }
  // ACPI RSDP found

  // Map RSDT (may be in high memory above PHYSTOP)
  rsdt = (struct acpi_header*) acpi_map_phys(rsdp->rsdtaddr);
  if(!rsdt){
    cprintf("smp: failed to map RSDT");
    boot_fail();
    return 0;
  }
  if(memcmp(rsdt->signature, "RSDT", 4) != 0){
    cprintf("smp: RSDT signature invalid");
    boot_fail();
    return 0;
  }
  // RSDT found

  // Scan RSDT entries to find MADT (APIC table)
  nentry = (rsdt->length - sizeof(struct acpi_header)) / 4;
  entry = (uint*)(rsdt + 1);
  madt = 0;
  for(i = 0; i < nentry; i++){
    struct acpi_header *h = (struct acpi_header*) acpi_map_phys(entry[i]);
    if(!h) continue;
    if(memcmp(h->signature, "APIC", 4) == 0){
      madt = (struct acpi_madt*) h;
      break;
    }
  }

  if(!madt){
    cprintf("smp: ACPI MADT not found");
    boot_fail();
    return 0;
  }

  // MADT found
  lapic = (uint*)(uint)madt->lapicaddr;

  // Parse MADT entries
  uchar *p = (uchar*)(madt + 1);
  uchar *e = (uchar*)madt + madt->header.length;

  // Reset CPU info - we'll rebuild from ACPI
  ncpu = 0;
  ismp = 1;

  while(p < e){
    uchar type = p[0];
    uchar len = p[1];
    if(len < 2) break;

    if(type == MADT_LAPIC){
      struct madt_lapic *la = (struct madt_lapic*)p;
      // Only count enabled processors
      if(la->flags & 1){
        if(ncpu < NCPU){
          // ACPI found cpu
          cpus[ncpu].id = la->apic_id;
          ncpu++;
          found_cpus++;
        }
      }
    } else if(type == MADT_IOAPIC){
      struct madt_ioapic *io = (struct madt_ioapic*)p;
      ioapicid = io->ioapic_id;
      // ACPI I/O APIC found
    }

    p += len;
  }

  return found_cpus;
}

void
mpinit(void)
{
  uchar *p, *e;
  struct mp *mp;
  struct mpconf *conf;
  struct mpproc *proc;
  struct mpioapic *ioapic;
  int mp_ncpu;

  bcpu = &cpus[0];
  if((conf = mpconfig(&mp)) == 0){
    // Try ACPI MADT as fallback
    if(acpi_parse_madt() > 0){
      bcpu = &cpus[0];
    }
    return;
  }

  ismp = 1;
  lapic = (uint*)conf->lapicaddr;
  for(p=(uchar*)(conf+1), e=(uchar*)conf+conf->length; p<e; ){
    switch(*p){
    case MPPROC:
      proc = (struct mpproc*)p;
      if(ncpu != proc->apicid){
        ismp = 0;
      }
      if(proc->flags & MPBOOT){
        bcpu = &cpus[ncpu];
      }
      cpus[ncpu].id = ncpu;
      ncpu++;
      p += sizeof(struct mpproc);
      continue;
    case MPIOAPIC:
      ioapic = (struct mpioapic*)p;
      ioapicid = ioapic->apicno;
      p += sizeof(struct mpioapic);
      continue;
    case MPBUS:
    case MPIOINTR:
    case MPLINTR:
      p += 8;
      continue;
    default:
      ismp = 0;
      p += 8; // skip unknown entry
      continue;
    }
  }
  if(!ismp){
    ncpu = 1;
    lapic = 0;
    ioapicid = 0;
    return;
  }

  mp_ncpu = ncpu;

  // If MP table found only 1 CPU, try ACPI MADT for additional CPUs.
  // QEMU puts only BSP in MP table; other CPUs are in ACPI MADT.
  if(ncpu == 1){
    int acpi_cpus = acpi_parse_madt();
    if(acpi_cpus > mp_ncpu){
      bcpu = &cpus[0];
    } else {
      ncpu = mp_ncpu;
    }
  }

  if(mp->imcrp){
    outb(0x22, 0x70);   // Select IMCR
    outb(0x23, inb(0x23) | 1);  // Mask external interrupts.
  }
}
