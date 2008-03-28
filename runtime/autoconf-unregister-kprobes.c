#include <linux/kprobes.h>

void * x = (void *)unregister_kprobes;
void * y = (void *)unregister_kretprobes;
