/* Framework for new probes using the runtime */
/* this example shows both kprobes and jprobes although you */
/* likely will only want one. */

/* define this if you don't want to use relayfs.  Normally */
/* you want relayfs, unless you need a realtime stream of data */

#define STP_NETLINK_ONLY

/* How many strings to allocate. see strings.c. Default is 0. */
#define STP_NUM_STRINGS 1

/* maximum size for a string. default is 2048 */
#define STP_STRING_SIZE 2048

/* size of strings saved in maps */
#define MAP_STRING_LENGTH 256

/* width of histograms. Default 50 */
#define HIST_WIDTH 50

/* always include this.  Put all non-map defines above it. */
#include "runtime.h"

/* These are the possible values a map can have. */
/* Comment out unused ones. Comment out all if you don't need maps. */
#define NEED_INT64_VALS
#define NEED_STRING_VALS
#define NEED_STAT_VALS

/* now for each set of keys, define the key type and include map-keys.c */
/* This define a single key of int64, for example map1[2048] */
#define KEY1_TYPE INT64
#include "map-keys.c"

/* This generates code to handle keys of int64,string, for example */
/* map2[1000,"Cobalt"] */
#define KEY1_TYPE INT64
#define KEY2_TYPE STRING
#include "map-keys.c"

/* After defining all your key combinations, include map.c */
#include "map.c"

/* include if you use lists */
#include "list.c"

/* include if you use copy_from_user functions */
#include "copy.c"

/* include if you use the _stp_register_probe functions */
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: myname");
MODULE_AUTHOR("Me <myname@nowhere.com>");

/* here is an example jprobe instrumentation function. */
/* jprobes require a prototype matching the probed function. */

static int inst_meminfo_read_proc(char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
  _stp_print("Now in meminfo\n");
  _stp_stack_printj();
  _stp_ustack_print();
  jprobe_return();
  return 0;
}

/* here is an example kprobe. kprobes always take the same arguments */
static int inst_uptime_read_proc(struct kprobe *p, struct pt_regs *regs)
{
  _stp_stack_print(regs);
  _stp_ustack_print();
  return 0;
}

/* put jprobes here */
static struct jprobe jp[] = {
  {
    .kp.addr = (kprobe_opcode_t *)"meminfo_read_proc",
    .entry = (kprobe_opcode_t *) inst_meminfo_read_proc
  },
};

/* put kprobes here */
static struct kprobe kp[] = {
  {
    .addr = "uptime_read_proc",
    .pre_handler = inst_uptime_read_proc,
  }
};

#define NUM_JPROBES (sizeof(jp)/sizeof(struct jprobe))
#define NUM_KPROBES (sizeof(kp)/sizeof(struct kprobe))

/* called when the module loads. */
int probe_start(void)
{
  /* initialize any data or variables */


  /* register any jprobes */
  int ret = _stp_register_jprobes (jp, NUM_JPROBES);

  /* Register any kprobes and jprobes. */
  /* You probably only have one type */
  if (ret >= 0)
    if ((ret = _stp_register_kprobes (kp, NUM_KPROBES)) < 0)
      _stp_unregister_jprobes (jp, NUM_JPROBES);

  return ret;
}


void probe_exit (void)
{
  /* unregister the probes */
  _stp_unregister_jprobes (jp, NUM_JPROBES);
  _stp_unregister_kprobes (kp, NUM_KPROBES);

  /* print out any colledted data, etc */
  _stp_printf ("whatever I want to say\n");
  _stp_print_flush();
}
