#include "sys/sdt.h"

#include <string>

size_t
length(const std::string& str)
{
    STAP_PROBE1(cast-scope, length, &str);
    return str.length();
}

int
main()
{
    std::string hello = "Hello World!";
    return 12 != length(hello);
}
