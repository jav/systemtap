#include <sys/sdt.h>

// PR12121
//
// This kind of "repeated" typedef used to cause bad things for us.  When
// resolving $epoch->foo below, we start at the typedef-tm and dereference to
// the struct-tm.  That's just a declaration, so we use declaration_resolve to
// look for the struct definition.  But since we were looking by the name "tm"
// only, we would find the typedef again, getting us nowhere.
//
// The fixed code does declaration_resolve on "struct tm" in the global cache,
// so in this case we won't find anything locally, and thus we continue looking
// in other CUs.  The test harness compiles this with time.h in another CU to
// provide the actual definition for "struct tm".
typedef struct tm tm;

typedef long time_t;

extern struct tm *localtime(const time_t *timep);

int main()
{
    tm* epoch = localtime(0);
    STAP_PROBE1(test, epoch, (void*)epoch);
    return 0;
}
