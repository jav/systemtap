struct foo {
    int x, y;
};

struct foo*
get_foo()
{
    static struct foo f = { 6, 7 };
    return &f;
}
