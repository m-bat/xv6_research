#include "types.h"
#include "user.h"

int main(int argc, char *argv[])
{
  int i, pid;
  char *argv1[] = { "ls", 0 };

  for (i = 0; i < 100; i++) {
    pid = fork();
    if (pid == -1) {
      printf(1, "measure");    
    }
    else if (pid == 0) {
      exec("ls", argv1);
    }
    wait();
  }
  
  exit();
}
