static int
func2 (int x, int y)
{
  return x + y;
}

static int
func (int arg)
{
  int x = 16;
  int y = arg - x;
  int z = func2(x, y);
  return x + y + z;
}

int
main (int argc, char *argv[], char *envp[])
{
// Note: Passing to srand to force evaluation of func().
 srand(func(42));
  return 0;
}
