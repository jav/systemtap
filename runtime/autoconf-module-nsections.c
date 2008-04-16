#include <linux/module.h>

struct module_sect_attrs x;

void foo (void)
{
  (void) x.nsections; 
}
