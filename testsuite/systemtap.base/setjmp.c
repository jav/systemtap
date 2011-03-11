#include <setjmp.h>

#ifdef SIGJMP
# define SETJMP(env)		sigsetjmp (env, 1)
# define LONGJMP(env, x)	siglongjmp (env, x)
#elif defined UNDERJMP
# define SETJMP(env)		_setjmp (env)
# define LONGJMP(env, x)	_longjmp (env, x)
#else
# define SETJMP(env)		setjmp (env)
# define LONGJMP(env, x)	longjmp (env, x)
#endif

struct env
{
  jmp_buf jmp;
};

struct env saved;

void __attribute__ ((noinline))
jumper (struct env *env, int x)
{
  if (x == 0)
    siglongjmp (env->jmp, 1);
}

void __attribute__ ((noinline))
jumped (void)
{
  asm volatile ("");
}

void __attribute__ ((noinline))
setter (void)
{
  if (sigsetjmp (saved.jmp, 1) == 0)
    jumper (&saved, 0);
  else
    jumped ();
}

int
main (void)
{
  setter ();
  return 0;
}
