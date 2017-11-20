// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"


void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  struct run *freelist_plocal;
} kmem;


//add manabu
extern pde_t *kpgdir;
//extern struct cpu *cpus;
//

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
  //DEBU
  /*
  cprintf("kinit1\n");
  cprintf("vstart: %x", vstart);
  cprintf("vlast: %x", vend);  
  */
  //
}

void
kinit2(void *vstart, void *vend)
{  
  freerange(vstart, vend);
  kmem.use_lock = 1;
}
void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  
  if (v >= (char *)KERNPLOCAL) {
    r->next = kmem.freelist_plocal;
    kmem.freelist_plocal = r;
    //Read-only set PTE_R
    //clearptew(kpgdir, (char *)r);
    //DEBUG
    //cprintf("kinit2: kpgdir->0x%x\n", kpgdir);
  }
  else {
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
    
  //kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
/*
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}*/
//add manabu 11/12

char*
kalloc(alloc_flag_t flag) {
  //struct run *r;
  struct run *r = 0;

  if(kmem.use_lock)
    acquire(&kmem.lock);

  switch(flag) {
  case ALLOC_KGLOBAL:
    r = kmem.freelist;
    if(r)
      kmem.freelist = r->next;
    break;
  case ALLOC_PLOCAL:
    r = kmem.freelist_plocal;
    if(r) {
      kmem.freelist_plocal = r->next;
      //cprintf("DEBUG: kalloc(ALLOC_PLOCAL) r %x\n", r);
    }
    break;
  default:
    panic("unknown allocation flag");
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}


//add function  by manabu
/*
char*
kuinfo_alloc (void) {
  struct run *r;

  if (kmem.use_lock) 
    acquire(&kmem.lock);
	
  r = kmem.freelist_plocal;
  if (r) 
    kmem.freelist_plocal = r->next;
	
  if (kmem.use_lock)
    release(&kmem.lock);
	
  return (char*)r;
}
*/

//add function by manabu 10/26
//allocate cpu array
//

int 
cpualloc() {

  if ((cpus = (struct cpu *)kalloc(ALLOC_KGLOBAL)) == 0) {
    return 0;
  }
  //DEBUG
  //`cprintf("cpus %x", cpus);

  //init cpus by zero
  memset((char *)cpus, 0, PGSIZE);
  return 1;
}

