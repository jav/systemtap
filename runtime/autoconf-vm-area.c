#include <linux/vmalloc.h>
#include <asm/page.h>

void foo (void)
{
  void *dummy;
  dummy = alloc_vm_area (PAGE_SIZE);
  free_vm_area (dummy);
}
