#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "librelay.h"

 /* relayfs base file name */
static char *stpd_filebase = "/mnt/relay/stpd/cpu";

 /* packet logging output written here, filebase0...N */
static char *stpd_outfilebase = "stpd_cpu";

#define DEFAULT_SUBBUF_SIZE (262144)
#define DEFAULT_N_SUBBUFS (4)
static unsigned subbuf_size = DEFAULT_SUBBUF_SIZE;
static unsigned n_subbufs = DEFAULT_N_SUBBUFS;

extern char *optarg;
extern int optopt;
int print_only = 0;
int quiet = 0;

static void usage(char *prog)
{
  fprintf(stderr, "%s [-p] [-q] [-b subbuf_size -n n_subbufs]\n", prog);
  fprintf(stderr, "-p  Print only.  Don't log to files.\n");
  fprintf(stderr, "-q  Quiet. Don't display trace to stdout.\n");
  fprintf(stderr, "-b subbuf_size  (default is %d)\n", DEFAULT_SUBBUF_SIZE);
  fprintf(stderr, "-b subbufs  (default is %d)\n", DEFAULT_N_SUBBUFS);
  exit(1);
}

int main(int argc, char **argv)
{
  int c;
  unsigned opt_subbuf_size = 0;
  unsigned opt_n_subbufs = 0;

  while ((c = getopt(argc, argv, "b:n:pq")) != EOF) 
    {
      switch (c) {
      case 'b':
	opt_subbuf_size = (unsigned)atoi(optarg);
	if (!opt_subbuf_size)
	  usage(argv[0]);
	break;
      case 'n':
	opt_n_subbufs = (unsigned)atoi(optarg);
	if (!opt_n_subbufs)
	  usage(argv[0]);
	break;
      case 'p':
	print_only = 1;
	break;
      case 'q':
	quiet = 1;
	break;
      default:
	usage(argv[0]);
      }
    }
	
  if ( print_only && quiet)
    {
      fprintf (stderr, "Cannot do \"-p\" and \"-q\" both.\n");
      usage(argv[0]);
    }

  if ((opt_n_subbufs && !opt_subbuf_size) ||
      (!opt_n_subbufs && opt_subbuf_size))
    usage(argv[0]);
  
  if (opt_n_subbufs && opt_n_subbufs) {
    subbuf_size = opt_subbuf_size;
    n_subbufs = opt_n_subbufs;
  }
  
  if (init_relay_app(stpd_filebase, stpd_outfilebase,
		     subbuf_size, n_subbufs, 1)) 
    {
      fprintf(stderr, "Couldn't initialize relay app. Exiting.\n");
      exit(1);
    }
	
  printf("Creating channel with %u sub-buffers of size %u.\n",
	 n_subbufs, subbuf_size);

  if (quiet)
    printf("Logging... Press Control-C to stop.\n");
  else
    printf("Press Control-C to stop.\n");

  if (relay_app_main_loop()) {
    printf("Couldn't enter main loop. Exiting.\n");
    exit(1);
  }
	
  return 0;
}
