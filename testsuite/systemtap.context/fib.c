#include <stdlib.h>
#include <stdio.h>

long fib(int x)
{
  if (x == 0 || x == 1)
    return 1;
  else
    return fib(x - 1) + fib(x - 2);
}

int main(int argc, char **argv)
{
  int x = 0;
  long result = 0;

  if (argc != 2)
    {
      printf("0\n");
      return 1;
    }
  x = atoi(argv[1]);
  if (x < 0)
    {
      printf("0\n");
      return 1;
    }
  result = fib(x);
  printf("%ld\n", result);
  return 0;
}
