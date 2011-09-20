/* Common runtime defines, not dependend on session variables.
   Included once at the top of the generated stap.c file by the translate.cxx
   translate_pass ().  */

/* Strings are used for storing backtraces, they are larger on 64bit
   so raise the size on 64bit architectures. PR10486.  */
#include <asm/types.h>
#ifndef MAXSTRINGLEN
#if BITS_PER_LONG == 32
#define MAXSTRINGLEN 256
#else
#define MAXSTRINGLEN 512
#endif
#endif
typedef char string_t[MAXSTRINGLEN];

#ifndef MAXACTION
#define MAXACTION 1000
#endif
#ifndef MAXACTION_INTERRUPTIBLE
#define MAXACTION_INTERRUPTIBLE (MAXACTION * 10)
#endif
#ifndef TRYLOCKDELAY
#define TRYLOCKDELAY 10 /* microseconds */
#endif
#ifndef MAXTRYLOCK
#define MAXTRYLOCK 100 /* 1 millisecond total */
#endif
#ifndef MAXMAPENTRIES
#define MAXMAPENTRIES 2048
#endif
#ifndef MAXERRORS
#define MAXERRORS 0
#endif
#ifndef MAXSKIPPED
#define MAXSKIPPED 100
#endif
#ifndef MINSTACKSPACE
#define MINSTACKSPACE 1024
#endif
#ifndef INTERRUPTIBLE
#define INTERRUPTIBLE 1
#endif

/* Overload processing.  */
#ifndef STP_OVERLOAD_INTERVAL
#define STP_OVERLOAD_INTERVAL 1000000000LL
#endif
#ifndef STP_OVERLOAD_THRESHOLD
#define STP_OVERLOAD_THRESHOLD 500000000LL
#endif

/* We allow the user to completely turn overload processing off
   (as opposed to tuning it by overriding the values above) by
   running:  stap -DSTP_NO_OVERLOAD {other options}.  */
#if !defined(STP_NO_OVERLOAD) && !defined(STAP_NO_OVERLOAD)
#define STP_OVERLOAD
#endif

/* Defines for CONTEXT probe_type. */
/* begin, end or never probe, triggered by stap module itself. */
#define _STP_PROBE_HANDLER_BEEN            1
/* user space instruction probe, trigger by utrace signal report. */
#define _STP_PROBE_HANDLER_ITRACE          2
/* kernel marker probe, triggered by old marker_probe (removed in 2.6.32). */
#define _STP_PROBE_HANDLER_MARKER          3
/* perf event probe, triggered by perf event counter.
   Note that although this is defined in tapset-perfmon.cxx, this has
   nothing to do with the (old and now removed) perfmon probes. */
#define _STP_PROBE_HANDLER_PERF            4
/* read or write of stap module proc file. Triggers on manipulation of
   the /proc/systemtap/MODNAME created through a procfs probe. */
#define _STP_PROBE_HANDLER_PROCFS          5
/* timer probe, triggered by standard kernel init_timer interface. */
#define _STP_PROBE_HANDLER_TIMER           6
/* high resolution timer probes, triggered by hrtimer firing. */
#define _STP_PROBE_HANDLER_HRTIMER         7
/* profile timer probe, triggered by kernel profile timer (either in
   kernel or user space). */
#define _STP_PROBE_HANDLER_PROFILE_TIMER   8
/* utrace thread start/end probe, triggered by utrace quiesce event for
   associated thread. */
#define _STP_PROBE_HANDLER_UTRACE          9
/* utrace syscall enter/exit probe, triggered by utrace syscall event. */
#define _STP_PROBE_HANDLER_UTRACE_SYSCALL 10
/* kprobe event, triggered for dwarf or dwarfless kprobes. */
#define _STP_PROBE_HANDLER_KPROBE         11
/* kretprobe event, triggered for dwarf or dwarfless kretprobes. */
#define _STP_PROBE_HANDLER_KRETPROBE      12
/* uprobe event, triggered by hitting a uprobe. */
#define _STP_PROBE_HANDLER_UPROBE         13
/* uretprobe event, triggered by hitting a uretprobe. */
#define _STP_PROBE_HANDLER_URETPROBE      14
/* hardware data watch break point, triggered by kernel data read/write. */
#define _STP_PROBE_HANDLER_HWBKPT         15
/* kernel tracepoint probe, triggered by tracepoint event call. */
#define _STP_PROBE_HANDLER_TRACEPOINT     16

/* Defines for CONTEXT probe_flags. */
/* Probe occured in user space, also indicate regs fully from user. */
#define _STP_PROBE_STATE_USER_MODE  1
/* _stp_get_uregs() was called and full user registers were recovered. */
#define _STP_PROBE_STATE_FULL_UREGS 2
