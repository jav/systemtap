#define SDT_USE_VARIADIC
#include "sys/sdt.h"

int main()
{
    STAP_PROBEV(test, mark_z);
    STAP_PROBEV(test, mark_a, 1);
    STAP_PROBEV(test, mark_b, 1, 2);
    STAP_PROBEV(test, mark_c, 1, 2, 3);
    STAP_PROBEV(test, mark_d, 1, 2, 3, 4);
    STAP_PROBEV(test, mark_e, 1, 2, 3, 4, 5);
    STAP_PROBEV(test, mark_f, 1, 2, 3, 4, 5, 6);
    STAP_PROBEV(test, mark_g, 1, 2, 3, 4, 5, 6, 7);
    STAP_PROBEV(test, mark_h, 1, 2, 3, 4, 5, 6, 7, 8);
    STAP_PROBEV(test, mark_i, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    STAP_PROBEV(test, mark_j, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    STAP_PROBEV(test, mark_k, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    STAP_PROBEV(test, mark_l, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);

    return 0;
}
