void setspeed(int v);
int getspeed();
void setdistance(int v);
int getdistance();

int value = 42;

int compare_value (int v)
{
  asm("");
  return v - value;
}

int calculate_value ()
{
  asm("");
  return getdistance () / getspeed ();
}

int main (int argc, char **argv)
{
  int value;

  setspeed (6);
  setdistance (60);

  value = calculate_value ();

  return compare_value (value) == 0 ? -1 : 0;
}
