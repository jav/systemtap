#include "sys/sdt.h"

#include <string>

size_t
length(const std::string& str)
{
    int res, r;
    STAP_PROBE1(cast-scope, length, &str);
    r = str.length() * 2;
    STAP_PROBE(cast-scope, dummy); /* Just here to probe line +4. */
    res = r / 2;
    STAP_PROBE(cast-scope, dummy2); /* Just here prevent line reordering. */
    return res;
}

int
main()
{
    std::string hello = "Hello World!";
    return 12 != length(hello);
}
