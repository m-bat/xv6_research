#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"


/* char *argv[] = { "ls", 0 }; */

/* void */
/* panic(char *s) */
/* { */
/*   printf(2, "%s\n", s); */
/*   exit(); */
/* } */


/* int */
/* fork1(void) */
/* { */
/*   int pid; */

/*   pid = fork(); */
/*   if(pid == -1) */
/*     panic("fork"); */
/*   return pid; */
/* } */


/* int main() */
/* { */

/*   while(1){ */
/*     if(fork1() == 0) */
/*       exec("ls", argv); */
/*     wait(); */
/*   } */
/*   exit(); */
/* } */


char *argv[] = { "ls", 0 };

int
main(void)
{
  int i = 0;

  for (i = 0; i < 10000; i++) {    
  }
  
  exit();
}
