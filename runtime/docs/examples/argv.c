MAP arglist ;

int inst_do_execve (char * filename, char __user *__user *argv, char __user *__user *envp, struct pt_regs * regs)
{
  struct map_node_str *ptr;

  _stp_list_clear (arglist);
  _stp_copy_argv_from_user (arglist, argv);

  foreach (arglist, ptr)
    _stp_printf ("%s ", _stp_get_str(ptr));
  _stp_print("\n");

}
