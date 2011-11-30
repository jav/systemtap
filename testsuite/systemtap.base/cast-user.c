#include <sys/socket.h>
int main()
{
    struct sockaddr sa = {
        .sa_family = (sa_family_t) -1,
        .sa_data = 42,
    };
    bind(-1, &sa, sizeof(sa));
    return 0;
}

