#include <nspr.h>
#include <pk11func.h>
#include <libintl.h>
#include <locale.h>
#include "config.h"

#if ENABLE_NLS
#define _(string) gettext(string)
#define _N(string, string_plural, count) \
        ngettext((string), (string_plural), (count))
#else
#define _(string) (string)
#define _N(string, string_plural, count) \
        ( (count) == 1 ? (string) : (string_plural) )
#endif
#define _F(format, ...) autosprintf(_(format), __VA_ARGS__)
#define _NF(format, format_plural, count, ...) \
        autosprintf(_N((format), (format_plural), (count)), __VA_ARGS__)

void nssError (void);
void nssCleanup (void);

char *
nssPasswordCallback (PK11SlotInfo *info __attribute ((unused)),
		     PRBool retry __attribute ((unused)),
		     void *arg __attribute ((unused)));

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
