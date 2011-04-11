#include <linux/namei.h>

/* kernel commit c9c6cac0c2bdbda */
int ____autoconf_func(const char *name)
{
    return kern_path_parent(name, NULL);
}
