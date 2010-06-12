int main() { return 0; }
extern int (*func_alias) __attribute__ ((alias ("main")));
