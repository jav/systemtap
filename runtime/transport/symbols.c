/* -*- linux-c -*- 
 * symbols.c - stp symbol and module functions
 *
 * Copyright (C) Red Hat Inc, 2006-2008
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * The u32_swap(), generic_swap(), and sort() functions were adapted from
 * lib/sort.c of kernel 2.6.22-rc5. It was written by Matt Mackall.
 */

#ifndef _STP_SYMBOLS_C_
#define _STP_SYMBOLS_C_
#include "../sym.h"



static void _stp_create_unwind_hdr(struct _stp_module *m);


static void _stp_do_relocation(const char __user *buf, size_t count)
{
  struct _stp_msg_relocation msg;
  unsigned mi, si;

  if (sizeof(msg) != count)
    {
      errk ("STP_RELOCATE message size mismatch (%lu vs %lu)\n",
            (long unsigned) sizeof(msg), (long unsigned) count);
      return;
    }

  if (unlikely(copy_from_user (& msg, buf, count)))
    return;

  dbug_sym(2, "relocate (%s %s 0x%lx)\n", msg.module, msg.reloc, (unsigned long) msg.address);


  /* Save the relocation value.  XXX: While keeping the data here is
     fine for the kernel address space ("kernel" and "*.ko" modules),
     it is NOT fine for user-space apps.  They need a separate
     relocation values for each address space, since the same shared
     libraries/executables can be mapped in at different
     addresses.  */

  for (mi=0; mi<_stp_num_modules; mi++)
    {
      if (strcmp (_stp_modules[mi]->name, msg.module))
        continue;

      if (!strcmp (".note.gnu.build-id", msg.reloc)) {
        _stp_modules[mi]->notes_sect = msg.address;   /* cache this particular address  */
      }

      for (si=0; si<_stp_modules[mi]->num_sections; si++)
        {
          if (strcmp (_stp_modules[mi]->sections[si].name, msg.reloc))
            continue;
          else
            {
              _stp_modules[mi]->sections[si].addr = msg.address;
              break;
            }
        } /* loop over sections */
    } /* loop over modules */
}


static void u32_swap(void *a, void *b, int size)
{
	u32 t = *(u32 *)a;
	*(u32 *)a = *(u32 *)b;
	*(u32 *)b = t;
}

static void generic_swap(void *a, void *b, int size)
{
  char *aa = a;
  char *bb = b;
	do {
          char t = *aa;
          *aa++ = *bb;
          *bb++ = t;
	} while (--size > 0);
}

/**
 * sort - sort an array of elements
 * @base: pointer to data to sort
 * @num: number of elements
 * @size: size of each element
 * @cmp_func: pointer to comparison function
 * @swap_func: pointer to swap function or NULL
 *
 * This function does a heapsort on the given array. You may provide a
 * swap function optimized to your element type.
 *
 * Sorting time is O(n log n) both on average and worst-case. While
 * qsort is about 20% faster on average, it suffers from exploitable
 * O(n*n) worst-case behavior and extra memory requirements that make
 * it less suitable for kernel use.
*/
void _stp_sort(void *_base, size_t num, size_t size,
	       int (*cmp_func) (const void *, const void *), void (*swap_func) (void *, void *, int size))
{
        char *base = (char*) _base;
	/* pre-scale counters for performance */
	int i = (num / 2 - 1) * size, n = num * size, c, r;

	if (!swap_func)
		swap_func = (size == 4 ? u32_swap : generic_swap);

	/* heapify */
	for (; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r = c) {
			c = r * 2 + size;
			if (c < n - size && cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i >= 0; i -= size) {
		swap_func(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size && cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}
}

/* filter out section names we don't care about */
static int _stp_section_is_interesting(const char *name)
{
	int ret = 1;
	if (!strncmp("__", name, 2)
	    || (!strncmp(".note", name, 5) 
		&& strncmp(".note.gnu.build-id", name, 18))
	    || !strncmp(".gnu", name, 4)
	    || !strncmp(".mod", name, 4))
		ret = 0;
	return ret;
}


#endif /* _STP_SYMBOLS_C_ */
