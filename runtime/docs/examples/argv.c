MAP arglist ;

int inst_do_execve (char * filename, char __user *__user *argv, char __user *__user *envp, struct pt_regs * regs)
{
  struct map_node_str *ptr;

  _stp_copy_argv_from_user (arglist, argv);

  foreach (arglist, ptr)
    printk ("%s ", ptr->str);
  printk ("\n");
}
