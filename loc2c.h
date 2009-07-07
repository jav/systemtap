#include <elfutils/libdw.h>

struct obstack;			/* Use <obstack.h> */
struct location;		/* Opaque */

/* G++ 3.3 doesn't seem to like the __attribute__ constructs below. */
#if (__GNUG__ == 3) && (__GNUC_MINOR__ == 3)
#define __attribute__(x) /* nothing */
#endif

/* Translate a C fragment for the location expression, using *INPUT
   as the starting location, begin from scratch if *INPUT is null.
   If DW_OP_fbreg is used, it may have a subfragment computing from
   the FB_ATTR location expression.

   On errors, call FAIL, which should not return.  Any later errors will use
   FAIL and FAIL_ARG from the first c_translate_location call.

   On success, return the first fragment created, which is also chained
   onto (*INPUT)->next.  *INPUT is then updated with the new tail of that
   chain.  */
struct location *c_translate_location (struct obstack *,
				       void (*fail) (void *arg,
						     const char *fmt, ...)
				       __attribute__ ((noreturn,
						       format (printf, 2, 3))),
				       void *fail_arg,
				       void (*emit_address) (void *fail_arg,
							     struct obstack *,
							     Dwarf_Addr),
				       int indent,
				       Dwarf_Addr bias,
				       Dwarf_Addr pc_address,
				       const Dwarf_Op *locexpr,
				       size_t locexprlen,
				       struct location **input,
				       Dwarf_Attribute *fb_attr);

/* Translate a fragment to dereference the given DW_TAG_pointer_type DIE,
   where *INPUT is the location of the pointer with that type.  */
void c_translate_pointer (struct obstack *pool, int indent,
			  Dwarf_Addr dwbias, Dwarf_Die *typedie,
			  struct location **input);

/* Translate a fragment to index a DW_TAG_array_type DIE (turning the location
   of the array into the location of an element).  If IDX is non-null,
   it's a string of C code to emit in the fragment as the array index.
   If the index is a known constant, IDX should be null and CONST_IDX
   is used instead (this case can handle local arrays in registers).  */
void c_translate_array (struct obstack *pool, int indent,
			Dwarf_Addr dwbias, Dwarf_Die *typedie,
			struct location **input,
			const char *idx, Dwarf_Word const_idx);

/* Translate a fragment to compute the address of the input location
   and assign it to the variable TARGET.  This doesn't really do anything
   (it always emits "TARGET = addr;"), but it will barf if the location
   is a register or noncontiguous object.  */
void c_translate_addressof (struct obstack *pool, int indent,
			    Dwarf_Addr dwbias, Dwarf_Die *die,
			    Dwarf_Die *typedie,
			    struct location **input, const char *target);

/* Translate a fragment to fetch the value of variable or member DIE
   at the *INPUT location and store it in lvalue TARGET.
   This handles base integer types and bit fields, i.e. DW_TAG_base_type.  */
void c_translate_fetch (struct obstack *pool, int indent,
			Dwarf_Addr dwbias __attribute__ ((unused)),
			Dwarf_Die *die, Dwarf_Die *typedie,
			struct location **input, const char *target);

/* Translate a fragment to locate the value of variable or member DIE
   at the *INPUT location and set it to the C expression RVALUE.
   This handles base integer types and bit fields, i.e. DW_TAG_base_type.  */
void c_translate_store (struct obstack *pool, int indent,
			Dwarf_Addr dwbias __attribute__ ((unused)),
			Dwarf_Die *die, Dwarf_Die *typedie,
			struct location **input, const char *rvalue);

/* Translate a fragment to write the given pointer value,
   where *INPUT is the location of the pointer with that type. */
void
c_translate_pointer_store (struct obstack *pool, int indent,
                           Dwarf_Addr dwbias __attribute__ ((unused)),
                           Dwarf_Die *typedie, struct location **input,
                           const char *rvalue);

/* Translate a fragment to add an offset to the currently calculated
   address of the input location. Used for struct fields. Only works
   when location is already an actual base address. */
void
c_translate_add_offset (struct obstack *pool, int indent, const char *comment,
			Dwarf_Sword off, struct location **input);

/* Translate a C fragment for a direct argument VALUE.  On errors, call FAIL,
   which should not return.  Any later errors will use FAIL and FAIL_ARG from
   this translate call.  On success, return the fragment created. */
struct location *c_translate_argument (struct obstack *,
				       void (*fail) (void *arg,
						     const char *fmt, ...)
				       __attribute__ ((noreturn,
						       format (printf, 2, 3))),
				       void *fail_arg,
				       void (*emit_address) (void *fail_arg,
							     struct obstack *,
							     Dwarf_Addr),
				       int indent, const char *value);



/* Emit the C fragment built up at LOC (i.e., the return value from the
   first c_translate_location call made).  INDENT should match that
   passed to c_translate_* previously.

   Writes complete lines of C99, code forming a complete C block, to STREAM.
   Return value is true iff that code uses the `deref' runtime macros.  */
bool c_emit_location (FILE *stream, struct location *loc, int indent);

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
