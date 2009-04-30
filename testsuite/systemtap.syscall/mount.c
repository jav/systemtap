/* COVERAGE: mount oldumount umount */
#include <sys/types.h>
#include <sys/mount.h>

#ifndef MNT_FORCE
#define MNT_FORCE    0x00000001      /* Attempt to forcibily umount */
#endif

#ifndef MNT_DETACH
#define MNT_DETACH   0x00000002      /* Just detach from the tree */
#endif

#ifndef MNT_EXPIRE
#define MNT_EXPIRE   0x00000004      /* Mark for expiry */
#endif

int main()
{
  mount ("mount_source", "mount_target", "ext2", MS_BIND|MS_NOATIME|MS_NODIRATIME|MS_NOSUID, "some arguments");
  //staptest// mount ("mount_source", "mount_target", "ext2", MS_BIND|MS_NOATIME|MS_NODIRATIME|MS_NOSUID, "some arguments") = -NNNN (ENOENT)

  umount("umount_target");
  //staptest// umount ("umount_target", 0) = -NNNN (ENOENT)

  umount2("umount2_target", MNT_FORCE);
  //staptest// umount ("umount2_target", MNT_FORCE) = -NNNN (ENOENT)

  umount2("umount2_target", MNT_DETACH);
  //staptest// umount ("umount2_target", MNT_DETACH) = -NNNN (ENOENT)

  umount2("umount2_target", MNT_EXPIRE);
  //staptest// umount ("umount2_target", MNT_EXPIRE) = -NNNN (ENOENT)

  return 0;
}
