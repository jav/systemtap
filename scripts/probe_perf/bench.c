#   include <sys/types.h>
#   include <sys/time.h>
#   include <sys/times.h>
#   include <memory.h>
#   include <stdio.h>
#   include <stdlib.h>
#   include "bench_.h"

#define  nil		 0
#define	 false		 0
#define  true		 1
#define  bubblebase 	 1.61
#define  dnfbase 	 3.5
#define  permbase 	 1.75
#define  queensbase 	 1.83
#define  towersbase 	 2.39
#define  quickbase 	 1.92
#define  intmmbase 	 1.46
#define  treebase 	  2.5
#define  mmbase 	 0.0
#define  fpmmbase 	 2.92
#define  puzzlebase 	 0.5
#define  fftbase 	 0.0
#define  fpfftbase 	 4.44

/* Towers */
#define maxcells 	 18

/* Intmm, Mm */
#define rowsize 	 40

/* Puzzle */
#define SIZE	 	 511
#define classmax 	 3
#define typemax 	 12
#define d 		 8

/* Bubble, Quick */
#define sortelements 	 5000
#define srtelements 	 500

/* fft */
#define fftsize 	 256
#define fftsize2 	 129

/* Perm */
#define    permrange 10

/* tree */
struct node
{
  struct node *left, *right;
  int val;
};

/* Towers */
#define    stackrange	3

struct element
{
  int discsize;
  int next;
};

/* FFT */
struct complex
{
  float rp, ip;
};

float value;
float fixed, floated;

/* global */
int seed;

#define CYCLES_HBOUND 1000
long cycles_start;
long permute_cycles_idx;
long permute_cycles_avg;
long (*permute_cycles) [CYCLES_HBOUND];
long tower_cycles_idx;
long tower_cycles_avg;
long (*tower_cycles) [CYCLES_HBOUND];
long try_cycles_idx;
long try_cycles_avg;
long (*try_cycles) [CYCLES_HBOUND];
long innerproduct_cycles_idx;
long innerproduct_cycles_avg;
long (*innerproduct_cycles) [CYCLES_HBOUND];
long trial_cycles_idx;
long trial_cycles_avg;
long (*trial_cycles) [CYCLES_HBOUND];
long quicksort_cycles_idx;
long quicksort_cycles_avg;
long (*quicksort_cycles) [CYCLES_HBOUND];
long insert_cycles_idx;
long insert_cycles_avg;
long (*insert_cycles) [CYCLES_HBOUND];
long bubble_cycles_idx;
long bubble_cycles_avg;
long (*bubble_cycles) [CYCLES_HBOUND];
long fft_cycles_idx;
long fft_cycles_avg;
long (*fft_cycles) [CYCLES_HBOUND];

/* Perm */
int permarray[permrange + 1];
int pctr;

/* tree */
struct node *tree;

/* Towers */
int stack[stackrange + 1];
struct element cellspace[maxcells + 1];
int freelist, movesdone;

/* Intmm, Mm */
int ima[rowsize + 1][rowsize + 1], imb[rowsize + 1][rowsize + 1],
  imr[rowsize + 1][rowsize + 1];
float rma[rowsize + 1][rowsize + 1], rmb[rowsize + 1][rowsize + 1],
  rmr[rowsize + 1][rowsize + 1];

/* Puzzle */
int piececount[classmax + 1],
  class[typemax + 1],
  piecemax[typemax + 1], puzzl[SIZE + 1], p[typemax + 1][SIZE + 1], n, kount;

/* Bubble, Quick */
int sortlist[sortelements + 1], biggest, littlest, top;

/* FFT */
struct complex z[fftsize + 1], w[fftsize + 1], e[fftsize2 + 1];
float zr, zi;

/* global procedures */

long rdtsc(void) __attribute__ ((__noinline__));
long rdtsc(void) 
{
  // XXX this only works on x86_64
  // -- not i386 because the long result is only 32-bit
  // -- not other archs because they need other asm
#if defined __x86_64__
  int res[2];

  __asm__ __volatile__  ("xorl %%eax,%%eax \n push %%rbx \n cpuid \n"
			 ::: "%rax", "%rcx", "%rdx");
  // read TSC, store edx:eax in res
  __asm__ __volatile__  ("rdtsc\n"
			 : "=a" (res[0]), "=d" (res[1]) );
  __asm__ __volatile__  ("xorl %%eax,%%eax \n cpuid \n pop %%rbx \n"
			 ::: "%rax", "%rcx", "%rdx");

  return *(long*)res;
#else
  return 0;
#endif
}

Initrand ()
{
  seed = 74755;
}

int
Rand ()
{
  seed = (seed * 1309 + 13849) & 65535;
  return (seed);
}

/* Permutation program, heavily recursive, written by Denny Brown. */

Swap (a, b)
     int *a, *b;
{
  int t;
  t = *a;
  *a = *b;
  *b = t;
}

Initialize ()
{
  int i;
  for (i = 1; i <= 7; i++)
    {
      permarray[i] = i - 1;
    };
}

Permute (n)
     int n;
{
  int k;
  pctr = pctr + 1;
  if (n != 1)
    {
      Permute (n - 1);
      for (k = n - 1; k >= 1; k--)
	{
	  cycles_start = rdtsc();
#ifndef NO_STAP_SDT
	  BENCH_PERMUTE (cycles_start,k);
#else
BENCH_PERMUTE:
#endif
	  if (__builtin_expect(permute_cycles_idx != CYCLES_HBOUND,0))
	    {
	      permute_cycles_idx++;
	      (*permute_cycles)[permute_cycles_idx] = rdtsc() - cycles_start;
	    }
	  Swap (&permarray[n], &permarray[k]);
	  Permute (n - 1);
	  Swap (&permarray[n], &permarray[k]);
	};
    };
}

Perm ()
{
  int i;
  pctr = 0;
  for (i = 1; i <= 5; i++)
    {
      Initialize ();
      Permute (7);
    };
  if (pctr != 43300)
    printf (" Error in Perm.\n");
}


/*  Program to Solve the Towers of Hanoi */

Error (emsg)
     char *emsg;
{
  printf (" Error in Towers: %s\n", emsg);
}

Makenull (s)
{
  stack[s] = 0;
}

int
Getelement ()
{
  int temp;
  if (freelist > 0)
    {
      temp = freelist;
      freelist = cellspace[freelist].next;
    }
  else
    Error ("out of space   ");
  return (temp);
}

Push (i, s)
     int i, s;
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

Init (s, n)
     int s, n;
{
  int discctr;
  Makenull (s);
  for (discctr = n; discctr >= 1; discctr--)
    Push (discctr, s);
}

int
Pop (s)
     int s;
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
}

Move (s1, s2)
     int s1, s2;
{
  Push (Pop (s1), s2);
  movesdone = movesdone + 1;
}

tower (i, j, k)
     int i, j, k;
{
  int other;
  cycles_start = rdtsc();
#ifndef NO_STAP_SDT
  BENCH_TOWER (cycles_start,i, j, k);
#else
BENCH_TOWER:
#endif
  if (__builtin_expect(tower_cycles_idx != CYCLES_HBOUND,0))
    {
      tower_cycles_idx++;
      (*tower_cycles)[tower_cycles_idx] = rdtsc() - cycles_start;
    }
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


Towers ()
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
}


/* The eight queens problem, solved 50 times. */
/*
	type    
	    doubleboard =   2..16;
	    doublenorm  =   -7..7;
	    boardrange  =   1..8;
	    aarray      =   array [boardrange] of boolean;
	    barray      =   array [doubleboard] of boolean;
	    carray      =   array [doublenorm] of boolean;
	    xarray      =   array [boardrange] of boardrange;
*/

Try (i, q, a, b, c, x)
     int i, *q, a[], b[], c[], x[];
{
  int j;
  j = 0;
  *q = false;
  while ((!*q) && (j != 8))
    {
      j = j + 1;
      cycles_start = rdtsc();
#ifndef NO_STAP_SDT
      BENCH_TRY (cycles_start,i, q, a, b, c, x);
#else
BENCH_TRY:
#endif
      if (__builtin_expect(try_cycles_idx != CYCLES_HBOUND,0))
	{
	  try_cycles_idx++;
	  (*try_cycles)[try_cycles_idx] = rdtsc() - cycles_start;
	}
      *q = false;
      if (b[j] && a[i + j] && c[i - j + 7])
	{
	  x[i] = j;
	  b[j] = false;
	  a[i + j] = false;
	  c[i - j + 7] = false;
	  if (i < 8)
	    {
	      Try (i + 1, q, a, b, c, x);
	      if (!*q)
		{
		  b[j] = true;
		  a[i + j] = true;
		  c[i - j + 7] = true;
		}
	    }
	  else
	    *q = true;
	}
    }
}

Doit ()
{
  int i, q;
  int a[9], b[17], c[15], x[9];
  i = 0 - 7;
  while (i <= 16)
    {
      if ((i >= 1) && (i <= 8))
	a[i] = true;
      if (i >= 2)
	b[i] = true;
      if (i <= 7)
	c[i + 7] = true;
      i = i + 1;
    }

  Try (1, &q, b, a, c, x);
  if (!q)
    printf (" Error in Queens.\n");
}

Queens ()
{
  int i;
  for (i = 1; i <= 50; i++)
    Doit ();
}

/* Multiplies two integer matrices. */

Initmatrix (m)
     int m[rowsize + 1][rowsize + 1];
{
  int temp, i, j;
  for (i = 1; i <= rowsize; i++)
    for (j = 1; j <= rowsize; j++)
      temp = Rand ();
  m[i][j] = temp - (temp / 120) * 120 - 60;
}

Innerproduct (result, a, b, row, column)
     int *result, a[rowsize + 1][rowsize + 1], b[rowsize + 1][rowsize + 1],
       row, column;
{
  int i;
  *result = 0;
  for (i = 1; i <= rowsize; i++)
    {
      cycles_start = rdtsc();
#ifndef NO_STAP_SDT
      BENCH_INNERPRODUCT (cycles_start,a, b, row, column);
#else
BENCH_INNERPRODUCT:
#endif
      if (__builtin_expect(innerproduct_cycles_idx != CYCLES_HBOUND,0))
	{
	  innerproduct_cycles_idx++;
	  (*innerproduct_cycles)[innerproduct_cycles_idx] = rdtsc() - cycles_start;
	}
      *result = *result + a[row][i] * b[i][column];
    }

}

Intmm ()
{
  int i, j;
  Initrand ();
  Initmatrix (ima);
  Initmatrix (imb);
  for (i = 1; i <= rowsize; i++)
    for (j = 1; j <= rowsize; j++)
      Innerproduct (&imr[i][j], ima, imb, i, j);
}


/* Multiplies two real matrices. */

rInitmatrix (m)
     float m[rowsize + 1][rowsize + 1];
{
  int temp, i, j;
  for (i = 1; i <= rowsize; i++)
    for (j = 1; j <= rowsize; j++)
      temp = Rand ();
  m[i][j] = (temp - (temp / 120) * 120 - 60) / 3;
}

rInnerproduct (result, a, b, row, column)
     float *result, a[rowsize + 1][rowsize + 1], b[rowsize + 1][rowsize + 1];
     int row, column;
{
  int i;
  *result = 0.0;
  for (i = 1; i <= rowsize; i++)
    *result = *result + a[row][i] * b[i][column];
}

Mm ()
{
  int i, j;
  Initrand ();
  rInitmatrix (rma);
  rInitmatrix (rmb);
  for (i = 1; i <= rowsize; i++)
    for (j = 1; j <= rowsize; j++)
      rInnerproduct (&rmr[i][j], rma, rmb, i, j);
}


int
Fit (i, j)
     int i, j;
{
  int k;
  for (k = 0; k <= piecemax[i]; k++)
    if (p[i][k])
      if (puzzl[j + k])
	return (false);
  return (true);
}

int
Place (i, j)
     int i, j;
{
  int k;
  for (k = 0; k <= piecemax[i]; k++)
    if (p[i][k])
      puzzl[j + k] = true;
  piececount[class[i]] = piececount[class[i]] - 1;
  for (k = j; k <= SIZE; k++)
    if (!puzzl[k])
      {
	return (k);
      }
  return (0);
}

Remove (i, j)
     int i, j;
{
  int k;
  for (k = 0; k <= piecemax[i]; k++)
    if (p[i][k])
      puzzl[j + k] = false;
  piececount[class[i]] = piececount[class[i]] + 1;
}

int
Trial (j)
     int j;
{
  int i, k;
  kount = kount + 1;
  for (i = 0; i <= typemax; i++)
    {
      if (piececount[class[i]] != 0)
	if (Fit (i, j))
	  {
	    k = Place (i, j);
	    cycles_start = rdtsc();
#ifndef NO_STAP_SDT
	    BENCH_TRIAL (cycles_start,i, j, k);
#else
BENCH_TRIAL:
#endif
	    if (__builtin_expect(trial_cycles_idx != CYCLES_HBOUND,0))
	      {
		trial_cycles_idx++;
		(*trial_cycles)[trial_cycles_idx] = rdtsc() - cycles_start;
	      }
	    if (Trial (k) || (k == 0))
	      {
		return (true);
	      }
	    else
	      Remove (i, j);
	  }
    }
  return (false);
}

Puzzle ()
{
  int i, j, k, m;
  for (m = 0; m <= SIZE; m++)
    puzzl[m] = true;
  for (i = 1; i <= 5; i++)
    for (j = 1; j <= 5; j++)
      for (k = 1; k <= 5; k++)
	puzzl[i + d * (j + d * k)] = false;
  for (i = 0; i <= typemax; i++)
    for (m = 0; m <= SIZE; m++)
      p[i][m] = false;
  for (i = 0; i <= 3; i++)
    for (j = 0; j <= 1; j++)
      for (k = 0; k <= 0; k++)
	p[0][i + d * (j + d * k)] = true;
  class[0] = 0;
  piecemax[0] = 3 + d * 1 + d * d * 0;
  for (i = 0; i <= 1; i++)
    for (j = 0; j <= 0; j++)
      for (k = 0; k <= 3; k++)
	p[1][i + d * (j + d * k)] = true;
  class[1] = 0;
  piecemax[1] = 1 + d * 0 + d * d * 3;
  for (i = 0; i <= 0; i++)
    for (j = 0; j <= 3; j++)
      for (k = 0; k <= 1; k++)
	p[2][i + d * (j + d * k)] = true;
  class[2] = 0;
  piecemax[2] = 0 + d * 3 + d * d * 1;
  for (i = 0; i <= 1; i++)
    for (j = 0; j <= 3; j++)
      for (k = 0; k <= 0; k++)
	p[3][i + d * (j + d * k)] = true;
  class[3] = 0;
  piecemax[3] = 1 + d * 3 + d * d * 0;
  for (i = 0; i <= 3; i++)
    for (j = 0; j <= 0; j++)
      for (k = 0; k <= 1; k++)
	p[4][i + d * (j + d * k)] = true;
  class[4] = 0;
  piecemax[4] = 3 + d * 0 + d * d * 1;
  for (i = 0; i <= 0; i++)
    for (j = 0; j <= 1; j++)
      for (k = 0; k <= 3; k++)
	p[5][i + d * (j + d * k)] = true;
  class[5] = 0;
  piecemax[5] = 0 + d * 1 + d * d * 3;
  for (i = 0; i <= 2; i++)
    for (j = 0; j <= 0; j++)
      for (k = 0; k <= 0; k++)
	p[6][i + d * (j + d * k)] = true;
  class[6] = 1;
  piecemax[6] = 2 + d * 0 + d * d * 0;
  for (i = 0; i <= 0; i++)
    for (j = 0; j <= 2; j++)
      for (k = 0; k <= 0; k++)
	p[7][i + d * (j + d * k)] = true;
  class[7] = 1;
  piecemax[7] = 0 + d * 2 + d * d * 0;
  for (i = 0; i <= 0; i++)
    for (j = 0; j <= 0; j++)
      for (k = 0; k <= 2; k++)
	p[8][i + d * (j + d * k)] = true;
  class[8] = 1;
  piecemax[8] = 0 + d * 0 + d * d * 2;
  for (i = 0; i <= 1; i++)
    for (j = 0; j <= 1; j++)
      for (k = 0; k <= 0; k++)
	p[9][i + d * (j + d * k)] = true;
  class[9] = 2;
  piecemax[9] = 1 + d * 1 + d * d * 0;
  for (i = 0; i <= 1; i++)
    for (j = 0; j <= 0; j++)
      for (k = 0; k <= 1; k++)
	p[10][i + d * (j + d * k)] = true;
  class[10] = 2;
  piecemax[10] = 1 + d * 0 + d * d * 1;
  for (i = 0; i <= 0; i++)
    for (j = 0; j <= 1; j++)
      for (k = 0; k <= 1; k++)
	p[11][i + d * (j + d * k)] = true;
  class[11] = 2;
  piecemax[11] = 0 + d * 1 + d * d * 1;
  for (i = 0; i <= 1; i++)
    for (j = 0; j <= 1; j++)
      for (k = 0; k <= 1; k++)
	p[12][i + d * (j + d * k)] = true;
  class[12] = 3;
  piecemax[12] = 1 + d * 1 + d * d * 1;
  piececount[0] = 13;
  piececount[1] = 3;
  piececount[2] = 1;
  piececount[3] = 1;
  m = 1 + d * (1 + d * 1);
  kount = 0;
  if (Fit (0, m))
    n = Place (0, m);
  else
    printf ("Error1 in Puzzle\n");
  if (!Trial (n))
    printf ("Error2 in Puzzle.\n");
  else if (kount != 2005)
    printf ("Error3 in Puzzle.\n");
}

/* Sorts an array using quicksort */

Initarr ()
{
  int i, temp;
  Initrand ();
  biggest = 0;
  littlest = 0;
  for (i = 1; i <= sortelements; i++)
    {
      temp = Rand ();
      sortlist[i] = temp - (temp / 100000) * 100000 - 50000;
      if (sortlist[i] > biggest)
	biggest = sortlist[i];
      else if (sortlist[i] < littlest)
	littlest = sortlist[i];
    }
}

Quicksort (a, l, r)
     int a[], l, r;
{
  int i, j, x, w;

  i = l;
  j = r;
  x = a[(l + r) / 2];
  do
    {
      while (a[i] < x)
	i = i + 1;
      while (x < a[j])
	j = j - 1;
      cycles_start = rdtsc();
#ifndef NO_STAP_SDT
      BENCH_QUICKSORT (cycles_start,i, j, x);
#else
BENCH_QUICKSORT:
#endif
      if (__builtin_expect(quicksort_cycles_idx != CYCLES_HBOUND,0))
	{
	  quicksort_cycles_idx++;
	  (*quicksort_cycles)[quicksort_cycles_idx] = rdtsc() - cycles_start;
	}
      if (i <= j)
	{
	  w = a[i];
	  a[i] = a[j];
	  a[j] = w;
	  i = i + 1;
	  j = j - 1;
	}
    }
  while (i <= j);
  if (l < j)
    Quicksort (a, l, j);
  if (i < r)
    Quicksort (a, i, r);
}


Quick ()
{
  Initarr ();
  Quicksort (sortlist, 1, sortelements);
  if ((sortlist[1] != littlest) || (sortlist[sortelements] != biggest))
    printf (" Error in Quick.\n");
}


tInitarr ()
{
  int i, temp;
  Initrand ();
  biggest = 0;
  littlest = 0;
  for (i = 1; i <= sortelements; i++)
    {
      temp = Rand ();
      sortlist[i] = temp - (temp / 100000) * 100000 - 50000;
      if (sortlist[i] > biggest)
	biggest = sortlist[i];
      else if (sortlist[i] < littlest)
	littlest = sortlist[i];
    }
}

CreateNode (t, n)
     struct node **t;
     int n;
{
  *t = (struct node *) malloc (sizeof (struct node));
  (*t)->left = nil;
  (*t)->right = nil;
  (*t)->val = n;
}

Insert (n, t)
     int n;
     struct node *t;
{
  cycles_start = rdtsc();
#ifndef NO_STAP_SDT
  BENCH_INSERT (cycles_start,n, t);
#else
BENCH_INSERT:
#endif
  if (__builtin_expect(insert_cycles_idx != CYCLES_HBOUND,0))
    {
      insert_cycles_idx++;
      (*insert_cycles)[insert_cycles_idx] = rdtsc() - cycles_start;
    }
  if (n > t->val)
    if (t->left == nil)
      CreateNode (&t->left, n);
    else
      Insert (n, t->left);
  else if (n < t->val)
    if (t->right == nil)
      CreateNode (&t->right, n);
    else
      Insert (n, t->right);
}

int
Checktree (p)
     struct node *p;
{
  int result;
  result = true;
  if (p->left != nil)
    if (p->left->val <= p->val)
      result = false;
    else
      result = Checktree (p->left) && result;
  if (p->right != nil)
    if (p->right->val >= p->val)
      result = false;
    else
      result = Checktree (p->right) && result;
  return (result);
}

Trees ()
{
  int i;
  tInitarr ();
  tree = (struct node *) malloc (sizeof (struct node));
  tree->left = nil;
  tree->right = nil;
  tree->val = sortlist[1];
  for (i = 2; i <= sortelements; i++)
    Insert (sortlist[i], tree);
  if (!Checktree (tree))
    printf (" Error in Tree.\n");
}


/* Sorts an array using bubblesort */

bInitarr ()
{
  int i, temp;
  Initrand ();
  biggest = 0;
  littlest = 0;
  for (i = 1; i <= srtelements; i++)
    {
      temp = Rand ();
      sortlist[i] = temp - (temp / 100000) * 100000 - 50000;
      if (sortlist[i] > biggest)
	biggest = sortlist[i];
      else if (sortlist[i] < littlest)
	littlest = sortlist[i];
    }
}

Bubble ()
{
  int i, j;
  bInitarr ();
  top = srtelements;

  while (top > 1)
    {

      i = 1;
      while (i < top)
	{
	  cycles_start = rdtsc();
#ifndef NO_STAP_SDT
	  BENCH_BUBBLE (cycles_start,i, top);
#else
BENCH_BUBBLE:
#endif
	  if (__builtin_expect(bubble_cycles_idx != CYCLES_HBOUND,0))
	    {
	      bubble_cycles_idx++;
	      (*bubble_cycles)[bubble_cycles_idx] = rdtsc() - cycles_start;
	    }
	  if (sortlist[i] > sortlist[i + 1])
	    {
	      j = sortlist[i];
	      sortlist[i] = sortlist[i + 1];
	      sortlist[i + 1] = j;
	    }
	  i = i + 1;
	}

      top = top - 1;
    }
  if ((sortlist[1] != littlest) || (sortlist[srtelements] != biggest))
    printf ("Error3 in Bubble.\n");
}


float
Cos (x)
     float x;
{
  int i, factor;
  float result, power;

  result = 1.0;
  factor = 1;
  power = x;
  for (i = 2; i <= 10; i++)
    {
      factor = factor * i;
      power = power * x;
      if ((i & 1) == 0)
	{
	  if ((i & 3) == 0)
	    result = result + power / factor;
	  else
	    result = result - power / factor;
	}
    }
  return (result);
}

int
Min0 (arg1, arg2)
     int arg1, arg2;
{
  if (arg1 < arg2)
    return (arg1);
  else
    return (arg2);
}

Printcomplex (arg1, arg2, zarray, start, finish, increment)
     int arg1, arg2, start, finish, increment;
     struct complex zarray[];
{
  int i;
  printf ("\n");

  i = start;
  do
    {
      printf ("  %15.3e%15.3e", zarray[i].rp, zarray[i].ip);
      i = i + increment;
      printf ("  %15.3e%15.3e", zarray[i].rp, zarray[i].ip);
      printf ("\n");
      i = i + increment;
    }
  while (i <= finish);

}

Uniform11 (iy, yfl)
     int iy;
     float yfl;
{
  iy = (4855 * iy + 1731) & 8191;
  yfl = iy / 8192.0;
}

Exptab (n, e)
     int n;
     struct complex e[];

{
  float theta, divisor, h[26];
  int i, j, k, l, m;

  theta = 3.1415926536;
  divisor = 4.0;
  for (i = 1; i <= 25; i++)
    {
      h[i] = 1 / (2 * Cos (theta / divisor));
      divisor = divisor + divisor;
    }

  m = n / 2;
  l = m / 2;
  j = 1;
  e[1].rp = 1.0;
  e[1].ip = 0.0;
  e[l + 1].rp = 0.0;
  e[l + 1].ip = 1.0;
  e[m + 1].rp = -1.0;
  e[m + 1].ip = 0.0;

  do
    {
      i = l / 2;
      k = i;

      do
	{
	  e[k + 1].rp = h[j] * (e[k + i + 1].rp + e[k - i + 1].rp);
	  e[k + 1].ip = h[j] * (e[k + i + 1].ip + e[k - i + 1].ip);
	  k = k + l;
	}
      while (k <= m);

      j = Min0 (j + 1, 25);
      l = i;
    }
  while (l > 1);

}

Fft (n, z, w, e, sqrinv)
     int n;
     struct complex z[], w[];
     struct complex e[];
     float sqrinv;
{
  int i, j, k, l, m, index;
  m = n / 2;
  l = 1;

  do
    {
      k = 0;
      j = l;
      i = 1;

      do
	{

	  do
	    {
	      cycles_start = rdtsc();
#ifndef NO_STAP_SDT
	      BENCH_FFT (cycles_start,n, z, w, e);
#else
BENCH_FFT:
#endif
	      if (__builtin_expect(fft_cycles_idx != CYCLES_HBOUND,0))
		{
		  fft_cycles_idx++;
		  (*fft_cycles)[fft_cycles_idx] = rdtsc() - cycles_start;
		}
	      w[i + k].rp = z[i].rp + z[m + i].rp;
	      w[i + k].ip = z[i].ip + z[m + i].ip;
	      w[i + j].rp = e[k + 1].rp * (z[i].rp - z[i + m].rp)
		- e[k + 1].ip * (z[i].ip - z[i + m].ip);
	      w[i + j].ip = e[k + 1].rp * (z[i].ip - z[i + m].ip)
		+ e[k + 1].ip * (z[i].rp - z[i + m].rp);
	      i = i + 1;
	    }
	  while (i <= j);

	  k = j;
	  j = k + l;
	}
      while (j <= m);

      index = 1;
      do
	{
	  z[index] = w[index];
	  index = index + 1;
	}
      while (index <= n);
      l = l + l;
    }
  while (l <= m);

  for (i = 1; i <= n; i++)
    {
      z[i].rp = sqrinv * z[i].rp;
      z[i].ip = -sqrinv * z[i].ip;
    };

}

Oscar ()
{
  int i;
  Exptab (fftsize, e);
  seed = 5767;
  for (i = 1; i <= fftsize; i++)
    {
      Uniform11 (seed, zr);
      Uniform11 (seed, zi);
      z[i].rp = 20.0 * zr - 10.0;
      z[i].ip = 20.0 * zi - 10.0;
    };


  for (i = 1; i <= 20; i++)
    {
      Fft (fftsize, z, w, e, 0.0625);
    }
}

#define Start()
#define Stop(a,b)

#ifndef LOOP
#define LOOP 500
#endif

int
main ()
{
  int i,j;

  permute_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  tower_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  try_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  innerproduct_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  trial_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  quicksort_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  insert_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  bubble_cycles = calloc(CYCLES_HBOUND,sizeof(long));
  fft_cycles = calloc(CYCLES_HBOUND,sizeof(long));

  Start ();
  for (i = 0; i < LOOP; i++)
    Perm ();
  Stop ("Perm", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Towers ();
  Stop ("Towers", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Queens ();
  Stop ("Queens", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Intmm ();
  Stop ("Intmm", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Mm ();
  Stop ("Mm", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Puzzle ();
  Stop ("Puzzle", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Quick ();
  Stop ("Quick", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Bubble ();
  Stop ("Bubble", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Trees ();
  Stop ("Trees", LOOP);
  Start ();
  for (i = 0; i < LOOP; i++)
    Oscar ();
  Stop ("Oscar", LOOP);

  for (j = 1; j <= permute_cycles_idx; j++)
    permute_cycles_avg += (*permute_cycles)[j];
  printf ("permute_cycles %#lx\n", permute_cycles_avg / permute_cycles_idx);
  for (j = 1; j <= tower_cycles_idx; j++)
    tower_cycles_avg += (*tower_cycles)[j];
  printf ("tower_cycles %#lx\n", tower_cycles_avg / tower_cycles_idx);
  for (j = 1; j <= try_cycles_idx; j++)
    try_cycles_avg += (*try_cycles)[j];
  printf ("try_cycles %#lx\n", try_cycles_avg / try_cycles_idx);
  for (j = 1; j <= innerproduct_cycles_idx; j++)
    innerproduct_cycles_avg += (*innerproduct_cycles)[j];
  printf ("innerproduct_cycles %#lx\n", innerproduct_cycles_avg / innerproduct_cycles_idx);
  for (j = 1; j <= trial_cycles_idx; j++)
    trial_cycles_avg += (*trial_cycles)[j];
  printf ("trial_cycles %#lx\n", trial_cycles_avg / trial_cycles_idx);
  for (j = 1; j <= quicksort_cycles_idx; j++)
    quicksort_cycles_avg += (*quicksort_cycles)[j];
  printf ("quicksort_cycles %#lx\n", quicksort_cycles_avg / quicksort_cycles_idx);
  for (j = 1; j <= insert_cycles_idx; j++)
    insert_cycles_avg += (*insert_cycles)[j];
  printf ("insert_cycles %#lx\n", insert_cycles_avg / insert_cycles_idx);
  for (j = 1; j <= bubble_cycles_idx; j++)
    bubble_cycles_avg += (*bubble_cycles)[j];
  printf ("bubble_cycles %#lx\n", bubble_cycles_avg / bubble_cycles_idx);
  for (j = 1; j <= fft_cycles_idx; j++)
    fft_cycles_avg += (*fft_cycles)[j];
  printf ("fft_cycles %#lx\n", fft_cycles_avg / fft_cycles_idx);
  return 0;
}
