#include <stdio.h>
#include <syscall.h>

int
main (int argc,char* argv[])
{
  int i;
  for (i = 0; i < argc; i++)
    printf ("%s ", argv[i]);
     
	printf ("\n");
printf("FUCK\n");
  *((int*)0xc0000001)=3;
  return EXIT_SUCCESS;
}
