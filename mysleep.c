#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
//#include "defs.h"

int main(int argc, char *argv[])
{
  int n;  

  n = atoi(argv[1]);
  
  sleep(n);
  exit();
}
