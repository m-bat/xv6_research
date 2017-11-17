//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

//add manabu 10/17
#include "mmu.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  //struct file file[NFILE];
  //add manabu
  struct file file;  //file head 
} ftable;

struct file *h;

void
fileinit(void)
{
  //***:add manabu 10/7 start*****
  /*
  struct file *f;
  int i;
  h = &(ftable.file);

  h->next = h;
  h->prev = h;  for (i = 0; i < NFILE; i++) {    
    if ((f = (struct file *)kuinfo_alloc()) == 0) {
      panic("fileinit");
    }
    //insert ftable list(from head)    
    f->prev = h;
    f->next = h->next;
    h->next->prev = f;
    h->next = f;
    f->ref = 0;
  } 
  */ 
  // ****finish***********************
  
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;
  // add manabu
 
  struct proc *p;
  p = myproc(); 
  
  //
  acquire(&ftable.lock);
  //comment out
  /*
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }  
  */
  
  //add manabu 10/17 start
  /*
  for(f = ftable.file.next; f != &ftable.file; f = f->next){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  */
  if ((f = (struct file *)kalloc(ALLOC_PLOCAL)) == 0) {    
    panic("fileinit");
  }
  else {
    switchkvm();
    setptew(p->pgdir, (char *)f, PGSIZE, 0);
    switchuvm(p);
    
    f->ref = 1;
    plocal_insert((char *)f); //insert plocal alloc list
    
    release(&ftable.lock);
    return f;
  }  
  //finish
  
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  
  //add manabu 10/31
  //free file struct (kuinfo_alloc)
  kfree((char*)f);
  //
  
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((LOGSIZE-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

