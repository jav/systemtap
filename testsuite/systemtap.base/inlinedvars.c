static inline int
m(char *name, int i, long j)
{
  // Random syntactical block to be inlined.
  // Mimics what STAP_PROBE macro does a bit.
  do {
    volatile __typeof__(name) p_name = name;
    volatile __typeof__(i) p_i = i;
    volatile __typeof__(j) p_j = j;
    // empty asm to force locals into regs.
    inlined_label: asm volatile ("" :: "g"(p_name), "g"(p_i), "g"(p_j));
  } while (0);
  return i + 32;
}

/* XXX PR10537 label() doesn't select multiple instances.
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
*/

int
main (int argc, char **argv)
{
  volatile int i = 64;
  volatile long j = 42;
  i = 54;// XXX PR10537 call(i, j);
  j = 150; // XXX PR10537 call2(i, j);
  m("main", i, j);
  return 0;
}
