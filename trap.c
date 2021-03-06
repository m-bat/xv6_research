#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
//struct spinlock tickslock;
//add manabu 11/10

struct spinlock *tickslock;
uint ticks __attribute__((__section__(".should_writable")));


//add manabu 10/24
uint kgflag __attribute__((__section__(".must_writable"))) = 0;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  //add manabu 11/10
  if ((tickslock = (struct spinlock *)kalloc(ALLOC_KGLOBAL)) == 0) {
    panic("kalloc: tickslock");
  }
  //

  initlock(tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  //add manabu 10/29
  struct proc *p;  
  //  
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
     return;
  }
  switch(tf->trapno){
    
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(tickslock);
      ticks++;
      wakeup(&ticks);
      release(tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",            
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //add manabu 9/22 :page table entry writeable
  case T_PGFLT:
    //cprintf("&lapic :%x, lapic :%x\n", &lapic, lapic);
    //force kill process
    //myproc()->killed = 1;   
    p = myproc();
     uint a = PGROUNDDOWN(rcr2());
    cprintf("LOG INFO: page fault proccess name %s, pid %d, addr %x\n", myproc()->name, myproc()->pid, rcr2());
    //cprintf("mycpu->ncli: %d\n", mycpu()->ncli);
    
    if (a >= (uint)get_kplocal_addr() && a <= (uint)get_devspace_addr()) {
      //access kernel process local area
      cprintf("LOG: Access KERNELPLOCAL! exit process %s\n", p->name);
      exit();
    }
    else {      
      //setptew(p->pgdir, (void *)a, PGSIZE, 1);
      if ( !kgflag ) {
        kgflag = 1;        
        cprintf("LOG: Access KERNELGLOBAL! set kgflag\n");
      }      
      switchkvm();
      //setptew_kernel(p->pgdir);
      setptew(p->pgdir, (char *)a, sizeof(a), 11);      
      switchuvm(p);
    }
    
    break;

  case T_NMI:
    cprintf("DEBUG: T_NMI call\n");
    break;

  case T_STACK:
    cprintf("DEBUG: T_STACK call\n");
    break;
    
  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }
  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER) {
    yield();
    //DEBUG
    //cprintf("DEBUG manabu\n");
  }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
