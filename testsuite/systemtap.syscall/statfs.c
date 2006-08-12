/* COVERAGE: fstatfs statfs ustat statfs64 */
#include <sys/types.h>
#include <unistd.h>
#include <ustat.h>
#include <sys/vfs.h>

int main()
{
  
  ustat(42, (struct ustat *)0x12345678);
#if __WORDSIZE == 64
  // ustat (42, 0x0000000012345678) = 
#else
  // ustat (42, 0x12345678) = 
#endif

  statfs("abc", (struct statfs *)0x12345678);
#if __WORDSIZE == 64
  // statfs ("abc", 0x0000000012345678) =
#else
  // statfs ("abc", 0x12345678) =
#endif

  fstatfs(77, (struct statfs *)0x12345678);
#if __WORDSIZE == 64
  // fstatfs (77, 0x0000000012345678) =
#else
  // fstatfs (77, 0x12345678) =
#endif


  return 0;
}
