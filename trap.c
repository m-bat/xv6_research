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

uint ticks;

//add manabu 10/24
uint kgflag = 0;

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

    /*
    switchkvm();    
    p = myproc();
    setptew_kernel(p->pgdir);
    switchuvm(p);
    */
    
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
    cprintf("&lapic :%x, lapic :%x\n", &lapic, lapic);
    //force kill process
    //myproc()->killed = 1;   
    p = myproc();
    cprintf("DEBUG INFO: in kvm, proccess name %s pid %d\n", myproc()->name, myproc()->pid);
    //cprintf("mycpu->ncli: %d\n", mycpu()->ncli);
    uint a = PGROUNDDOWN(rcr2());
    cprintf("trap a: %x\n", a);
    cprintf("trap rcr2: %x\n", rcr2());
    
    if (a >= (uint)get_kplocal_addr() && a <= (uint)get_devspace_addr()) {
      //access kernel process local area
      cprintf("DEBUG: Access KERNELPLOCAL\n");
      exit();
    }
    else {      
      //setptew(p->pgdir, (void *)a, PGSIZE, 1);
      cprintf("DEBUG: Access KERNELGLOBAL\n") ;
      switchkvm();
      setptew_kernel(p->pgdir);
      switchuvm(p);
      cprintf("after setptew :%x\n", a);
      cprintf("kgflag : %x\n", &kgflag);
      kgflag = 1;
    }
    
    //cprintf("T_PGFLT: kgflag = %d\n", kgflag);
    cprintf("info: proccess name %s pid %d\n", myproc()->name, myproc()->pid);
    //panic("T_PGFLT");
    //exit();    
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
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
