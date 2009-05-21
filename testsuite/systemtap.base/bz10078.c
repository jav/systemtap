#include <stdlib.h>
#include <stdio.h>

struct point { int x, y; };

struct point mkpoint2(void)
{
	struct point p = { 1, 2 };
	return p;
}

struct point mkpoint1(void)
{
	return mkpoint2();
}

main()
{
	struct point p = mkpoint1();
	printf("%d,%d\n", p.x, p.y);
	exit(0);
}
