#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "buf.h"


extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()
//add manabu 10/31
extern struct cons_lk *cons;

//add manabu 11/2
extern struct ptable_t *ptable;
extern struct superblock sb;

//add manabu 11/17
extern struct spinlock idelock;
extern struct buf *idequeue;
extern char *plist[100];
extern int plist_index;
//

extern struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

extern struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

extern struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  struct run *freelist_plocal;
} kmem;

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.

static pte_t *
walkpgdir(pde_t *pgdir, const void *va)
{  
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
      return 0;
  }
  return &pgtab[PTX(va)];
}

static pte_t *
walkpgdir_with_alloc(pde_t *pgdir, const void *va, alloc_flag_t flag)
{  
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if((pgtab = (pte_t*)kalloc(flag)) == 0)
      return 0;    
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm, alloc_flag_t flag)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir_with_alloc(pgdir, a, flag)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
  { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, //     [1] I/O space 
  { (void*)KERNLINK, V2P(KERNLINK), V2P(data),     0}, //     [2] kern text+rodata
  { (void*)data,     V2P(data),     V2P(KERNPLOCAL), PTE_W}, // [3] kern data+memory
  { (void*)KERNPLOCAL, V2P(KERNPLOCAL), PHYSTOP,  PTE_W}, //      [4] kern userinfo
  { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, //     [5] more devices
};

// Set up kernel part of a page table.

char * get_kplocal_addr() {    
  return kmap[3].virt;
}

char * get_devspace_addr() {
  return kmap[4].virt;
}

pde_t*
setupkvm(alloc_flag_t flag)  
{  
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc(ALLOC_KGLOBAL)) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);  

  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++) {
    //DEBUG
    //cprintf("k->virt: 0x%x, k->phys_start: 0x%x, k->phys_end: 0x%x\n", k->virt, k->phys_start, k->phys_end);
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm, flag) < 0) {
      freevm(pgdir);
      return 0;
    }
  }
  return pgdir;
}

//add manabu 10/9 kernel land read-only
void set_kmem_readonly(pde_t *pgdir) {
  struct kmap *k;  
  char *a, *last;
  uint size;
  int i = 0;
  
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++) {
    i++;
    //TODO Now make only kmap:data memory read-only
    if (i == 1 || i == 2 || i == 5) {
      continue;
    }       
    size = k->phys_end - k->phys_start;
    a = (char*)PGROUNDDOWN((uint)k->virt);
    last = (char*)PGROUNDDOWN(((uint)k->virt) + size - 1);
    cprintf("i: %d, start %x, last %x\n", i, a, last);
   
    for(;;){
      clearptew(pgdir, a);
      if(a == last)
        break;
      a += PGSIZE;
    }
    
  }
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm(ALLOC_KGLOBAL);
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  
  popcli();
}

//add manabu 10/14 moify switchuvm

char *mem_inituvm;

void
switchuvm_ro(struct proc *p, const int n)
{
  int i;
  
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);

    // add manabu 10/16  
  if (n) {
    cprintf("DEBUG: init hit or sh hit\n");
  }  
  else {
    cprintf("DEBUG: before: kernel_ro\n");
    //cprintf("ptable addr %x\n", &ptable);

    set_kmem_readonly(p->pgdir);    
    //cprintf("ptable addr: %x\n", ptable);
    //int size = PGROUNDUP(sizeof(ptable));
    //cprintf("ptable size %d\n", size);
    //cprintf("DEBUG: keme size: %x\n", sizeof(kmem));

    //********* Kenel Global  *********************
    //setptew(p->pgdir, (char *)idt, PGSIZE, 1);
    setptew(p->pgdir, (char *)cpus, PGSIZE, 1);
    setptew(p->pgdir, (char *)cons, PGSIZE, 1);
    setptew(p->pgdir, (char *)mem_inituvm, PGSIZE, 1);
    setptew(p->pgdir, (char *)tickslock, PGSIZE, 1);
    setptew(p->pgdir, (char *)&ticks, PGSIZE, 1);
    setptew(p->pgdir, (char *)lapic, PGSIZE, 1);        
    setptew(p->pgdir, (char *)ptable, PGSIZE, 1);
    setptew(p->pgdir, (char *)&icache, sizeof(icache), 1);
    setptew(p->pgdir, (char *)&sb, sizeof(sb), 1);
    setptew(p->pgdir, (char *)&bcache, sizeof(bcache), 1);
    setptew(p->pgdir, (char *)&kmem, sizeof(kmem), 1);
    setptew(p->pgdir, (char *)idequeue, sizeof(idequeue), 1);
    setptew(p->pgdir, (char *)&idelock, sizeof(idelock), 1);
    setptew(p->pgdir, (char *)&plist_index, sizeof(plist_index), 1);
    for (i = 0; i < 100; i++) {
      //cprintf("DEBUG: setptew &plist[%d] %x\n", i, &plist[i]);
      setptew(p->pgdir, (char *)&plist[i], sizeof(&plist), 1);
    }
          
    cprintf("DEBUG: bcache size %d\n", sizeof(bcache));
    cprintf("DEBUG: plist size %d\n", sizeof(&plist));

    //********* Kernel Process Local  *************
    setptew(p->pgdir, p->kstack, KSTACKSIZE, 1);

    //*********************************************

    cprintf("DEBUG: process name %s\n", p->name);
    
    //set open filen array to be writable
    //set ofile[0], ofile[1], ofile[2] to be writable because parent process is init.
    //ofile[0] == ofile[1] == ofile[2] Even if i < 1
    int i;
    //*Additional notes*
    //Because fork is duped from the original process, ofile is made writeable.
    for (i = 0; i < NOFILE; i++) { // Even if i < NOFILE      
      setptew(p->pgdir, (char *)(p->ofile[i]), PGSIZE, 1);
      //Make the pipe structure writeable
      if (p->ofile[i]->type == FD_PIPE) {
        setptew(p->pgdir, (char *)(p->ofile[i]->pipe), PGSIZE, 1);
      }
    }

    cprintf("DEBUG: after: kernel_ro\n");
  }
   
  lcr3(V2P(p->pgdir));  // switch to process's address space
    
  cprintf("DEBUG: after: changed lcr3\n");
  popcli();
  //cprintf("after: popcli");
  //panic after
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.

void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc(ALLOC_KGLOBAL);
  //add manabu
  mem_inituvm = mem;
  //
  
  //DEBUG
  cprintf("mem %x", mem);
  //
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U, ALLOC_KGLOBAL);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}
                   
// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc(ALLOC_KGLOBAL);
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U, ALLOC_KGLOBAL) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;
  
  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE) {
    //cprintf("DEBUG: deallocuvm %x\n", (char *)tmp);
    
    pte = walkpgdir(pgdir, (char*)a);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0) {
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      //printf("DEBUG: deallocuvm: v %x\n", v);
      /*
      switchkvm(); 
      setptew(myproc()->pgdir, (char *)pte, PGSIZE, 1);      
      setptew(myproc()->pgdir, (char *)v, PGSIZE, 1);      
      switchuvm(myproc());           
      */
      
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;
  if (pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;
  
  pte = walkpgdir(pgdir, uva);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

//add manabu 9/22: set read-only
void
clearptew(pde_t *pgdir, char *uva)  
{

  pte_t *pte;
  
  pte = walkpgdir(pgdir, uva);
    
  if (pte == 0)
    panic("clearptew");

  //read-only
  *pte &= ~PTE_W;  
}

 
//add manabu 10/08: set write-enable
void
setptew(pde_t *pgdir, char *uva, uint size, uint c)  
{
  char *a, *last;
  a = (char *)PGROUNDDOWN((uint)uva);
  last = (char *)PGROUNDDOWN(((uint)uva) + size - 1);
  pte_t *pte;

  for (;;) {
    pte = walkpgdir(pgdir, a);
  
    if (pte == 0)
      panic("setptew");

    //set write-eable
    *pte |= PTE_W;
    if  (a == last) {
      break;
    }
    a += PGSIZE;
  }
  //flush the TLB
  if (c == 0)
    lcr3(V2P(pgdir));
}

//add manabu 10/24: set wite-enable kernel land data & memory

void setptew_kernel(pde_t *pgdir)
{
  char *a, *last;
  uint size;
  pte_t *pte;
  
  size = kmap[2].phys_end - kmap[2].phys_start;

  //DEBUG
  //size /= 1.5;  
  a = (char *)PGROUNDDOWN((uint)kmap[2].virt);
  last = (char *)PGROUNDDOWN(((uint)kmap[2].virt) + size - 1);

  cprintf("kernel start addr %x\n", a);
  cprintf("kernel last addr %x\n", last);
  
  for (;;) {
    pte = walkpgdir(pgdir, a);
  
    if (pte == 0)
      panic("setptew");

    //set write-eable
    *pte |= PTE_W;
    if (a == last) {
      break;
    }
    //cprintf("a %x\n", a);
    a += PGSIZE;
  }
  //flush the TLB
  //lcr3(V2P(pgdir));
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm(ALLOC_KGLOBAL)) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc(ALLOC_KGLOBAL)) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags, ALLOC_KGLOBAL) < 0)
      goto bad;
  }
  return d;

 bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}


//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

