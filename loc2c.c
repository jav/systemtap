#include <config.h>
#include <inttypes.h>
#include <stdbool.h>
#include <obstack.h>
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <assert.h>
#include "loc2c.h"

#define N_(x) x

#define STACK_TYPE	"intptr_t"  /* Must be the signed type.  */
#define UTYPE		"uintptr_t" /* Must be the unsigned type.  */
#define SFORMAT		"%" PRId64 "L"
#define UFORMAT		"%" PRIu64 "UL"
#define AFORMAT		"%#" PRIx64 "UL"
#define STACKFMT	"s%u"

struct location
{
  struct location *next;

  void (*fail) (void *arg, const char *fmt, ...)
    __attribute__ ((noreturn, format (printf, 2, 3)));
  void *fail_arg;
  void (*emit_address) (void *fail_arg, struct obstack *, Dwarf_Addr);

  const Dwarf_Op *ops;
  size_t nops;

  Dwarf_Word byte_size;

  enum
    {
      loc_address, loc_register, loc_noncontiguous,
      loc_decl, loc_fragment, loc_final
    } type;
  struct location *frame_base;
  union
  {
    struct			/* loc_address, loc_fragment, loc_final */
    {
      const char *declare;	/* Temporary that needs declared.  */
      char *program;		/* C fragment, leaves address in s0.  */
      unsigned int stack_depth;	/* Temporaries "s0..<N>" used by it.  */
      bool used_deref;		/* Program uses "deref" macro.  */
    } address;
    unsigned int regno;		/* loc_register */
    struct location *pieces;	/* loc_noncontiguous */
  };
};

static struct location *
alloc_location (struct obstack *pool, struct location *origin)
{
  struct location *loc = obstack_alloc (pool, sizeof *loc);
  loc->fail = origin->fail;
  loc->fail_arg = origin->fail_arg;
  loc->emit_address = origin->emit_address;
  loc->byte_size = 0;
  loc->frame_base = NULL;
  return loc;
}

#define FAIL(loc, fmt, ...) \
  (*(loc)->fail) ((loc)->fail_arg, fmt, ## __VA_ARGS__)

static void
default_emit_address (void *fail_arg __attribute__ ((unused)),
		      struct obstack *pool, Dwarf_Addr address)
{
  obstack_printf (pool, AFORMAT, address);
}

/* Synthesize a new loc_address using the program on the obstack.  */
static struct location *
new_synthetic_loc (struct obstack *pool, struct location *origin, bool deref)
{
  obstack_1grow (pool, '\0');
  char *program = obstack_finish (pool);

  struct location *loc = alloc_location (pool, origin);
  loc->next = NULL;
  loc->byte_size = 0;
  loc->type = loc_address;
  loc->address.program = program;
  loc->address.stack_depth = 0;
  loc->address.declare = NULL;
  loc->address.used_deref = deref;

  if (origin->type == loc_register)
    {
      loc->ops = origin->ops;
      loc->nops = origin->nops;
    }
  else
    {
      loc->ops = NULL;
      loc->nops = 0;
    }

  return loc;
}


/* Die in the middle of an expression.  */
static struct location *
lose (struct location *loc,
      const char *failure, const Dwarf_Op *lexpr, size_t i)
{
  FAIL (loc, N_("%s in DWARF expression [%Zu] at %" PRIu64
		" (%#x: %" PRId64 ", %" PRId64 ")"),
	failure, i, lexpr[i].offset,
	lexpr[i].atom, lexpr[i].number, lexpr[i].number2);
  return NULL;
}

/* Translate a (constrained) DWARF expression into C code
   emitted to the obstack POOL.  INDENT is the number of indentation levels.
   ADDRBIAS is the difference between runtime and Dwarf info addresses.
   INPUT is null or an expression to be initially pushed on the stack.
   If NEED_FB is null, fail on DW_OP_fbreg, else set *NEED_FB to true
   and emit "frame_base" for it.  On success, set *MAX_STACK to the number
   of stack slots required.  On failure, set *LOSER to the index in EXPR
   of the operation we could not handle.

   Returns a failure message or null for success.  */

static const char *
translate (struct obstack *pool, int indent, Dwarf_Addr addrbias,
	   const Dwarf_Op *expr, const size_t len,
	   struct location *input,
	   bool *need_fb, size_t *loser,
	   struct location *loc)
{
  loc->ops = expr;
  loc->nops = len;

#define DIE(msg) return (*loser = i, N_(msg))

#define emit(fmt, ...) obstack_printf (pool, fmt, ## __VA_ARGS__)

  unsigned int stack_depth = 0, max_stack = 0;
  inline void deepen (void)
    {
      if (stack_depth == max_stack)
	++max_stack;
    }

#define POP(var)							      \
    if (stack_depth > 0)						      \
      --stack_depth;							      \
    else if (tos_register != -1)					      \
      fetch_tos_register ();						      \
    else								      \
      goto underflow;							      \
    int var = stack_depth
#define PUSH 		(deepen (), stack_depth++)
#define STACK(idx)	(stack_depth - 1 - (idx))

  /* Don't put stack operations in the arguments to this.  */
#define push(fmt, ...) \
  emit ("%*s" STACKFMT " = " fmt ";\n", indent * 2, "", PUSH, ## __VA_ARGS__)

  int tos_register = -1;
  inline void fetch_tos_register (void)
    {
      deepen ();
      emit ("%*s" STACKFMT " = fetch_register (%d);\n",
	    indent * 2, "", stack_depth, tos_register);
      tos_register = -1;
    }

  if (input != NULL)
    switch (input->type)
      {
      case loc_address:
	push ("addr");
	break;

      case loc_register:
	tos_register = input->regno;
	break;

      default:
	abort ();
	break;
      }

  size_t i;

  bool used_deref = false;
  inline const char *finish (struct location *piece)
    {
      if (stack_depth > 1)
	DIE ("multiple values left on stack");
      if (stack_depth == 1)
	{
	  obstack_1grow (pool, '\0');
	  char *program = obstack_finish (pool);
	  piece->type = loc_address;
	  piece->address.declare = NULL;
	  piece->address.program = program;
	  piece->address.stack_depth = max_stack;
	  piece->address.used_deref = used_deref;
	  used_deref = false;
	}
      else if (tos_register == -1)
	DIE ("stack underflow");
      else if (obstack_object_size (pool) != 0)
	DIE ("register value must stand alone in location expression");
      else
	{
	  piece->type = loc_register;
	  piece->regno = tos_register;
	}
      return NULL;
    }

  struct location *pieces = NULL, **tailpiece = &pieces;
  size_t piece_expr_start = 0;
  Dwarf_Word piece_total_bytes = 0;
  for (i = 0; i < len; ++i)
    {
      unsigned int reg;
      uint_fast8_t sp;
      Dwarf_Word value;

      switch (expr[i].atom)
	{
	  /* Basic stack operations.  */
	case DW_OP_nop:
	  break;

	case DW_OP_dup:
	  if (stack_depth < 1)
	    goto underflow;
	  else
	    {
	      unsigned int tos = STACK (0);
	      push (STACKFMT, tos);
	    }
	  break;

	case DW_OP_drop:
	  POP (ignore);
	  emit ("%*s/* drop " STACKFMT "*/\n", indent * 2, "", ignore);
	  break;

	case DW_OP_pick:
	  sp = expr[i].number;
	op_pick:
	  if (sp >= stack_depth)
	    goto underflow;
	  sp = STACK (sp);
	  push (STACKFMT, sp);
	  break;

	case DW_OP_over:
	  sp = 1;
	  goto op_pick;

	case DW_OP_swap:
	  if (stack_depth < 2)
	    goto underflow;
	  deepen ();		/* Use a temporary slot.  */
	  emit ("%*s"
		STACKFMT " = " STACKFMT ", "
		STACKFMT " = " STACKFMT ", "
		STACKFMT " = " STACKFMT ";\n",
		indent * 2, "",
		STACK (-1), STACK (0),
		STACK (0), STACK (1),
		STACK (1), STACK (-1));
	  break;

	case DW_OP_rot:
	  if (stack_depth < 3)
	    goto underflow;
	  deepen ();		/* Use a temporary slot.  */
	  emit ("%*s"
		STACKFMT " = " STACKFMT ", "
		STACKFMT " = " STACKFMT ", "
		STACKFMT " = " STACKFMT ", "
		STACKFMT " = " STACKFMT ";\n",
		indent * 2, "",
		STACK (-1), STACK (0),
		STACK (0), STACK (1),
		STACK (1), STACK (2),
		STACK (3), STACK (-1));
	  break;


	  /* Control flow operations.  */
	case DW_OP_skip:
	  {
	    Dwarf_Off target = expr[i].offset + 3 + expr[i].number;
	    while (i + 1 < len && expr[i + 1].offset < target)
	      ++i;
	    if (expr[i + 1].offset != target)
	      DIE ("invalid skip target");
	    break;
	  }

	case DW_OP_bra:
	  DIE ("conditional branches not supported");
	  break;


	  /* Memory access.  */
	case DW_OP_deref:
	  {
	    POP (addr);
	    push ("deref (sizeof (void *), " STACKFMT ")", addr);
	    used_deref = true;
	  }
	  break;

	case DW_OP_deref_size:
	  {
	    POP (addr);
	    push ("deref (" UFORMAT ", " STACKFMT ")",
		  expr[i].number, addr);
	    used_deref = true;
	  }
	  break;

	case DW_OP_xderef:
	  {
	    POP (addr);
	    POP (as);
	    push ("xderef (sizeof (void *), " STACKFMT ", " STACKFMT ")",
		  addr, as);
	    used_deref = true;
	  }
	  break;

	case DW_OP_xderef_size:
	  {
	    POP (addr);
	    POP (as);
	    push ("xderef (" UFORMAT ", " STACKFMT ", " STACKFMT ")",
		  expr[i].number, addr, as);
	    used_deref = true;
	  }
	  break;

	  /* Constant-value operations.  */

	case DW_OP_addr:
	  emit ("%*s" STACKFMT " = ", indent * 2, "", PUSH);
	  (*loc->emit_address) (loc->fail_arg, pool,
				addrbias + expr[i].number);
	  emit (";\n");
	  break;

	case DW_OP_lit0 ... DW_OP_lit31:
	  value = expr[i].atom - DW_OP_lit0;
	  goto op_const;

	case DW_OP_const1u:
	case DW_OP_const1s:
	case DW_OP_const2u:
	case DW_OP_const2s:
	case DW_OP_const4u:
	case DW_OP_const4s:
	case DW_OP_const8u:
	case DW_OP_const8s:
	case DW_OP_constu:
	case DW_OP_consts:
	  value = expr[i].number;
	op_const:
	  push (SFORMAT, value);
	  break;

	  /* Arithmetic operations.  */
#define UNOP(dw_op, c_op)						      \
	case DW_OP_##dw_op:						      \
	  {								      \
	    POP (tos);							      \
	    push ("%s (" STACKFMT ")", #c_op, tos);			      \
	  }								      \
	  break
#define BINOP(dw_op, c_op)						      \
	case DW_OP_##dw_op:						      \
	  {								      \
	    POP (b);							      \
	    POP (a);							      \
	    push (STACKFMT " %s " STACKFMT, a, #c_op, b);		      \
	  }								      \
	  break

	  UNOP (abs, op_abs);
	  BINOP (and, &);
	  BINOP (div, /);
	  BINOP (minus, -);
	  BINOP (mod, %);
	  BINOP (mul, *);
	  UNOP (neg, -);
	  UNOP (not, ~);
	  BINOP (or, |);
	  BINOP (plus, +);
	  BINOP (shl, <<);
	  BINOP (shra, >>);
	  BINOP (xor, ^);

	  /* Comparisons are binary operators too.  */
	  BINOP (le, <=);
	  BINOP (ge, >=);
	  BINOP (eq, ==);
	  BINOP (lt, <);
	  BINOP (gt, >);
	  BINOP (ne, !=);

#undef	UNOP
#undef	BINOP

	case DW_OP_shr:
	  {
	    POP (b);
	    POP (a);
	    push ("(%s) " STACKFMT " >> (%s)" STACKFMT,
		  UTYPE, a, UTYPE, b);
	    break;
	  }

	case DW_OP_plus_uconst:
	  {
	    POP (x);
	    push (STACKFMT " + " UFORMAT, x, expr[i].number);
	  }
	  break;


	  /* Register-relative addressing.  */
	case DW_OP_breg0 ... DW_OP_breg31:
	  reg = expr[i].atom - DW_OP_breg0;
	  value = expr[i].number;
	  goto op_breg;

	case DW_OP_bregx:
	  reg = expr[i].number;
	  value = expr[i].number2;
	op_breg:
	  push ("fetch_register (%u) + " SFORMAT, reg, value);
	  break;

	case DW_OP_fbreg:
	  if (need_fb == NULL)
	    DIE ("DW_OP_fbreg from DW_AT_frame_base");
	  *need_fb = true;
	  push ("frame_base + " SFORMAT, expr[i].number);
	  break;

	  /* Direct register contents.  */
	case DW_OP_reg0 ... DW_OP_reg31:
	  reg = expr[i].atom - DW_OP_reg0;
	  goto op_reg;

	case DW_OP_regx:
	  reg = expr[i].number;
	op_reg:
	  tos_register = reg;
	  break;

	  /* Special magic.  */
	case DW_OP_piece:
	  if (stack_depth > 1)
	    /* If this ever happens we could copy the program.  */
	    DIE ("DW_OP_piece left multiple values on stack");
	  else
	    {
	      /* The obstack has a pending program for loc_address,
		 so we must finish that piece off before we can
		 allocate again.  */
	      struct location temp_piece =
		{
		  .fail = loc->fail,
		  .fail_arg = loc->fail_arg,
		  .emit_address = loc->emit_address,
		  .frame_base = NULL,
		  .ops = &expr[piece_expr_start],
		  .nops = i - piece_expr_start,
		};
	      const char *failure = finish (&temp_piece);
	      if (failure != NULL)
		return failure;

	      struct location *piece = obstack_alloc (pool, sizeof *piece);
	      *piece = temp_piece;

	      piece_expr_start = i + 1;

	      piece_total_bytes += piece->byte_size = expr[i].number;

	      *tailpiece = piece;
	      tailpiece = &piece->next;
	      piece->next = NULL;
	    }
	  break;

	case DW_OP_push_object_address:
	  DIE ("XXX DW_OP_push_object_address");
	  break;

	default:
	  DIE ("unrecognized operation");
	  break;
	}
    }

  if (pieces == NULL)
    return finish (loc);

  if (piece_expr_start != i)
    DIE ("extra operations after last DW_OP_piece");

  loc->type = loc_noncontiguous;
  loc->pieces = pieces;
  loc->byte_size = piece_total_bytes;

  return NULL;

 underflow:
  DIE ("stack underflow");

#undef emit
#undef push
#undef PUSH
#undef POP
#undef STACK
#undef DIE
}

/* Translate a location starting from an address or nothing.  */
static struct location *
location_from_address (struct obstack *pool,
		       void (*fail) (void *arg, const char *fmt, ...)
		         __attribute__ ((noreturn, format (printf, 2, 3))),
		       void *fail_arg,
		       void (*emit_address) (void *fail_arg,
					     struct obstack *, Dwarf_Addr),
		       int indent, Dwarf_Addr dwbias,
		       const Dwarf_Op *expr, size_t len, Dwarf_Addr address,
		       struct location **input, Dwarf_Attribute *fb_attr)
{
  struct location *loc = obstack_alloc (pool, sizeof *loc);
  loc->fail = *input == NULL ? fail : (*input)->fail;
  loc->fail_arg = *input == NULL ? fail_arg : (*input)->fail_arg;
  loc->emit_address = *input == NULL ? emit_address : (*input)->emit_address;
  loc->byte_size = 0;
  loc->frame_base = NULL;

  bool need_fb = false;
  size_t loser;
  const char *failure = translate (pool, indent + 1, dwbias, expr, len,
				   *input, &need_fb, &loser, loc);
  if (failure != NULL)
    return lose (loc, failure, expr, loser);

  loc->next = NULL;
  if (need_fb)
    {
      /* The main expression uses DW_OP_fbreg, so we need to compute
	 the DW_AT_frame_base attribute expression's value first.  */

      if (fb_attr == NULL)
	FAIL (loc, N_("required DW_AT_frame_base attribute not supplied"));

      Dwarf_Op *fb_expr;
      size_t fb_len;
      switch (dwarf_getlocation_addr (fb_attr, address - dwbias,
				      &fb_expr, &fb_len, 1))
	{
	case 1:			/* Should always happen.  */
	  if (fb_len == 0)
	    goto fb_inaccessible;
	  break;

	default:		/* Shouldn't happen.  */
	case -1:
	  FAIL (loc, N_("dwarf_getlocation_addr (form %#x): %s"),
		dwarf_whatform (fb_attr), dwarf_errmsg (-1));
	  return NULL;

	case 0:			/* Shouldn't happen.  */
	fb_inaccessible:
	  FAIL (loc, N_("DW_AT_frame_base not accessible at this address"));
	  return NULL;
	}

      loc->frame_base = alloc_location (pool, loc);
      failure = translate (pool, indent + 1, dwbias, fb_expr, fb_len, NULL,
			   NULL, &loser, loc->frame_base);
      if (failure != NULL)
	return lose (loc, failure, fb_expr, loser);
    }

  if (*input != NULL)
    (*input)->next = loc;
  *input = loc;

  return loc;
}

/* Translate a location starting from a non-address "on the top of the
   stack".  The *INPUT location is a register name or noncontiguous
   object specification, and this expression wants to find the "address"
   of an object relative to that "address".  */

static struct location *
location_relative (struct obstack *pool,
		   int indent, Dwarf_Addr dwbias,
		   const Dwarf_Op *expr, size_t len, Dwarf_Addr address,
		   struct location **input, Dwarf_Attribute *fb_attr)
{
  Dwarf_Sword *stack;
  unsigned int stack_depth = 0, max_stack = 0;
  inline void deepen (void)
    {
      if (stack_depth == max_stack)
	{
	  ++max_stack;
	  obstack_blank (pool, sizeof stack[0]);
	  stack = (void *) obstack_base (pool);
	}
    }

#define POP(var)							      \
    if (stack_depth > 0)						      \
      --stack_depth;							      \
    else								      \
      goto underflow;							      \
    int var = stack_depth
#define PUSH 		(deepen (), stack_depth++)
#define STACK(idx)	(stack_depth - 1 - (idx))
#define STACKWORD(idx)	stack[STACK (idx)]

  /* Don't put stack operations in the arguments to this.  */
#define push(value) (stack[PUSH] = (value))

  const char *failure = NULL;
#define DIE(msg) do { failure = N_(msg); goto fail; } while (0)

  struct location *head = NULL;
  size_t i;
  for (i = 0; i < len; ++i)
    {
      uint_fast8_t sp;
      Dwarf_Word value;

      switch (expr[i].atom)
	{
	  /* Basic stack operations.  */
	case DW_OP_nop:
	  break;

	case DW_OP_dup:
	  if (stack_depth < 1)
	    goto underflow;
	  else
	    {
	      unsigned int tos = STACK (0);
	      push (stack[tos]);
	    }
	  break;

	case DW_OP_drop:
	  if (stack_depth > 0)
	    --stack_depth;
	  else if (*input != NULL)
	    /* Mark that we have consumed the input.  */
	    *input = NULL;
	  else
	    /* Hits if cleared above, or if we had no input at all.  */
	    goto underflow;
	  break;

	case DW_OP_pick:
	  sp = expr[i].number;
	op_pick:
	  if (sp >= stack_depth)
	    goto underflow;
	  sp = STACK (sp);
	  push (stack[sp]);
	  break;

	case DW_OP_over:
	  sp = 1;
	  goto op_pick;

	case DW_OP_swap:
	  if (stack_depth < 2)
	    goto underflow;
	  deepen ();		/* Use a temporary slot.  */
	  STACKWORD (-1) = STACKWORD (0);
	  STACKWORD (0) = STACKWORD (1);
	  STACKWORD (1) = STACKWORD (-1);
	  break;

	case DW_OP_rot:
	  if (stack_depth < 3)
	    goto underflow;
	  deepen ();		/* Use a temporary slot.  */
	  STACKWORD (-1) = STACKWORD (0);
	  STACKWORD (0) = STACKWORD (1);
	  STACKWORD (2) = STACKWORD (2);
	  STACKWORD (2) = STACKWORD (-1);
	  break;


	  /* Control flow operations.  */
	case DW_OP_bra:
	  {
	    POP (taken);
	    if (stack[taken] == 0)
	      break;
	  }
	  /*FALLTHROUGH*/

	case DW_OP_skip:
	  {
	    Dwarf_Off target = expr[i].offset + 3 + expr[i].number;
	    while (i + 1 < len && expr[i + 1].offset < target)
	      ++i;
	    if (expr[i + 1].offset != target)
	      DIE ("invalid skip target");
	    break;
	  }

	  /* Memory access.  */
	case DW_OP_deref:
	case DW_OP_deref_size:
	case DW_OP_xderef:
	case DW_OP_xderef_size:

	  /* Register-relative addressing.  */
	case DW_OP_breg0 ... DW_OP_breg31:
	case DW_OP_bregx:
	case DW_OP_fbreg:

	  /* This started from a register, but now it's following a pointer.
	     So we can do the translation starting from address here.  */
	  return location_from_address (pool, NULL, NULL, NULL, indent, dwbias,
					expr, len, address, input, fb_attr);


	  /* Constant-value operations.  */
	case DW_OP_addr:
	  DIE ("static calculation depends on load-time address");
	  push (dwbias + expr[i].number);
	  break;

	case DW_OP_lit0 ... DW_OP_lit31:
	  value = expr[i].atom - DW_OP_lit0;
	  goto op_const;

	case DW_OP_const1u:
	case DW_OP_const1s:
	case DW_OP_const2u:
	case DW_OP_const2s:
	case DW_OP_const4u:
	case DW_OP_const4s:
	case DW_OP_const8u:
	case DW_OP_const8s:
	case DW_OP_constu:
	case DW_OP_consts:
	  value = expr[i].number;
	op_const:
	  push (value);
	  break;

	  /* Arithmetic operations.  */
#define UNOP(dw_op, c_op)						      \
	case DW_OP_##dw_op:						      \
	  {								      \
	    POP (tos);							      \
	    push (c_op (stack[tos])); 					      \
	  }								      \
	  break
#define BINOP(dw_op, c_op)						      \
	case DW_OP_##dw_op:						      \
	  {								      \
	    POP (b);							      \
	    POP (a);							      \
 	    push (stack[a] c_op stack[b]);				      \
	  }								      \
	  break

#define op_abs(x) (x < 0 ? -x : x)
	  UNOP (abs, op_abs);
	  BINOP (and, &);
	  BINOP (div, /);
	  BINOP (mod, %);
	  BINOP (mul, *);
	  UNOP (neg, -);
	  UNOP (not, ~);
	  BINOP (or, |);
	  BINOP (shl, <<);
	  BINOP (shra, >>);
	  BINOP (xor, ^);

	  /* Comparisons are binary operators too.  */
	  BINOP (le, <=);
	  BINOP (ge, >=);
	  BINOP (eq, ==);
	  BINOP (lt, <);
	  BINOP (gt, >);
	  BINOP (ne, !=);

#undef	UNOP
#undef	BINOP

	case DW_OP_shr:
	  {
	    POP (b);
	    POP (a);
	    push ((Dwarf_Word) stack[a] >> (Dwarf_Word) stack[b]);
	    break;
	  }

	  /* Simple addition we may be able to handle relative to
	     the starting register name.  */
	case DW_OP_minus:
	  {
	    POP (tos);
	    value = -stack[tos];
	    goto plus;
	  }
	case DW_OP_plus:
	  {
	    POP (tos);
	    value = stack[tos];
	    goto plus;
	  }
	case DW_OP_plus_uconst:
	  value = expr[i].number;
	plus:
	  if (stack_depth > 0)
	    {
	      /* It's just private diddling after all.  */
	      POP (a);
	      push (stack[a] + value);
	      break;
	    }
	  if (*input == NULL)
	    goto underflow;

	  /* This is the primary real-world case: the expression takes
	     the input address and adds a constant offset.  */

	  while ((*input)->type == loc_noncontiguous)
	    {
	      /* We are starting from a noncontiguous object (DW_OP_piece).
		 Find the piece we want.  */

	      struct location *piece = (*input)->pieces;
	      while (piece != NULL && value >= piece->byte_size)
		{
		  value -= piece->byte_size;
		  piece = piece->next;
		}
	      if (piece == NULL)
		DIE ("offset outside available pieces");

	      *input = piece;
	    }

	  switch ((*input)->type)
	    {
	    case loc_address:
	      {
		/* The piece we want is actually in memory.  Use the same
		   program to compute the address from the preceding input.  */

		struct location *loc = alloc_location (pool, *input);
		*loc = **input;
		if (head == NULL)
		  head = loc;
		(*input)->next = loc;
		if (value == 0)
		  {
		    /* The piece addresses exactly where we want to go.  */
		    loc->next = NULL;
		    *input = loc;
		  }
		else
		  {
		    /* Add a second fragment to offset the piece address.  */
		    obstack_printf (pool, "%*saddr += " SFORMAT "\n",
				    indent * 2, "", value);
		    *input = loc->next = new_synthetic_loc (pool, *input,
							    false);
		  }

		if (i + 1 < len)
		  {
		    /* This expression keeps going, but further
		       computations now have an address to start with.
		       So we can punt to the address computation generator.  */
		    loc = location_from_address (pool, NULL, NULL, NULL,
						 indent, dwbias,
						 &expr[i + 1], len - i - 1,
						 address, input, fb_attr);
		    if (loc == NULL)
		      return NULL;
		  }

		/* That's all she wrote.  */
		return head;
	      }

	    case loc_register:
	      // XXX

	    default:
	      abort ();
	    }
	  break;

	  /* Direct register contents.  */
	case DW_OP_reg0 ... DW_OP_reg31:
	case DW_OP_regx:
	  DIE ("register");
	  break;

	  /* Special magic.  */
	case DW_OP_piece:
	  DIE ("DW_OP_piece");
	  break;

	case DW_OP_push_object_address:
	  DIE ("XXX DW_OP_push_object_address");
	  break;

	default:
	  DIE ("unrecognized operation");
	  break;
	}
    }

  if (stack_depth > 1)
    DIE ("multiple values left on stack");

  if (stack_depth > 0)		/* stack_depth == 1 */
    {
      if (*input != NULL)
	DIE ("multiple values left on stack");

      /* Could handle this if it ever actually happened.  */
      DIE ("relative expression computed constant");
    }

  return head;

 underflow:
  if (*input == NULL)
    DIE ("stack underflow");
  else
    DIE ("cannot handle location expression");

 fail:
  return lose (*input, failure, expr, i);
}


/* Translate a C fragment for the location expression, using *INPUT
   as the starting location, begin from scratch if *INPUT is null.
   If DW_OP_fbreg is used, it may have a subfragment computing from
   the FB_ATTR location expression.

   On errors, call FAIL and never return.  On success, return the
   first fragment created, which is also chained onto (*INPUT)->next.
   *INPUT is then updated with the new tail of that chain.  */

struct location *
c_translate_location (struct obstack *pool,
		      void (*fail) (void *arg, const char *fmt, ...)
		        __attribute__ ((noreturn, format (printf, 2, 3))),
		      void *fail_arg,
		      void (*emit_address) (void *fail_arg,
					    struct obstack *, Dwarf_Addr),
		      int indent, Dwarf_Addr dwbias, Dwarf_Addr pc_address,
		      const Dwarf_Op *expr, size_t len,
		      struct location **input, Dwarf_Attribute *fb_attr)
{
  indent += 2;

  switch (*input == NULL ? loc_address : (*input)->type)
    {
    case loc_address:
      /* We have a previous address computation.
	 This expression will compute starting with that on the stack.  */
      return location_from_address (pool, fail, fail_arg,
				    emit_address ?: &default_emit_address,
				    indent, dwbias, expr, len, pc_address,
				    input, fb_attr);

    case loc_noncontiguous:
    case loc_register:
      /* The starting point is not an address computation, but a
	 register.  We can only handle limited computations from here.  */
      return location_relative (pool, indent, dwbias, expr, len, pc_address,
				input, fb_attr);

    default:
      abort ();
      break;
    }

  return NULL;
}

/* Emit "uintNN_t TARGET = ...;".  */
static bool
emit_base_fetch (struct obstack *pool, Dwarf_Word byte_size,
                 bool signed_p, const char *target, struct location *loc)
{
  bool deref = false;
  /* int i; */

  /* Emit size/signed coercion. */ 
  obstack_printf (pool, "{ ");
  obstack_printf (pool, "%sint%u_t value = ", 
                  (signed_p ? "" : "u"), (unsigned)(byte_size * 8));

  switch (loc->type)
    {
    case loc_address:
      if (byte_size != 0 && byte_size != (Dwarf_Word) -1)
	obstack_printf (pool, "deref (%" PRIu64 ", addr);", byte_size);
      else
	obstack_printf (pool, "deref (sizeof %s, addr);", target);
      deref = true;
      break;

    case loc_register:
      obstack_printf (pool, "fetch_register (%u);", loc->regno);
      break;

    case loc_noncontiguous:
      FAIL (loc, N_("noncontiguous location for base fetch"));
      break;

    default:
      abort ();
      break;
    }

  obstack_printf (pool, "%s = value; ", target);
  obstack_printf (pool, "}");
  return deref;
}

/* Emit "... = RVALUE;".  */
static bool
emit_base_store (struct obstack *pool, Dwarf_Word byte_size,
		 const char *rvalue, struct location *loc)
{
  switch (loc->type)
    {
    case loc_address:
      if (byte_size != 0 && byte_size != (Dwarf_Word) -1)
	obstack_printf (pool, "store_deref (%" PRIu64 ", addr, %s); ",
			byte_size, rvalue);
      else
	obstack_printf (pool, "store_deref (sizeof %s, addr, %s); ",
			rvalue, rvalue);
      return true;

    case loc_register:
      obstack_printf (pool, "store_register (%u, %s);", loc->regno, rvalue);
      break;

    case loc_noncontiguous:
      FAIL (loc, N_("noncontiguous location for base store"));
      break;

    default:
      abort ();
      break;
    }

  return false;
}


/* Slice up an object into pieces no larger than MAX_PIECE_BYTES,
   yielding a loc_noncontiguous location unless LOC is small enough.  */
static struct location *
discontiguify (struct obstack *pool, int indent, struct location *loc,
	       Dwarf_Word total_bytes, Dwarf_Word max_piece_bytes)
{
  inline bool pieces_small_enough (void)
    {
      if (loc->type != loc_noncontiguous)
	return (loc->byte_size ?: total_bytes) <= max_piece_bytes;
      struct location *p;
      for (p = loc->pieces; p != NULL; p = p->next)
	if (p->byte_size > max_piece_bytes)
	  return false;
      return true;
    }

  if (pieces_small_enough ())
    return loc;

  struct location *noncontig = alloc_location (pool, loc);
  noncontig->next = NULL;
  noncontig->type = loc_noncontiguous;
  noncontig->byte_size = total_bytes;
  noncontig->pieces = NULL;
  struct location **tailpiece = &noncontig->pieces;
  inline void add (struct location *piece)
    {
      *tailpiece = piece;
      tailpiece = &piece->next;
    }

  switch (loc->type)
    {
    case loc_address:
      {
	/* Synthesize a piece that sets "container_addr" to the computed
	   address of the whole object.  Each piece will refer to this.  */
	obstack_printf (pool, "%*scontainer_addr = addr;\n",
			indent++ * 2, "");
	loc->next = new_synthetic_loc (pool, loc, false);
	loc->next->byte_size = loc->byte_size;
	loc->next->type = loc_fragment;
	loc->next->address.declare = "container_addr";
	loc = loc->next;

	/* Synthesize pieces that just compute "container_addr + N".  */
	Dwarf_Word offset = 0;
	while (total_bytes - offset > 0)
	  {
	    Dwarf_Word size = total_bytes - offset;
	    if (size > max_piece_bytes)
	      size = max_piece_bytes;

	    obstack_printf (pool, "%*saddr = container_addr + " UFORMAT ";\n",
			    indent * 2, "", offset);
	    struct location *piece = new_synthetic_loc (pool, loc, false);
	    piece->byte_size = size;
	    add (piece);

	    offset += size;
	  }

	--indent;
	break;
      }

    case loc_register:
      FAIL (loc, N_("single register too big for fetch/store ???"));
      break;

    case loc_noncontiguous:
      /* Could be handled if it ever happened.  */
      FAIL (loc, N_("discontiguify of noncontiguous location not supported"));
      break;

    default:
      abort ();
      break;
    }

  loc->next = noncontig;
  return noncontig;
}

/* Make a fragment that declares a union such as:
    union {
      char bytes[8];
      struct {
        uint32_t p0;
        uint32_t p4;
      } pieces __attribute__ ((packed));
      uint64_t whole;
    } u;
*/
static void
declare_noncontig_union (struct obstack *pool, int indent,
			 struct location **input, struct location *loc)
{
  obstack_printf (pool, "%*sunion {\n", indent++ * 2, "");

  obstack_printf (pool, "%*schar bytes[%" PRIu64 "];\n",
		  indent * 2, "", loc->byte_size);

  obstack_printf (pool, "%*sstruct {\n", indent++ * 2, "");

  Dwarf_Word offset = 0;
  struct location *p;
  for (p = loc->pieces; p != NULL; p = p->next)
    {
      obstack_printf (pool, "%*suint%" PRIu64 "_t p%" PRIu64 ";\n",
		      indent * 2, "", p->byte_size * 8, offset);
      offset += p->byte_size;
    }

  obstack_printf (pool, "%*s} pieces __attribute__ ((packed));\n",
		  --indent * 2, "");

  obstack_printf (pool, "%*suint%" PRIu64 "_t whole;\n",
		  indent * 2, "", loc->byte_size * 8);

  obstack_printf (pool, "%*s} u;\n", --indent * 2, "");

  loc = new_synthetic_loc (pool, *input, false);
  loc->type = loc_decl;
  (*input)->next = loc;
  *input = loc;
}

/* Determine the byte size of a base type.  */
static Dwarf_Word
base_byte_size (Dwarf_Die *typedie, struct location *origin)
{
  assert (dwarf_tag (typedie) == DW_TAG_base_type ||
	  dwarf_tag (typedie) == DW_TAG_enumeration_type);

  Dwarf_Attribute attr_mem;
  Dwarf_Word size;
  if (dwarf_attr_integrate (typedie, DW_AT_byte_size, &attr_mem) != NULL
      && dwarf_formudata (&attr_mem, &size) == 0)
    return size;

  FAIL (origin,
	 N_("cannot get byte_size attribute for type %s: %s"),
	 dwarf_diename (typedie) ?: "<anonymous>",
	 dwarf_errmsg (-1));
  return -1;
}

static Dwarf_Word
base_encoding (Dwarf_Die *typedie, struct location *origin)
{
  assert (dwarf_tag (typedie) == DW_TAG_base_type ||
	  dwarf_tag (typedie) == DW_TAG_enumeration_type);

  Dwarf_Attribute attr_mem;
  Dwarf_Word encoding;
  if (dwarf_attr_integrate (typedie, DW_AT_encoding, &attr_mem) != NULL
      && dwarf_formudata (&attr_mem, &encoding) == 0)
    return encoding;

  FAIL (origin,
	 N_("cannot get encoding attribute for type %s: %s"),
	 dwarf_diename (typedie) ?: "<anonymous>",
	 dwarf_errmsg (-1));
  return -1;
}



/* Fetch the bitfield parameters.  */
static void
get_bitfield (struct location *loc,
	      Dwarf_Die *die, Dwarf_Word *bit_offset, Dwarf_Word *bit_size)
{
  Dwarf_Attribute attr_mem;
  if (dwarf_attr_integrate (die, DW_AT_bit_offset, &attr_mem) == NULL
      || dwarf_formudata (&attr_mem, bit_offset) != 0
      || dwarf_attr_integrate (die, DW_AT_bit_size, &attr_mem) == NULL
      || dwarf_formudata (&attr_mem, bit_size) != 0)
    FAIL (loc, N_("cannot get bit field parameters: %s"), dwarf_errmsg (-1));
}

/* Translate a fragment to fetch the base-type value of BYTE_SIZE bytes
   at the *INPUT location and store it in lvalue TARGET.  */
static void
translate_base_fetch (struct obstack *pool, int indent, Dwarf_Word byte_size,
                      bool signed_p, struct location **input, const char *target)
{
  bool deref = false;

  if ((*input)->type == loc_noncontiguous)
    {
      struct location *p = (*input)->pieces;

      declare_noncontig_union (pool, indent, input, *input);

      Dwarf_Word offset = 0;
      char piece[sizeof "u.pieces.p" + 20] = "u.pieces.p";
      while (p != NULL)
	{
	  struct location *newp = obstack_alloc (pool, sizeof *newp);
	  *newp = *p;
	  newp->next = NULL;
	  (*input)->next = newp;
	  *input = newp;

	  snprintf (&piece[sizeof "u.pieces.p" - 1], 20, "%" PRIu64, offset);
	  translate_base_fetch (pool, indent, p->byte_size, signed_p /* ? */,
                                input, piece);
	  (*input)->type = loc_fragment;

	  offset += p->byte_size;
	  p = p->next;
	}

      obstack_printf (pool, "%*s%s = u.whole;\n", indent * 2, "", target);
    }
  else
    switch (byte_size)
      {
      case 0:			/* Special case, means address size.  */
      case 1:
      case 2:
      case 4:
      case 8:
	obstack_printf (pool, "%*s", indent * 2, "");
	deref = emit_base_fetch (pool, byte_size, signed_p, target, *input);
	obstack_printf (pool, "\n");
	break;

      default:
	/* Could handle this generating call to memcpy equivalent.  */
	FAIL (*input, N_("fetch is larger than base integer types"));
	break;
      }

  struct location *loc = new_synthetic_loc (pool, *input, deref);
  loc->byte_size = byte_size;
  loc->type = loc_final;
  (*input)->next = loc;
  *input = loc;
}

/* Determine the maximum size of a base type, from some DIE in the CU.  */
static Dwarf_Word
max_fetch_size (struct location *loc, Dwarf_Die *die)
{
  Dwarf_Die cu_mem;
  uint8_t address_size;
  Dwarf_Die *cu = dwarf_diecu (die, &cu_mem, &address_size, NULL);
  if (cu == NULL)
    FAIL (loc, N_("cannot determine CU address size from %s: %s"),
	  dwarf_diename (die), dwarf_errmsg (-1));

  return address_size;
}

/* Translate a fragment to fetch the value of variable or member DIE
   at the *INPUT location and store it in lvalue TARGET.  */
void
c_translate_fetch (struct obstack *pool, int indent,
		   Dwarf_Addr dwbias __attribute__ ((unused)),
		   Dwarf_Die *die, Dwarf_Die *typedie,
		   struct location **input, const char *target)
{
  ++indent;

  Dwarf_Attribute size_attr;
  Dwarf_Word byte_size;
  if (dwarf_attr_integrate (die, DW_AT_byte_size, &size_attr) == NULL
      || dwarf_formudata (&size_attr, &byte_size) != 0)
    byte_size = base_byte_size (typedie, *input);

  Dwarf_Attribute encoding_attr;
  Dwarf_Word encoding;
  if (dwarf_attr_integrate (die, DW_AT_encoding, &encoding_attr) == NULL
      || dwarf_formudata (&encoding_attr, &encoding) != 0)
    encoding = base_encoding (typedie, *input);
  bool signed_p = (encoding == DW_ATE_signed 
                   || encoding == DW_ATE_signed_char);

  *input = discontiguify (pool, indent, *input, byte_size,
			  max_fetch_size (*input, die));

  if (dwarf_hasattr_integrate (die, DW_AT_bit_offset))
    {
      /* This is a bit field.  Fetch the containing base type into a
	 temporary variable.  */

      translate_base_fetch (pool, indent, byte_size, signed_p, input, "tmp");
      (*input)->type = loc_fragment;
      (*input)->address.declare = "tmp";

      Dwarf_Word bit_offset, bit_size;
      get_bitfield (*input, die, &bit_offset, &bit_size);

      obstack_printf (pool, "%*s"
		      "fetch_bitfield (%s, tmp, %" PRIu64 ", %" PRIu64 ");\n",
		      indent *2, "", target, bit_offset, bit_size);

      struct location *loc = new_synthetic_loc (pool, *input, false);
      loc->type = loc_final;
      (*input)->next = loc;
      *input = loc;
    }
  else
    translate_base_fetch (pool, indent, byte_size, signed_p, input, target);
}

/* Translate a fragment to store RVALUE into the base-type value of
   BYTE_SIZE bytes at the *INPUT location.  */
static void
translate_base_store (struct obstack *pool, int indent, Dwarf_Word byte_size,
		      struct location **input, struct location *store_loc,
		      const char *rvalue)
{
  bool deref = false;

  if (store_loc->type == loc_noncontiguous)
    {
      declare_noncontig_union (pool, indent, input, store_loc);

      obstack_printf (pool, "%*su.whole = %s;\n", indent * 2, "", rvalue);
      struct location *loc = new_synthetic_loc (pool, *input, deref);
      loc->type = loc_fragment;
      (*input)->next = loc;
      *input = loc;

      Dwarf_Word offset = 0;
      char piece[sizeof "u.pieces.p" + 20] = "u.pieces.p";
      struct location *p;
      for (p = store_loc->pieces; p != NULL; p = p->next)
        {
	  struct location *newp = obstack_alloc (pool, sizeof *newp);
	  *newp = *p;
	  newp->next = NULL;
	  (*input)->next = newp;
	  *input = newp;

	  snprintf (&piece[sizeof "u.pieces.p" - 1], 20, "%" PRIu64, offset);
	  translate_base_store (pool, indent,
				p->byte_size, input, *input, piece);
	  (*input)->type = loc_fragment;

	  offset += p->byte_size;
	}

      (*input)->type = loc_final;
    }
  else
    {
      switch (byte_size)
	{
	case 1:
	case 2:
	case 4:
	case 8:
	  obstack_printf (pool, "%*s", indent * 2, "");
	  deref = emit_base_store (pool, byte_size, rvalue, store_loc);
	  obstack_printf (pool, "\n");
	  break;

	default:
	  /* Could handle this generating call to memcpy equivalent.  */
	  FAIL (*input, N_("store is larger than base integer types"));
	  break;
	}

      struct location *loc = new_synthetic_loc (pool, *input, deref);
      loc->type = loc_final;
      (*input)->next = loc;
      *input = loc;
    }
}

/* Translate a fragment to fetch the value of variable or member DIE
   at the *INPUT location and store it in rvalue RVALUE.  */

void
c_translate_store (struct obstack *pool, int indent,
		   Dwarf_Addr dwbias __attribute__ ((unused)),
		   Dwarf_Die *die, Dwarf_Die *typedie,
		   struct location **input, const char *rvalue)
{
  ++indent;

  Dwarf_Attribute size_attr;
  Dwarf_Word byte_size;
  if (dwarf_attr_integrate (die, DW_AT_byte_size, &size_attr) == NULL
      || dwarf_formudata (&size_attr, &byte_size) != 0)
    byte_size = base_byte_size (typedie, *input);

  Dwarf_Attribute encoding_attr;
  Dwarf_Word encoding;
  if (dwarf_attr_integrate (die, DW_AT_encoding, &encoding_attr) == NULL
      || dwarf_formudata (&encoding_attr, &encoding) != 0)
    encoding = base_encoding (typedie, *input);
  bool signed_p = (encoding == DW_ATE_signed 
                   || encoding == DW_ATE_signed_char);

  *input = discontiguify (pool, indent, *input, byte_size,
			  max_fetch_size (*input, die));

  struct location *store_loc = *input;

  if (dwarf_hasattr_integrate (die, DW_AT_bit_offset))
    {
      /* This is a bit field.  Fetch the containing base type into a
	 temporary variable.  */

      translate_base_fetch (pool, indent, byte_size, signed_p, input, "tmp");
      (*input)->type = loc_fragment;
      (*input)->address.declare = "tmp";

      Dwarf_Word bit_offset, bit_size;
      get_bitfield (*input, die, &bit_offset, &bit_size);

      obstack_printf (pool, "%*s"
		      "store_bitfield (tmp, %s, %" PRIu64 ", %" PRIu64 ");\n",
		      indent * 2, "", rvalue, bit_offset, bit_size);

      struct location *loc = new_synthetic_loc (pool, *input, false);
      loc->type = loc_fragment;
      (*input)->next = loc;
      *input = loc;

      /* We have mixed RVALUE into the bits in "tmp".
	 Now we'll store "tmp" back whence we fetched it.  */
      rvalue = "tmp";
    }

  translate_base_store (pool, indent, byte_size, input, store_loc, rvalue);
}

/* Translate a fragment to dereference the given pointer type,
   where *INPUT is the location of the pointer with that type.

   We chain on a loc_address program that yields this pointer value
   (i.e. the location of what it points to).  */

void
c_translate_pointer (struct obstack *pool, int indent,
		     Dwarf_Addr dwbias __attribute__ ((unused)),
		     Dwarf_Die *typedie, struct location **input)
{
  assert (dwarf_tag (typedie) == DW_TAG_pointer_type);

  Dwarf_Attribute attr_mem;
  Dwarf_Word byte_size;
  if (dwarf_attr_integrate (typedie, DW_AT_byte_size, &attr_mem) == NULL)
    byte_size = 0;
  else if (dwarf_formudata (&attr_mem, &byte_size) != 0)
    FAIL (*input,
	  N_("cannot get byte_size attribute for type %s: %s"),
	  dwarf_diename (typedie) ?: "<anonymous>",
	  dwarf_errmsg (-1));

  Dwarf_Attribute encoding_attr;
  Dwarf_Word encoding;
  if (dwarf_attr_integrate (typedie, DW_AT_encoding, &encoding_attr) == NULL
      || dwarf_formudata (&encoding_attr, &encoding) != 0)
    encoding = base_encoding (typedie, *input);
  bool signed_p = (encoding == DW_ATE_signed 
                   || encoding == DW_ATE_signed_char);

  translate_base_fetch (pool, indent + 1, byte_size, signed_p, input, "addr");
  (*input)->type = loc_address;
}


void
c_translate_addressof (struct obstack *pool, int indent,
		       Dwarf_Addr dwbias __attribute__ ((unused)),
		       Dwarf_Die *die,
		       Dwarf_Die *typedie __attribute__ ((unused)),
		       struct location **input, const char *target)
{
  ++indent;

  if (dwarf_hasattr_integrate (die, DW_AT_bit_offset))
    FAIL (*input, N_("cannot take the address of a bit field"));

  switch ((*input)->type)
    {
    case loc_address:
      obstack_printf (pool, "%*s%s = addr;\n", indent * 2, "", target);
      (*input)->next = new_synthetic_loc (pool, *input, false);
      (*input)->next->type = loc_final;
      break;

    case loc_register:
      FAIL (*input, N_("cannot take address of object in register"));
      break;
    case loc_noncontiguous:
      FAIL (*input, N_("cannot take address of noncontiguous object"));
      break;

    default:
      abort();
      break;
    }
}


/* Determine the element stride of an array type.  */
static Dwarf_Word
array_stride (Dwarf_Die *typedie, struct location *origin)
{
  Dwarf_Attribute attr_mem;
  if (dwarf_attr_integrate (typedie, DW_AT_byte_stride, &attr_mem) != NULL)
    {
      Dwarf_Word stride;
      if (dwarf_formudata (&attr_mem, &stride) == 0)
	return stride;
      FAIL (origin, N_("cannot get byte_stride attribute array type %s: %s"),
	    dwarf_diename (typedie) ?: "<anonymous>",
	    dwarf_errmsg (-1));
    }

  Dwarf_Die die_mem;
  if (dwarf_attr_integrate (typedie, DW_AT_type, &attr_mem) == NULL
      || dwarf_formref_die (&attr_mem, &die_mem) == NULL)
    FAIL (origin, N_("cannot get element type of array type %s: %s"),
	  dwarf_diename (typedie) ?: "<anonymous>",
	  dwarf_errmsg (-1));

  if (dwarf_attr_integrate (&die_mem, DW_AT_byte_size, &attr_mem) != NULL)
    {
      Dwarf_Word stride;
      if (dwarf_formudata (&attr_mem, &stride) == 0)
	return stride;
      FAIL (origin,
	    N_("cannot get byte_size attribute for array element type %s: %s"),
	    dwarf_diename (&die_mem) ?: "<anonymous>",
	    dwarf_errmsg (-1));
    }

  FAIL (origin, N_("confused about array element size"));
  return 0;
}

void
c_translate_array (struct obstack *pool, int indent,
		   Dwarf_Addr dwbias __attribute__ ((unused)),
		   Dwarf_Die *typedie, struct location **input,
		   const char *idx, Dwarf_Word const_idx)
{
  assert (dwarf_tag (typedie) == DW_TAG_array_type);

  ++indent;

  Dwarf_Word stride = array_stride (typedie, *input);

  struct location *loc = *input;
  while (loc->type == loc_noncontiguous)
    {
      if (idx != NULL)
	FAIL (*input, N_("cannot dynamically index noncontiguous array"));
      else
	{
	  Dwarf_Word offset = const_idx * stride;
	  struct location *piece = loc->pieces;
	  while (piece != NULL && offset >= piece->byte_size)
	    {
	      offset -= piece->byte_size;
	      piece = piece->next;
	    }
	  if (piece == NULL)
	    FAIL (*input, N_("constant index is outside noncontiguous array"));
	  if (offset % stride != 0)
	    FAIL (*input, N_("noncontiguous array splits elements"));
	  const_idx = offset / stride;
	  loc = piece;
	}
    }

  switch (loc->type)
    {
    case loc_address:
      ++indent;
      if (idx != NULL)
	obstack_printf (pool, "%*saddr += %s * " UFORMAT ";\n",
			indent * 2, "", idx, stride);
      else
	obstack_printf (pool, "%*saddr += " UFORMAT " * " UFORMAT ";\n",
			indent * 2, "", const_idx, stride);
      loc = new_synthetic_loc (pool, loc, false);
      break;

    case loc_register:
      FAIL (*input, N_("cannot index array stored in a register"));
      break;

    default:
      abort();
      break;
    }

  (*input)->next = loc;
  *input = (*input)->next;
}


/* Emitting C code for finalized fragments.  */


#define emit(fmt, ...) fprintf (out, fmt, ## __VA_ARGS__)

/* Open a block with a comment giving the original DWARF expression.  */
static void
emit_header (FILE *out, struct location *loc, unsigned int hindent)
{
  if (loc->ops == NULL)
    emit ("%*s{ // synthesized\n", hindent * 2, "");
  else
    {
      emit ("%*s{ // DWARF expression:", hindent * 2, "");
      size_t i;
      for (i = 0; i < loc->nops; ++i)
	{
	  emit (" %#x", loc->ops[i].atom);
	  if (loc->ops[i].number2 == 0)
	    {
	      if (loc->ops[i].number != 0)
		emit ("(%" PRId64 ")", loc->ops[i].number);
	    }
	  else
	    emit ("(%" PRId64 ",%" PRId64 ")",
		  loc->ops[i].number, loc->ops[i].number2);
	}
      emit ("\n");
    }
}

/* Emit a code fragment to assign the target variable to a register value.  */
static void
emit_loc_register (FILE *out, struct location *loc, unsigned int indent,
		   const char *target)
{
  assert (loc->type == loc_register);

  emit ("%*s%s = fetch_register (%u);\n",
	indent * 2, "", target, loc->regno);
}

/* Emit a code fragment to assign the target variable to an address.  */
static void
emit_loc_address (FILE *out, struct location *loc, unsigned int indent,
		  const char *target)
{
  assert (loc->type == loc_address);

  if (loc->address.stack_depth == 0)
    /* Synthetic program.  */
    emit ("%s", loc->address.program);
  else
    {
      emit ("%*s{\n", indent * 2, "");
      emit ("%*s%s " STACKFMT, (indent + 1) * 2, "", STACK_TYPE, 0);
      unsigned i;
      for (i = 1; i < loc->address.stack_depth; ++i)
	emit (", " STACKFMT, i);
      emit (";\n");

      emit ("%s%*s%s = " STACKFMT ";\n", loc->address.program,
	    (indent + 1) * 2, "", target, 0);
      emit ("%*s}\n", indent * 2, "");
    }
}

/* Emit a code fragment to declare the target variable and
   assign it to an address-sized value.  */
static void
emit_loc_value (FILE *out, struct location *loc, unsigned int indent,
		const char *target, bool declare)
{
  if (declare)
    emit ("%*s%s %s;\n", indent * 2, "", STACK_TYPE, target);

  emit_header (out, loc, indent++);

  switch (loc->type)
    {
    default:
      abort ();
      break;

    case loc_register:
      emit_loc_register (out, loc, indent, target);
      break;

    case loc_address:
      emit_loc_address (out, loc, indent, target);
      break;
    }

  emit ("%*s}\n", --indent * 2, "");
}

bool
c_emit_location (FILE *out, struct location *loc, int indent)
{
  emit ("%*s{\n", indent * 2, "");

  bool declared_addr = false;
  struct location *l;
  for (l = loc; l != NULL; l = l->next)
    switch (l->type)
      {
      case loc_decl:
	emit ("%s", l->address.program);
	break;

      case loc_address:
	if (declared_addr)
	  break;
	declared_addr = true;
	l->address.declare = "addr";
      case loc_fragment:
      case loc_final:
	if (l->address.declare != NULL)
	  {
	    if (l->byte_size == 0 || l->byte_size == (Dwarf_Word) -1)
	      emit ("%*s%s %s;\n", (indent + 1) * 2, "",
		    STACK_TYPE, l->address.declare);
	    else
	      emit ("%*suint%" PRIu64 "_t %s;\n", (indent + 1) * 2, "",
		    l->byte_size * 8, l->address.declare);
	  }

      default:
	break;
      }

  bool deref = false;

  if (loc->frame_base != NULL)
    emit_loc_value (out, loc->frame_base, indent, "frame_base", true);

  for (; loc->next != NULL; loc = loc->next)
    switch (loc->type)
      {
      case loc_address:
	/* Emit the program fragment to calculate the address.  */
	emit_loc_value (out, loc, indent + 1, "addr", false);
	deref = deref || loc->address.used_deref;
	break;

      case loc_fragment:
	emit ("%s", loc->address.program);
	deref = deref || loc->address.used_deref;
	break;

      case loc_decl:
      case loc_register:
      case loc_noncontiguous:
	/* These don't produce any code directly.
	   The next address/final record incorporates the value.  */
	break;

      case loc_final:		/* Should be last in chain!  */
      default:
	abort ();
	break;
      }

  if (loc->type != loc_final)	/* Unfinished chain.  */
    abort ();

  emit ("%s%*s}\n", loc->address.program, indent * 2, "");

  return deref || loc->address.used_deref;
}

#undef emit
