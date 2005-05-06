#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */

#define STP_NETLINK_ONLY
#define STP_NUM_STRINGS 1

#include "runtime.h"
#include "map.c"
#include "copy.c"
#include "probes.c"

MODULE_DESCRIPTION("SystemTap probe: shellsnoop");
MODULE_AUTHOR("Martin Hunt <hunt@redhat.com>");

MAP pids, arglist ;

int inst_do_execve (char * filename, char __user *__user *argv, char __user *__user *envp, struct pt_regs * regs)
{
  struct map_node_str *ptr;

  /* watch shells only */
  /* FIXME: detect more shells, like csh, tcsh, zsh */
  
  if (!strcmp(current->comm,"bash") || !strcmp(current->comm,"sh") || !strcmp(current->comm, "zsh")
      || !strcmp(current->comm, "tcsh") || !strcmp(current->comm, "pdksh"))
    {
      _stp_printf ("%d\t%d\t%d\t%s ", current->uid, current->pid, current->parent->pid, filename);

      _stp_map_key_long (pids, current->pid);
      _stp_map_set_int64 (pids, 1);
      
      _stp_list_clear (arglist);
      _stp_copy_argv_from_user (arglist, argv);
      
      foreach (arglist, ptr)
	_stp_printf ("%s ", ptr->str);
      
      _stp_print_flush();
    }
  jprobe_return();
  return 0;
}

struct file * inst_filp_open (const char * filename, int flags, int mode)
{
  _stp_map_key_long (pids, current->pid);
  if (_stp_map_get_int64 (pids))
    _stp_printf ("%d\t%d\t%s\tO %s", current->pid, current->parent->pid, current->comm, filename);

  _stp_print_flush();
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_read (unsigned int fd, char __user * buf, size_t count)
{
  _stp_map_key_long (pids, current->pid);
  if (_stp_map_get_int64 (pids))
    _stp_printf ("%d\t%d\t%s\tR %d", current->pid, current->parent->pid, current->comm, fd);
  
  _stp_print_flush();
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  _stp_map_key_long (pids, current->pid);
  if (_stp_map_get_int64 (pids))
    {
      String str = _stp_string_init (0);
      _stp_string_from_user(str, buf, count);
      _stp_printf ("%d\t%d\t%s\tW %s", current->pid, current->parent->pid, current->comm, str->buf);
      _stp_print_flush();
    }
  
  jprobe_return();
  return 0;
}

static struct jprobe dtr_probes[] = {
  {
    .kp.addr = (kprobe_opcode_t *)"do_execve",
    .entry = (kprobe_opcode_t *) inst_do_execve
  },
  {
    .kp.addr = (kprobe_opcode_t *)"filp_open",
    .entry = (kprobe_opcode_t *) inst_filp_open
  },
  {
    .kp.addr = (kprobe_opcode_t *)"sys_read",
    .entry = (kprobe_opcode_t *) inst_sys_read
  },
  {
    .kp.addr = (kprobe_opcode_t *)"sys_write",
    .entry = (kprobe_opcode_t *) inst_sys_write
  }, 
};

#define MAX_DTR_ROUTINE (sizeof(dtr_probes)/sizeof(struct jprobe))

static unsigned n_subbufs = 4;
module_param(n_subbufs, uint, 0);
MODULE_PARM_DESC(n_subbufs, "number of sub-buffers per per-cpu buffer");

static unsigned subbuf_size = 65536;
module_param(subbuf_size, uint, 0);
MODULE_PARM_DESC(subbuf_size, "size of each per-cpu sub-buffers");

static int pid;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "daemon pid");

static int init_dtr(void)
{
	int ret;

	if (!pid) {
		printk("init_dtr: Can't start without daemon pid\n");		
		return -1;
	}

	if (_stp_transport_open(n_subbufs, subbuf_size, pid) < 0) {
		printk("init_dtr: Couldn't open transport\n");		
		return -1;
	}

	pids = _stp_map_new (10000, INT64);
	arglist = _stp_list_new (10, STRING);

	ret = _stp_register_jprobes (dtr_probes, MAX_DTR_ROUTINE);
	
	printk("instrumentation is enabled... %s\n", __this_module.name);

	return ret;
}

static int exited; /* FIXME: this is a stopgap - if we don't do this
		    * and are manually removed, bad things happen */

static void probe_exit (void)
{
	exited = 1;

	_stp_unregister_jprobes (dtr_probes, MAX_DTR_ROUTINE);

	_stp_print ("In probe_exit now.");
	_stp_map_del (pids);
	_stp_print_flush();
}

static void cleanup_dtr(void)
{
	if (!exited)
		probe_exit();
	
	_stp_transport_close();
}

module_init(init_dtr);
module_exit(cleanup_dtr);
MODULE_LICENSE("GPL");

