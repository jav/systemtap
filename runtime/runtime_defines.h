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
