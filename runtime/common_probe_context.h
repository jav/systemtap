/* Included once by translate.cxx c_unparser::emit_common_header ()
   Defines all common fields and probe flags for struct context.
   Available to C-based probe handlers as fields of the CONTEXT ptr.  */

/* Used to indicate whether a probe context is in use.
   Tested in the code entering the probe setup by common_probe_entry_prologue
   and cleared by the common_probe_entry_epilogue code. When an early error
   forces a goto probe_epilogue then needs an explicitly atomic_dec() first.
   All context busy flags are tested on module unload to prevent unloading
   while some probe is still running.  */
atomic_t busy;

/* The fully-resolved probe point associated with a currently running probe
   handler, including alias and wild-card expansion effects.
   aka stap_probe->pp.  Setup by common_probe_entryfn_prologue.
   Used in warning/error messages and accessible by pp() tapset function.  */
const char *probe_point;

/* The script-level probe point associated with a currently running probe
   handler, including  wild-card expansion effects as per 'stap -l'.
   Guarded by STP_NEED_PROBE_NAME as setup in pn() tapset function.  */
#ifdef STP_NEED_PROBE_NAME
const char *probe_name;
#endif

int actionremaining;
int nesting;
string_t error_buffer;

/* Only used when stap script uses tokenize.stp tapset.  */
#ifdef STAP_NEED_CONTEXT_TOKENIZE
string_t tok_str;
char *tok_start;
char *tok_end;
#endif

/* NB: last_error is used as a health flag within a probe.
   While it's 0, execution continues
   When it's "something", probe code unwinds, _stp_error's, sets error state */
const char *last_error;
const char *last_stmt;

/* status of pt_regs regs field.  _STP_REGS_ flags.  */
int regflags;
struct pt_regs *regs;

/* unwaddr is caching unwound address in each probe handler on ia64. */
#if defined __ia64__
unsigned long *unwaddr;
#endif

/* non-NULL when this probe was a kretprobe. */
struct kretprobe_instance *pi;
int pi_longs; /* int64_t count in pi->data, the rest is string_t */

/* Only used when stap script uses the i386 or x86_64 register.stp tapset. */
#ifdef STAP_NEED_REGPARM
int regparm;
#endif

/* State for mark_derived_probes.  */
va_list *mark_va_list;
const char * marker_name;
const char * marker_format;
void *data;

/* Only used for overload processing. */
#ifdef STP_OVERLOAD
cycles_t cycles_base;
cycles_t cycles_sum;
#endif

/* non-NULL when this probe was a uretprobe. */
struct uretprobe_instance *ri;

/* Current state of the unwinder (as used in the unwind.c dwarf unwinder). */
#if defined(STP_NEED_UNWIND_DATA)
struct unwind_context uwcontext;
#endif
