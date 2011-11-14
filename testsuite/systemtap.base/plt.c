#define _GNU_SOURCE
#include <string.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <printf.h>

#define SIZE 256

/* Examples from libc.info */

#if defined (LIBPLT2) || defined (NOLIBPLT)
void *
fatal (const char *ptr)
{
  puts (ptr);
  exit (1);
}


void *
xmalloc (size_t size)
{
  register void *value = malloc (size);
  if (value == 0)
    fatal ("virtual memory exhausted");
  return value;
}

char *
savestring (const char *ptr)
{
  size_t len = sizeof (ptr);
  register char *value = (char *) xmalloc (len + 1);
  value[len] = '\0';
  return (char *) memcpy (value, ptr, len);
}

int
open2 (char *str1, char *str2, int flags)
{
  char *end;
  char *name = (char *) alloca (strlen (str1) + strlen (str2) + 1);
  end = stpcpy (stpcpy (name, str1), str2);
  return open (name, flags) && end == name;
}

int
open3 (char *str1, char *str2, int flags)
{
  char *end;
  char *name = (char *) malloc (strlen (str1) + strlen (str2) + 1);
  int desc;
  if (name == 0)
    fatal ("virtual memory exceeded");
  end = stpcpy (stpcpy (name, str1), str2);
  desc = open (name, flags);
  free (name);
  return desc && end == str1;
}

char *
basename2 (char *prog)
{
  return basename (prog);
}

/* Define an array of critters to sort. */

struct critter
{
  const char *name;
  const char *species;
};

struct critter muppets[] =
  {
    {"Kermit", "frog"},
    {"Piggy", "pig"},
    {"Gonzo", "whatever"},
    {"Fozzie", "bear"},
    {"Sam", "eagle"},
    {"Robin", "frog"},
    {"Animal", "animal"},
    {"Camilla", "chicken"},
    {"Sweetums", "monster"},
    {"Dr. Strangepork", "pig"},
    {"Link Hogthrob", "pig"},
    {"Zoot", "human"},
    {"Dr. Bunsen Honeydew", "human"},
    {"Beaker", "human"},
    {"Swedish Chef", "human"}
  };

int count = sizeof (muppets) / sizeof (struct critter);

/* This is the comparison function used for sorting and searching. */

int
critter_cmp (const void *c1, const void *c2)
{
  return strcmp (((const struct critter*)c1)->name, ((const struct critter*)c2)->name);
}


/* Print information about a critter. */

void
print_critter (const struct critter *c)
{
  printf ("%s, the %s\n", c->name, c->species);
}


/* Do the lookup into the sorted array. */

void
find_critter (const char *name)
{
  struct critter target, *result;
  target.name = name;
  result = bsearch (&target, muppets, count, sizeof (struct critter),
		    critter_cmp);
  if (result)
    print_critter (result);
  else
    printf ("Couldn't find %s.\n", name);
}

int
critters (void)
{
  int i;

  for (i = 0; i < count; i++)
    print_critter (&muppets[i]);
  printf ("\n");

  qsort (muppets, count, sizeof (struct critter), critter_cmp);
  for (i = 0; i < count; i++)
    print_critter (&muppets[i]);
  printf ("\n");

  find_critter ("Kermit");
  find_critter ("Gonzo");
  find_critter ("Janice");

  return 0;
}

typedef struct
{
  char *name;
}
  Widget;

int
print_widget (FILE *stream,
	      const struct printf_info *info,
	      const void *const *args)
{
  const Widget *w;
  char *buffer;
  int len;

  /* Format the output into a string. */
  w = *((const Widget **) (args[0]));
  len = asprintf (&buffer, "<Widget %p: %s>", w, w->name);
  if (len == -1)
    return -1;

  /* Pad to the minimum field width and print to the stream. */
  len = fprintf (stream, "%*s",
		 (info->left ? -info->width : info->width),
		 buffer);
  /* Clean up and return. */
  free (buffer);
  return len;
}


/* Type of a printf specifier-arginfo function.
   INFO gives information about the format specification.
   N, ARGTYPES, *SIZE has to contain the size of the parameter for
   user-defined types, and return value are as for parse_printf_format
   except that -1 should be returned if the handler cannot handle
   this case.  This allows to partially overwrite the functionality
   of existing format specifiers.  */

int
print_widget_arginfo (const struct printf_info *info, size_t n,
		      int *argtypes)
{
  /* We always take exactly one argument and this is a pointer to the
     structure.. */
  if (n > 0)
    argtypes[0] = PA_POINTER;
  return 1;
}


int
widgets (void)
{
  /* Make a widget to print. */
  Widget mywidget;
  mywidget.name = "mywidget";

  /* Register the print function for widgets. */
  register_printf_function ('W', print_widget, print_widget_arginfo);

  /* Now print the widget. */
  printf ("|%W|\n", &mywidget);
  printf ("|%35W|\n", &mywidget);
  printf ("|%-35W|\n", &mywidget);

  return;
}


int
datetime (void)
{
  char buffer[SIZE];
  time_t curtime;
  struct tm *loctime;

  /* Get the current time. */
  curtime = time (NULL);

  /* Convert it to local time representation. */
  loctime = localtime (&curtime);

  /* Print out the date and time in the standard format. */
  fputs (asctime (loctime), stdout);

  /* Print it out in a nice format. */
  strftime (buffer, SIZE, "Today is %A, %B %d.\n", loctime);
  fputs (buffer, stdout);
  strftime (buffer, SIZE, "The time is %I:%M %p.\n", loctime);
  fputs (buffer, stdout);

  return 0;
}
#endif


#if defined (LIBPLT1) || defined (NOLIBPLT)
void
zenme ()
{
  xmalloc (32);
  savestring ("abcdefghijklmnopqrstuvwxyz");
  open2 ("/dev", "/null", O_RDONLY);
  open3 ("/dev", "/null", O_RDONLY);
  basename2 ("/dev/null");
  critters();
  widgets();
  datetime();
}
#endif

#if defined (NOLIBPLT) || defined (ONLY_MAIN)
int
main ()
{
  zenme ();
}
#endif
