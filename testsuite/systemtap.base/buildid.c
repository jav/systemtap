#ifndef ONLY_MAIN
foo ()
{
  int x = 1;
 label: 
  x = 2;
}
#endif
#ifndef NO_MAIN
main ()
{
  foo();
}
#endif
