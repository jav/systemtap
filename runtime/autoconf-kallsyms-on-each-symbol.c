#include <linux/kallsyms.h>

void foo (void) {
   (void) kallsyms_on_each_symbol(NULL, NULL);
}
