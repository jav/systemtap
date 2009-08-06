int func_a(int a)
{
  a = a + 1;
  return a;
}


int func_b(int b)
{
  b = b + 2;
  return b;
}

int main()
{
  int a;

  a = func_a(1);
  a = a + func_b(a);
  return 0;
}
