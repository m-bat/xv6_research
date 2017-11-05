// Memory layout


#define EXTMEM  0x100000            // Start of extended memory
//#define EXTUSE  0xDFFF000

//#define EXTUSE  0xE000000
#define EXTUSE  0xC000000

#define EXT64   0x64

#define PHYSTOP 0xE000000          // Top physical memory
//#define PHYSTOP 0xE00A000

//#define USERINFO 0xDFFFF9C
#define DEVSPACE 0xFE000000         // Other devices are at high addresses

// Key addresses for address space layout (see kmap in vm.c for layout)

//#define KERNBASE_U 0x7FFFFF9C
#define KERNBASE 0x80000000        //qFirst kernel virtual address

//#define KERNBASE 0x7FFFFF9C
#define KERNLINK (KERNBASE+EXTMEM)  // Address where kernel is linked
//#define USERINFO (EXTUSE+EXT64)
#define USERINFO (KERNBASE+EXTUSE)

#define V2P(a) (((uint) (a)) - KERNBASE)
#define P2V(a) (((void *) (a)) + KERNBASE)

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts
