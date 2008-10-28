#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

void funcd(int i, jmp_buf env)
{
	printf("In %s: %s :%d : i=%d.  Calling longjmp\n", "bz5274.c",__func__,__LINE__,i);
	longjmp(env, i);
}

void funcc(int i, jmp_buf env)
{
	printf("In %s: %s :%d : i=%d.  Calling funcd\n", "bz5274.c",__func__,__LINE__,i);
	funcd(i,env);
}


void funcb(int i, jmp_buf env)
{
	printf("In %s: %s :%d : i=%d.  Calling funcc\n", "bz5274.c",__func__,__LINE__,i);
	funcc(i,env);
}


void funca(char *s, jmp_buf env)
{
	int i;

	i = setjmp(env);
	if (i == 4)
		return;
	funcb(++i, env);
	return;
}



int main(int argc, char **argv)
{
	jmp_buf env;

	funca("Hello World", env);
	exit(0);
}
