/* Use the os timer as a poor-man's time based profiler for a UP system */

/* Demonstrates the beginnings of a generic framework for */
/* asynchronous probes using the runtime */

/* @todo NOTE: the statement: regs = task_pt_regs(current); */
/* isn't working in the way I would expect. The timer callback */
/* happens, but I don't get a reasonable value in regs->eip */
/* Can this routine be called during the timer callback? */

/* os includes */
#include "linux/timer.h"

/* define this if you don't want to use relayfs.  Normally */
/* you want relayfs, unless you need a realtime stream of data */

/* #define STP_NETLINK_ONLY */

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

/* since we don't have aggregation maps yet, try regular maps */
#define NEED_INT64_VALS

#define KEY1_TYPE INT64
#include "map-keys.c"

#include "map.c"

#include "stat.c"
#include "stack.c"

MODULE_DESCRIPTION("SystemTap probe: os_timer");
MODULE_AUTHOR("Charles Spirakis <charles.spirakis@intel.com>");

Stat addr;
MAP cur_addr;

/* A generic asynchorous probe entry point */
void inst_async(struct pt_regs *regs)
{
    unsigned long ip = regs->eip;

    /* can we generate a histogram of ip addresses seen? */
    _stp_stat_add(addr, 1);

    /* Create a map of interrupted addresses seen */
    /* really want a map of image name / address */
    _stp_map_key_int64(cur_addr, ip);
    _stp_map_add_int64(cur_addr, 1);

    /* Need _stp_stack() and _stp_ustack()? */
    /* _stp_image() and aggregation maps */
}

static struct timer_list timer;

/* Helper function to convert from os timer callback into */
/* generic asynchronous entry point form */
static void os_timer_callback(unsigned long val)
{
    struct pt_regs *regs;

    /* setup the next timeout now so it doesn't drift */
    /* due to processing the async probe code */
    mod_timer(&timer, jiffies + val);

    /* determine pt_regs from the kernel stack */
    /* @todo This doesn't seem to get a reasonable pt_regs pointer */
    /* based on the value of regs->eip. However, KSTK_EIP() in */
    /* include/asm/processor.h implies regs->eip is valid... */
    regs = task_pt_regs(current);

    /* Call the asynchronous probe with a ptregs struct */
    inst_async(regs);
}

/* called when the module loads. */
int init_module(void)
{
    int ret;

    TRANSPORT_OPEN;

    addr = _stp_stat_init(HIST_LINEAR, 0, 1000, 100);

    cur_addr = _stp_map_new_int64(1000, INT64);

    /* register the os_timer */
    init_timer(&timer);

    timer.expires = jiffies + 50;
    timer.function = os_timer_callback;

    /* data is usd for defining when the next timeout shoud occur */
    timer.data = 50;

    add_timer(&timer);

    ret = 0;

    return ret;
}

static void probe_exit (void)
{
    /* unregister the os_timer */
    del_timer_sync(&timer);

    /* print out any colledted data, etc */
    _stp_printf ("os timer done. Currently an issue with tast_pt_regs() call so data below may not be valid\n");
    _stp_stat_print (addr, "addr: count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 0);

    _stp_map_print (cur_addr, "Count: %d\tInterrupts: %1P");
    _stp_map_del(cur_addr);

    _stp_print_flush();
}

/* required */
void cleanup_module(void)
{
    _stp_transport_close();
}

MODULE_LICENSE("GPL");
