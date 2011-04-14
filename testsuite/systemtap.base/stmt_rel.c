#include <stdio.h>

#define    stackrange  3
#define maxcells 	 18
#define false 0
#define true 1

int stack[stackrange + 1];
int freelist, movesdone;
struct element
{
  int discsize;
  int next;
};

struct element cellspace[maxcells + 1];

static inline void
Error (char *emsg)
{
  printf (" Error in Towers: %s\n", emsg);
}

static inline void  
Makenull (s)
{
  stack[s] = 0;
}

static inline int  
Getelement ()
{
  int temp = 0;
  if (freelist > 0)
    {
      temp = freelist;
      freelist = cellspace[freelist].next;
    }
  else
    Error ("out of space   ");
  return (temp);
}

static inline void
Push (int i, int s)
{
  int errorfound, localel;
  errorfound = false;
  if (stack[s] > 0)
    if (cellspace[stack[s]].discsize <= i)
      {
	errorfound = true;
	Error ("disc size error");
      };
  if (!errorfound)
    {
      localel = Getelement ();
      cellspace[localel].next = stack[s];
      stack[s] = localel;
      cellspace[localel].discsize = i;
    }
}

static inline void
Init (int s, int n)
{
  int discctr;
  Makenull (s);
  for (discctr = n; discctr >= 1; discctr--)
    Push (discctr, s);
}

static inline int
Pop (int s)
{
  int temp, temp1;
  if (stack[s] > 0)
    {
      temp1 = cellspace[stack[s]].discsize;
      temp = cellspace[stack[s]].next;
      cellspace[stack[s]].next = freelist;
      freelist = stack[s];
      stack[s] = temp;
      return (temp1);
    }
  else
    Error ("nothing to pop ");
  return -1;
}

static inline void
Move (int s1, int s2)
{
  Push (Pop (s1), s2);
  movesdone = movesdone + 1;
}

static void
tower (int i, int j, int k)
{
  int other;
  if (k == 1)
    Move (i, j);
  else
    {
      other = 6 - i - j;
      tower (i, other, k - 1);
      Move (i, j);
      tower (other, j, k - 1);
    }
}

int
main ()
{
  int i;
  for (i = 1; i <= maxcells; i++)
    cellspace[i].next = i - 1;
  freelist = maxcells;
  Init (1, 14);
  Makenull (2);
  Makenull (3);
  movesdone = 0;
  tower (1, 2, 14);
  if (movesdone != 16383)
    printf (" Error in Towers.\n");
  return 0;
}
