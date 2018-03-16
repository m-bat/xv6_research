#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  printf(1, "Manabu's first user program on xv6\n");
  malloc(10);
  malloc(2);
  exit();  
}
