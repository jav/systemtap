struct point {
    int x, y;
    point(int x, int y): x(x), y(y) {}
};
struct A {
    point foo, bar;
    A(int i): foo(i, i + 1), bar(i + 2, i + 3) {}
};
struct B : A {
    int foo;
    B(): A(1), foo(42) {}
};

static B b;
int main() {
    int sum = b.A::foo.x + b.A::foo.y + b.bar.x + b.bar.y + b.foo;
    return sum != 10 + 42;
}
