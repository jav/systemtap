static inline int
m(char *name, int i, long j)
{
  // Random syntactical block to be inlined.
  // Mimics what STAP_PROBE macro does a bit.
  do {
    // Dummy (volatile) counter to trick gcc into thinking we are actually
    // using the label. If not it will partially optimize the label away,
    // but still emits a somewhat bogus DW_AT_low_pc for it...
    volatile int c = 0;
    volatile __typeof__(name) p_name = name;
    volatile __typeof__(i) p_i = i;
    volatile __typeof__(j) p_j = j;
    // empty asm to force locals into regs.
    inlined_label: asm volatile ("nop" : "=g"(c) : "g"(p_name), "g"(p_i), "g"(p_j));
    if (c != 0) goto inlined_label;
  } while (0);
  return i + 32;
}

static inline int
call(int pi, long pj)
{
  volatile ic = pi - 42;
  volatile jc = pj + 42;
  return m("call", ic, jc);
}

static inline int
call2(int pi2, long pj2)
{
  volatile ic2 = pi2 + 64;
  volatile jc2 = pj2 - 64;
  return m("call2", ic2, jc2);
}

int
main (int argc, char **argv)
{
  volatile int i = 64;
  volatile long j = 42;
  call(i, j);
  call2(i, j);
  m("main", i, j);
  return 0;
}
