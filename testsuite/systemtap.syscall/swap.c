/* COVERAGE: swapon swapoff */
#include <unistd.h>
#include <sys/swap.h>


int main()
{
  swapon("foobar_swap", 0);
  //staptest// swapon ("foobar_swap", 0) =

  swapon("foobar_swap", ((1 << SWAP_FLAG_PRIO_SHIFT) & SWAP_FLAG_PRIO_MASK) | SWAP_FLAG_PREFER);
  //staptest// swapon ("foobar_swap", 32769) =

  swapon("foobar_swap", ((7 << SWAP_FLAG_PRIO_SHIFT) & SWAP_FLAG_PRIO_MASK) | SWAP_FLAG_PREFER);
  //staptest// swapon ("foobar_swap", 32775) =

  swapon(0, 0);
  //staptest// swapon (NULL, 0) =

  swapoff("foobar_swap");
  //staptest// swapoff ("foobar_swap") =

  swapoff(0);
  //staptest// swapoff (NULL) =

  return 0;
}
 
