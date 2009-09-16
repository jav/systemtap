/* Simple test program for loc2c code.  */

#include <config.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <locale.h>
#include <argp.h>
#include <elfutils/libdwfl.h>
#ifdef HAVE_ELFUTILS_VERSION_H
#include <elfutils/version.h>
#endif
#include <dwarf.h>
#include <obstack.h>
#include <unistd.h>
#include <stdarg.h>
#include "loc2c.h"

#if !defined(_ELFUTILS_PREREQ)
// make a dummy PREREQ check for elfutils < 0.138
#define _ELFUTILS_PREREQ(major, minor) (0 >= 1)
#endif

#define _(msg) msg

static void __attribute__ ((noreturn))
fail (void *arg __attribute__ ((unused)), const char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, "%s: ", program_invocation_short_name);

  va_start (ap, fmt);
  vfprintf (stderr, _(fmt), ap);
  va_end (ap);

  fprintf (stderr, "\n");

  exit (2);
}

static const Dwarf_Op *
get_location (Dwarf_Addr dwbias, Dwarf_Addr pc, Dwarf_Attribute *loc_attr,
	      size_t *len)
{
  Dwarf_Op *expr;

  switch (dwarf_getlocation_addr (loc_attr, pc - dwbias, &expr, len, 1))
    {
    case 1:			/* Should always happen.  */
      if (*len == 0)
	goto inaccessible;
      break;

    default:			/* Shouldn't happen.  */
    case -1:
      fail (NULL, _("dwarf_getlocation_addr (form %#x): %s"),
	    dwarf_whatform (loc_attr), dwarf_errmsg (-1));
      return NULL;

    case 0:			/* Shouldn't happen.  */
    inaccessible:
      fail (NULL, _("not accessible at this address"));
      return NULL;
    }

  return expr;
}

static void
handle_variable (Dwarf_Die *lscopes, int lnscopes, int out,
		 Dwarf_Addr cubias, Dwarf_Die *vardie, Dwarf_Addr pc,
		 Dwarf_Op *cfa_ops, char **fields)
{
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free
  struct obstack pool;
  obstack_init (&pool);

  /* Figure out the appropriate frame base for accessing this variable.
   * XXX not handling nested functions
   */
  Dwarf_Attribute fb_attr_mem, *fb_attr = NULL;
  int inner;
  /* We start out walking the "lexical scopes" as returned by
   * as returned by dwarf_getscopes for the address, starting with the
   * 'out' scope that the variable was found in.
   */
  Dwarf_Die *scopes = lscopes;
  int nscopes = lnscopes;
  for (inner = out; inner < nscopes && fb_attr == NULL; ++inner)
    {
      switch (dwarf_tag (&scopes[inner]))
	{
	default:
	  continue;
	case DW_TAG_subprogram:
	case DW_TAG_entry_point:
	  fb_attr = dwarf_attr_integrate (&scopes[inner],
					  DW_AT_frame_base,
					  &fb_attr_mem);
	  break;
	case DW_TAG_inlined_subroutine:
	  /* Unless we already are going through the "pyshical die tree",
	   * we now need to start walking the die tree where this
	   * subroutine is inlined to find the appropriate frame base. */
          if (out != -1)
	    {
	      nscopes = dwarf_getscopes_die (&scopes[inner], &scopes);
	      if (nscopes == -1)
		error (2, 0, _("cannot get die scopes inlined_subroutine: %s"),
		       dwarf_errmsg (-1));
	      inner = 0; // zero is current scope, for look will increase.
	      out = -1;
	    }
	  break;
	}
    }

  Dwarf_Attribute attr_mem;

  if (dwarf_attr_integrate (vardie, DW_AT_location, &attr_mem) == NULL)
    error (2, 0, _("cannot get location of variable: %s"),
	   dwarf_errmsg (-1));

#define FIELD "addr"
#define emit(fmt, ...) printf ("  addr = " fmt "\n", ## __VA_ARGS__)

  size_t locexpr_len;
  const Dwarf_Op *locexpr = get_location (cubias, pc, &attr_mem, &locexpr_len);

  struct location *head, *tail = NULL;
  head = c_translate_location (&pool, &fail, NULL, NULL,
			       1, cubias, pc, locexpr, locexpr_len,
			       &tail, fb_attr, cfa_ops);

  if (dwarf_attr_integrate (vardie, DW_AT_type, &attr_mem) == NULL)
    error (2, 0, _("cannot get type of variable: %s"),
	   dwarf_errmsg (-1));

  bool store = false;
  Dwarf_Die die_mem, *die = vardie;
  while (*fields != NULL)
    {
      if (!strcmp (*fields, "="))
	{
	  store = true;
	  if (fields[1] != NULL)
	    error (2, 0, _("extra fields after ="));
	  break;
	}

      die = dwarf_formref_die (&attr_mem, &die_mem);

      const int typetag = dwarf_tag (die);
      switch (typetag)
	{
	case DW_TAG_typedef:
	case DW_TAG_const_type:
	case DW_TAG_volatile_type:
	  /* Just iterate on the referent type.  */
	  break;

	case DW_TAG_pointer_type:
	  if (**fields == '+')
	    goto subscript;
	  /* A "" field means explicit pointer dereference and we consume it.
	     Otherwise the next field implicitly gets the dereference.  */
	  if (**fields == '\0')
	    ++fields;
	  c_translate_pointer (&pool, 1, cubias, die, &tail);
	  break;

	case DW_TAG_array_type:
	  if (**fields == '+')
	    {
	    subscript:;
	      char *endp = *fields + 1;
	      uintmax_t idx = strtoumax (*fields + 1, &endp, 0);
	      if (endp == NULL || endp == *fields || *endp != '\0')
		c_translate_array (&pool, 1, cubias, die, &tail,
				   *fields + 1, 0);
	      else
		c_translate_array (&pool, 1, cubias, die, &tail,
				   NULL, idx);
	      ++fields;
	    }
	  else
	    error (2, 0, _("bad field for array type: %s"), *fields);
	  break;

	case DW_TAG_structure_type:
	case DW_TAG_union_type:
	  switch (dwarf_child (die, &die_mem))
	    {
	    case 1:		/* No children.  */
	      error (2, 0, _("empty struct %s"),
		     dwarf_diename (die) ?: "<anonymous>");
	      break;
	    case -1:		/* Error.  */
	    default:		/* Shouldn't happen */
	      error (2, 0, _("%s %s: %s"),
		     typetag == DW_TAG_union_type ? "union" : "struct",
		     dwarf_diename (die) ?: "<anonymous>",
		     dwarf_errmsg (-1));
	      break;

	    case 0:
	      break;
	    }
	  while (dwarf_tag (die) != DW_TAG_member
		 || ({ const char *member = dwarf_diename (die);
		       member == NULL || strcmp (member, *fields); }))
	    if (dwarf_siblingof (die, &die_mem) != 0)
	      error (2, 0, _("field name %s not found"), *fields);

	  if (dwarf_attr_integrate (die, DW_AT_data_member_location,
				    &attr_mem) == NULL)
	    {
	      /* Union members don't usually have a location,
		 but just use the containing union's location.  */
	      if (typetag != DW_TAG_union_type)
		error (2, 0, _("no location for field %s: %s"),
		       *fields, dwarf_errmsg (-1));
	    }
	  else
	    {
	      /* We expect a block or a constant.  In older elfutils,
		 dwarf_getlocation_addr would not handle the constant for
		 us, but newer ones do.  For older ones, we work around
		 it by faking an expression, which is what newer ones do.  */
#if !_ELFUTILS_PREREQ (0,142)
	      Dwarf_Op offset_loc = { .atom = DW_OP_plus_uconst };
	      if (dwarf_formudata (&attr_mem, &offset_loc.number) == 0)
		c_translate_location (&pool, NULL, NULL, NULL,
				      1, cubias, pc, &offset_loc, 1,
				      &tail, NULL, NULL);
	      else
#endif
		{
		  locexpr = get_location (cubias, pc, &attr_mem,
					  &locexpr_len);
		  c_translate_location (&pool, NULL, NULL, NULL,
					1, cubias, pc, locexpr, locexpr_len,
					&tail, NULL, NULL);
		}
	    }
	  ++fields;
	  break;

	case DW_TAG_base_type:
	  error (2, 0, _("field %s vs base type %s"),
		 *fields, dwarf_diename (die) ?: "<anonymous type>");
	  break;

	case -1:
	  error (2, 0, _("cannot find type: %s"), dwarf_errmsg (-1));
	  break;

	default:
	  error (2, 0, _("%s: unexpected type tag %#x"),
		 dwarf_diename (die) ?: "<anonymous type>",
		 dwarf_tag (die));
	  break;
	}

      /* Now iterate on the type in DIE's attribute.  */
      if (dwarf_attr_integrate (die, DW_AT_type, &attr_mem) == NULL)
	error (2, 0, _("cannot get type of field: %s"), dwarf_errmsg (-1));
    }

  /* Fetch the type DIE corresponding to the final location to be accessed.
     It must be a base type or a typedef for one.  */

  Dwarf_Die typedie_mem;
  Dwarf_Die *typedie;
  int typetag;
  while (1)
    {
      typedie = dwarf_formref_die (&attr_mem, &typedie_mem);
      if (typedie == NULL)
	error (2, 0, _("cannot get type of field: %s"), dwarf_errmsg (-1));
      typetag = dwarf_tag (typedie);
      if (typetag != DW_TAG_typedef &&
	  typetag != DW_TAG_const_type &&
	  typetag != DW_TAG_volatile_type)
	break;
      if (dwarf_attr_integrate (typedie, DW_AT_type, &attr_mem) == NULL)
	error (2, 0, _("cannot get type of field: %s"), dwarf_errmsg (-1));
    }

  switch (typetag)
    {
    case DW_TAG_base_type:
      if (store)
	c_translate_store (&pool, 1, cubias, die, typedie, &tail, "value");
      else
	c_translate_fetch (&pool, 1, cubias, die, typedie, &tail, "value");
      break;

    case DW_TAG_pointer_type:
      if (store)
	error (2, 0, _("store not supported for pointer type"));
      c_translate_pointer (&pool, 1, cubias, typedie, &tail);
      c_translate_addressof (&pool, 1, cubias, die, typedie, &tail, "value");
      break;

    default:
      if (store)
	error (2, 0, _("store supported only for base type"));
      else
	error (2, 0, _("fetch supported only for base type or pointer"));
      break;
    }

  printf ("#define PROBEADDR %#" PRIx64 "ULL\n", pc);

  puts (store
	? "static void set_value(struct pt_regs *regs, intptr_t value)\n{"
	: "static void print_value(struct pt_regs *regs)\n"
	"{\n"
	"  intptr_t value;");

  bool deref = c_emit_location (stdout, head, 1);

  obstack_free (&pool, NULL);

  puts (store ? " return;" :
	"  printk (\" ---> %ld\\n\", (unsigned long) value);\n"
	"  return;");

  if (deref)
    puts ("\n"
	  " deref_fault:\n"
	  "  printk (\" => BAD ACCESS\\n\");");

  puts ("}");
}

static void
paddr (const char *prefix, Dwarf_Addr addr, Dwfl_Line *line)
{
  const char *src;
  int lineno, linecol;
  if (line != NULL
      && (src = dwfl_lineinfo (line, &addr, &lineno, &linecol,
			       NULL, NULL)) != NULL)
    {
      if (linecol != 0)
	printf ("%s%#" PRIx64 " (%s:%d:%d)",
		prefix, addr, src, lineno, linecol);
      else
	printf ("%s%#" PRIx64 " (%s:%d)",
		prefix, addr, src, lineno);
    }
  else
    printf ("%s%#" PRIx64, prefix, addr);
}

static void
print_type (Dwarf_Die *typedie, char space)
{
  if (typedie == NULL)
    printf ("%c<no type>", space);
  else
    {
      const char *name = dwarf_diename (typedie);
      if (name != NULL)
	printf ("%c%s", space, name);
      else
	{
	  Dwarf_Attribute attr_mem;
	  Dwarf_Die die_mem;
	  Dwarf_Die *die = dwarf_formref_die
	    (dwarf_attr_integrate (typedie, DW_AT_type, &attr_mem), &die_mem);
	  int tag = dwarf_tag (typedie);
	  switch (tag)
	  {
	  case DW_TAG_pointer_type:
	    print_type (die, space);
	    putchar ('*');
	    break;
	  case DW_TAG_array_type:
	    print_type (die, space);
	    printf ("[]");
	    break;
	  case DW_TAG_const_type:
	    print_type (die, space);
	    printf (" const");
	    break;
	  case DW_TAG_volatile_type:
	    print_type (die, space);
	    printf (" volatile");
	    break;
	  default:
	    printf ("%c<unknown %#x>", space, tag);
	    break;
	  }
	}
    }
}

static void
print_vars (unsigned int indent, Dwarf_Die *die)
{
  Dwarf_Die child;
  Dwarf_Attribute attr_mem;
  Dwarf_Die typedie_mem;
  Dwarf_Die *typedie;
  if (dwarf_child (die, &child) == 0)
    do
      switch (dwarf_tag (&child))
	{
	case DW_TAG_variable:
	case DW_TAG_formal_parameter:
	  printf ("%*s%-30s[%6" PRIx64 "]", indent, "",
		  dwarf_diename (&child),
		  (uint64_t) dwarf_dieoffset (&child));
	  typedie = dwarf_formref_die
	    (dwarf_attr_integrate (&child, DW_AT_type, &attr_mem),
	     &typedie_mem);
	  print_type (typedie, '\t');
	  puts ("");
	  break;
	default:
	  break;
	}
    while (dwarf_siblingof (&child, &child) == 0);
}

#define INDENT 4

int
main (int argc, char **argv)
{
  /* We use no threads here which can interfere with handling a stream.  */
  (void) __fsetlocking (stdout, FSETLOCKING_BYCALLER);

  /* Set locale.  */
  (void) setlocale (LC_ALL, "");

  Dwfl *dwfl = NULL;
  int argi;
  (void) argp_parse (dwfl_standard_argp (), argc, argv, 0, &argi, &dwfl);
  assert (dwfl != NULL);

  if (argi == argc)
    error (2, 0, "need address argument");

  char *endp;
  uintmax_t pc = strtoumax (argv[argi], &endp, 0);
  if (endp == argv[argi])
    error (2, 0, "bad address argument");

  Dwarf_Addr cubias;
  Dwarf_Die *cudie = dwfl_addrdie (dwfl, pc, &cubias);
  if (cudie == NULL)
    error (EXIT_FAILURE, 0, "dwfl_addrdie: %s", dwfl_errmsg (-1));

  Dwarf_Die *scopes;
  int n = dwarf_getscopes (cudie, pc - cubias, &scopes);
  if (n < 0)
    error (EXIT_FAILURE, 0, "dwarf_getscopes: %s", dwarf_errmsg (-1));
  else if (n == 0)
    error (EXIT_FAILURE, 0, "%#" PRIx64 ": not in any scope\n", pc);

  if (++argi == argc)
    {
      unsigned int indent = 0;
      while (n-- > 0)
	{
	  Dwarf_Die *const die = &scopes[n];

	  indent += INDENT;
	  printf ("%*s[%6" PRIx64 "] %s (%#x)", indent, "",
		  dwarf_dieoffset (die),
		  dwarf_diename (die) ?: "<unnamed>",
		  dwarf_tag (die));

	  Dwarf_Addr lowpc, highpc;
	  if (dwarf_lowpc (die, &lowpc) == 0
	      && dwarf_highpc (die, &highpc) == 0)
	    {
	      lowpc += cubias;
	      highpc += cubias;
	      Dwfl_Line *loline = dwfl_getsrc (dwfl, lowpc);
	      Dwfl_Line *hiline = dwfl_getsrc (dwfl, highpc);
	      paddr (": ", lowpc, loline);
	      if (highpc != lowpc)
		paddr (" .. ", lowpc, hiline == loline ? NULL : hiline);
	    }
	  puts ("");

	  print_vars (indent + INDENT, die);
	}
    }
  else
    {
      char *spec = argv[argi++];

      int lineno = 0, colno = 0, shadow = 0;
      char *at = strchr (spec, '@');
      if (at != NULL)
	{
	  *at++ = '\0';
	  if (sscanf (at, "%*[^:]:%i:%i", &lineno, &colno) < 1)
	    lineno = 0;
	}
      else
	{
	  int len;
	  if (sscanf (spec, "%*[^+]%n+%i", &len, &shadow) == 2)
	    spec[len] = '\0';
	}

      Dwarf_Die vardie;
      int out = dwarf_getscopevar (scopes, n, spec, shadow, at, lineno, colno,
				   &vardie);
      if (out == -2)
	error (0, 0, "no match for %s (+%d, %s:%d:%d)",
	       spec, shadow, at, lineno, colno);
      else if (out < 0)
	error (0, 0, "dwarf_getscopevar: %s (+%d, %s:%d:%d): %s",
	       spec, shadow, at, lineno, colno, dwarf_errmsg (-1));
      else
	{
	  Dwarf_Op *cfa_ops = NULL;

#if _ELFUTILS_PREREQ(0,142)
	  size_t cfa_nops;
	  Dwarf_Addr bias;
	  Dwfl_Module *module = dwfl_addrmodule (dwfl, pc);
	  if (module != NULL)
	    {
	      // Try debug_frame first, then fall back on eh_frame.
	      Dwarf_CFI *cfi = dwfl_module_dwarf_cfi (module, &bias);
	      if (cfi != NULL)
		{
		  Dwarf_Frame *frame = NULL;
		  if (dwarf_cfi_addrframe (cfi, pc, &frame) == 0)
		    dwarf_frame_cfa (frame, &cfa_ops, &cfa_nops);
		}
	      if (cfa_ops == NULL)
		{
		  cfi = dwfl_module_eh_cfi (module, &bias);
		  if (cfi != NULL)
		    {
		      Dwarf_Frame *frame = NULL;
		      if (dwarf_cfi_addrframe (cfi, pc, &frame) == 0)
			dwarf_frame_cfa (frame, &cfa_ops, &cfa_nops);
		    }
		}
	    }
#endif

	  handle_variable (scopes, n, out, cubias, &vardie, pc, cfa_ops,
			   &argv[argi]);
	}
    }

  free (scopes);

  dwfl_end (dwfl);

  return 0;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
