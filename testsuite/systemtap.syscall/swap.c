/* COVERAGE: swapon swapoff */
#include <unistd.h>
#include <asm/page.h>
#include <sys/swap.h>


int main()
{
  swapon("foobar_swap", 0);
  // swapon ("foobar_swap", 0) =

  swapon("foobar_swap", ((1 << SWAP_FLAG_PRIO_SHIFT) & SWAP_FLAG_PRIO_MASK) | SWAP_FLAG_PREFER);
  // swapon ("foobar_swap", 32769) =

  swapon("foobar_swap", ((7 << SWAP_FLAG_PRIO_SHIFT) & SWAP_FLAG_PRIO_MASK) | SWAP_FLAG_PREFER);
  // swapon ("foobar_swap", 32775) =

  swapon(0, 0);
  // swapon (NULL, 0) =

  swapoff("foobar_swap");
  // swapoff ("foobar_swap") =

  swapoff(0);
  // swapoff (NULL) =

  return 0;
}
 
