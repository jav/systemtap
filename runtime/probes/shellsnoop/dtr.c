#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */

#include "runtime.h"
#include "io.c"
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
      dlog ("%d\t%d\t%d\t%s ", current->uid, current->pid, current->parent->pid, filename);

      _stp_map_key_long (pids, current->pid);
      _stp_map_set_int64 (pids, 1);
      
      _stp_list_clear (arglist);
      _stp_copy_argv_from_user (arglist, argv);
      foreach (arglist, ptr)
	printk ("%s ", ptr->str);
      printk ("\n");
    }
  jprobe_return();
  return 0;
}

struct file * inst_filp_open (const char * filename, int flags, int mode)
{
  _stp_map_key_long (pids, current->pid);
  if (_stp_map_get_int64 (pids))
    dlog ("%d\t%d\t%s\tO %s\n", current->pid, current->parent->pid, current->comm, filename);
  
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_read (unsigned int fd, char __user * buf, size_t count)
{
  _stp_map_key_long (pids, current->pid);
  if (_stp_map_get_int64 (pids))
    dlog ("%d\t%d\t%s\tR %d\n", current->pid, current->parent->pid, current->comm, fd);
  
  jprobe_return();
  return 0;
}

asmlinkage ssize_t inst_sys_write (unsigned int fd, const char __user * buf, size_t count)
{
  size_t len;
  char str[256];
  _stp_map_key_long (pids, current->pid);
  if (_stp_map_get_int64 (pids))
    {
      if (count < 64) 
	len = count;
      else 
	len = 64;
      len = _stp_strncpy_from_user(str, buf, len);
      if (len < 0) len = 0;
      str[len] = 0;
      dlog ("%d\t%d\t%s\tW %s\n", current->pid, current->parent->pid, current->comm, str);
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

static int init_dtr(void)
{
  int ret;

  pids = _stp_map_new (10000, INT64);
  arglist = _stp_list_new (10, STRING);

  ret = _stp_register_jprobes (dtr_probes, MAX_DTR_ROUTINE);

  dlog("instrumentation is enabled...\n");
  return ret;
}

static void cleanup_dtr(void)
{
  _stp_unregister_jprobes (dtr_probes, MAX_DTR_ROUTINE);
  _stp_map_del (pids);
  dlog("EXIT\n");
}

module_init(init_dtr);
module_exit(cleanup_dtr);
MODULE_LICENSE("GPL");

