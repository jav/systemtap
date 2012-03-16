struct s1
{
  char c;
  short s;
  int i;
  long l;
  float f;
  double d;
};

s1 S1;

int func (s1 *p)
{
  return p->i;
}

int main()
{
  return func (&S1);
}
