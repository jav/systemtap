#include <stdio.h>

struct foo;
struct foo* get_foo(void);

void
print(struct foo* f)
{
    printf("%p\n", f);
}

int
main()
{
    print(get_foo());
    return 0;
}
