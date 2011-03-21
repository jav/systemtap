#include <nspr.h>
#include <pk11func.h>

void nssError (void);
void nssCleanup (void);

char *
nssPasswordCallback (PK11SlotInfo *info __attribute ((unused)),
		     PRBool retry __attribute ((unused)),
		     void *arg __attribute ((unused)));

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
