accept.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//accept ___________________________________________
void do_accept() {
   struct sockaddr sa;
   socklen_t upa;
   accept(45,&sa,&upa);
   dmsg("accept","s",45);
   dmsg("accept","addr_uaddr",(int)&sa);
   dmsg("accept","addrlen_uaddr",(int)&upa);
}

int main (void) {
   do_accept();
}
access.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//access ___________________________________________
void do_access() {
   char path[] = "accesstr";
   access(path,66);
   dmsg("access","pathname_uaddr",(int)&path);
   dmsg("access","mode",66);
}

int main (void) {
   do_access();
}
acct.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//acct _____________________________________________
void do_acct() {
   char fname[] = "goosfraba.txt";
   acct(fname);
   dmsg("acct","filename_uaddr",(int)&fname);
}

int main (void) {
   do_acct();
}
adjtimex.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/timex.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//adjtimex _________________________________________
void do_adjtimex() {
   struct timex tx;
   adjtimex(&tx);
   dmsg("adjtimex","buf_uaddr",(int)&tx);
}

int main (void) {
   do_adjtimex();
}
alarm.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//alarm ____________________________________________
void do_alarm() {
   alarm(0);
   dmsg("alarm","seconds",2);
}

int main (void) {
   do_alarm();
}
bdflush.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//bdflush __________________________________________
void do_bdflush() {
   bdflush(5,998);
   dmsg("bdflush","func",5);
   dmsg("bdflush","data",998);
}

int main (void) {
   do_bdflush();
}
bind.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//bind _____________________________________________
void do_bind() {
   struct sockaddr sa;
   bind(99,&sa,sizeof(sa));
   dmsg("bind","sockfd",99);
   dmsg("bind","my_addr_uaddr",(int)&sa);
   dmsg("bind","addrlen",sizeof(sa));
}

int main (void) {
   do_bind();
}
brk.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//brk ______________________________________________
void do_brk() { 
   void *end;
   brk(end);
   dmsg("brk","brk",(int)end);
}

int main (void) {
   do_brk();
}
capget.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#undef _POSIX_SOURCE
#include <sys/capability.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//capget ___________________________________________
void do_capget() {
   struct __user_cap_header_struct cuh;
   struct __user_cap_data_struct cud;
   capget(&cuh,&cud);
   dmsg("capget","header_uaddr",(int)&cuh);
   dmsg("capget","data_uaddr",(int)&cud);
}

int main (void) {
   do_capget();
}
capset.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#undef _POSIX_SOURCE
#include <sys/capability.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//capset ___________________________________________
void do_capset() {
   struct __user_cap_header_struct cuh;
   struct __user_cap_data_struct cud;
   capset(&cuh,&cud);
   dmsg("capset","header_uaddr",(int)&cuh);
   dmsg("capset","data_uaddr",(int)&cud);
}

int main (void) {
   do_capset();
}
chdir.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//chdir ____________________________________________
void do_chdir() {
   char path[] = "/home/does/not/exist";
   chdir(path);
   dmsg("chdir","path_uaddr",(int)&path);
}

int main (void) {
   do_chdir();
}
chmod.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//chmod ____________________________________________
void do_chmod() {
   char path[] = "/home/mouth";
   chmod(path,755);
   dmsg("chmod","path_uaddr",(int)&path);
   dmsg("chmod","mode",755);
}

int main (void) {
   do_chmod();
}
chown.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//chown ____________________________________________
void do_chown() {
   char path[] = "do_sys_chown";
   chown(path,91,19);
   dmsg("chown","path_uaddr",(int)&path);
   dmsg("chown","owner",91);
   dmsg("chown","group",19);
}

int main (void) {
   do_chown();
}
chroot.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//chroot ___________________________________________
void do_chroot() {
   char name[] = "paperbackbook";
   chroot(name);
   dmsg("chroot","path_uaddr",(int)&name);
}

int main (void) {
   do_chroot();
}
clock_getres.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//clock_getres _____________________________________
void do_clock_getres() {
   struct timespec ts;
   clockid_t clid = (clockid_t)44;
   clock_getres(clid,&ts);
   dmsg("clock_getres","clk_id",clid);
   dmsg("clock_getres","res_uaddr",(int)&ts);
}

int main (void) {
   do_clock_getres();
}
clock_gettime.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//clock_gettime ____________________________________
void do_clock_gettime() {
   struct timespec ts;
   clockid_t clid = (clockid_t)33;
   clock_gettime(clid,&ts);
   dmsg("clock_gettime","clk_id",clid);
   dmsg("clock_gettime","tp_uaddr",(int)&ts);
}

int main (void) {
   do_clock_gettime();
}
clock_nanosleep.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//clock_nanosleep __________________________________
void do_clock_nanosleep() {
   struct timespec ts1,ts2;
   clockid_t clid = (clockid_t)55;
   clock_nanosleep(clid,0,&ts1,&ts2);
   dmsg("clock_nanosleep","clock_id",clid);
   dmsg("clock_nanosleep","flags",0);
   dmsg("clock_nanosleep","rqtp_uaddr",(int)&ts1);
   dmsg("clock_nanosleep","rmtp_uaddr",(int)&ts2);
}

int main (void) {
   do_clock_nanosleep();
}
clock_settime.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//clock_settime ____________________________________
void do_clock_settime() {
   struct timespec ts;
   clockid_t clid = (clockid_t)21;
   clock_settime(clid,&ts);
   dmsg("clock_settime","clk_id",clid);
   dmsg("clock_settime","tp_uaddr",(int)&ts);
}

int main (void) {
   do_clock_settime();
}
close.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//close ____________________________________________
void do_close() {
   close(5);
   dmsg("close","fd",5);
}

int main (void) {
   do_close();
}
connect.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//connect __________________________________________
void do_connect() {
   struct sockaddr sa;
   connect(33,&sa,sizeof(sa));
   dmsg("connect","sockfd",99);
   dmsg("connect","serv_addr_uaddr",(int)&sa);
   dmsg("connect","addrlen",sizeof(sa));
}

int main (void) {
   do_connect();
}
creat.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//creat ____________________________________________
void do_creat() {
   char path[] = "/a7x/chap/four";
   creat(path,14);
   dmsg("creat","pathname_uaddr",(int)path);
   dmsg("creat","mode",14);
}

int main (void) {
   do_creat();
}
delete_module.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//delete_module ____________________________________
void do_delete_module() {
   char mod[] = "module";
   delete_module(mod,10);
   dmsg("delete_module","name_user_uaddr",(int)&mod);
   dmsg("delete_module","flags",10);
}

int main (void) {
   do_delete_module();
}
dup2.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//dup2 _____________________________________________
void do_dup2() {
   dup2(34,56);
   dmsg("dup2","oldfd",34);
   dmsg("dup2","newfd",56);
}

int main (void) {
   do_dup2();
}
dup.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//dup ______________________________________________
void do_dup() {
   dup(900);
   dmsg("dup","oldfd",900);
}

int main (void) {
   do_dup();
}
epoll_create.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/epoll.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//epoll_create _____________________________________
void do_epoll_create() {
   epoll_create(13);
   dmsg("epoll_create","size",13);
}

int main (void) {
   do_epoll_create();
}
epoll_ctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/epoll.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//epoll_ctl ________________________________________
void do_epoll_ctl() {
   struct epoll_event epe;
   epoll_ctl(71,48,9,&epe);
   dmsg("epoll_ctl","epfd",71);
   dmsg("epoll_ctl","op",48);
   dmsg("epoll_ctl","fd",9);
   dmsg("epoll_ctl","event_uaddr",(int)&epe);
}

int main (void) {
   do_epoll_ctl();
}
epoll_wait.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/epoll.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//epoll_wait _______________________________________
void do_epoll_wait() {
   struct epoll_event ev;
   epoll_wait(17,&ev,9,8);
   dmsg("epoll_wait","epfd",17);
   dmsg("epoll_wait","events_uaddr",(int)&ev);
   dmsg("epoll_wait","maxevents",9); 
   dmsg("epoll_wait","timeout",8);
}

int main (void) {
   do_epoll_wait();
}
exit.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//exit _____________________________________________
void do_exit() {
   dmsg("exit","status",4);
   exit(4);
}

int main (void) {
   do_exit();
}
exit_group.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//exit_group _______________________________________
void do_exit_group() {
   dmsg("exit_group","status",5);  
   syscall(SYS_exit_group,5);
}

int main (void) {
   do_exit_group();
}
fadvise64_64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fadvise64_64 _____________________________________
void do_fadvise64_64() {
   syscall(SYS_fadvise64_64,54,49,3,9);
   dmsg("fadvise64_64","fd",54);
   dmsg("fadvise64_64","offset",49);
   dmsg("fadvise64_64","len",3);
   dmsg("fadvise64_64","advise",9);
}

int main (void) {
   do_fadvise64_64();
}
fadvise64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fadvise64 ________________________________________
void do_fadvise64 () {
   syscall(SYS_fadvise64,54,49,3,9);
   dmsg("fadvise64","fd",54);
   dmsg("fadvise64","offset",49);
   dmsg("fadvise64","len",3);
   dmsg("fadvise64","advise",9);
}

int main (void) {
   do_fadvise64();
}
fchdir.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fchdir ___________________________________________
void do_fchdir() {
   fchdir(88);
   dmsg("fchdir","fd",88);
}

int main (void) {
   do_fchdir();
}
fchmod.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fchmod ___________________________________________
void do_fchmod() {
   fchmod(4,655);
   dmsg("fchmod","fildes",4);
   dmsg("fchmod","mode",655);
}

int main (void) {
   do_fchmod();
}
fchown.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fchown ___________________________________________
void do_fchown() {
   fchown(9,45,88);
   dmsg("lchown","fd",9);
   dmsg("lchown","owner",45);
   dmsg("lchown","group",88);
}

int main (void) {
   do_fchown();
}
fcntl64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fcntl64 __________________________________________
void do_fcntl64() {
   syscall(SYS_fcntl64,4,5,6);
   dmsg("fcntl64","fd",4);
   dmsg("fcntl64","cmd",5);
   dmsg("fcntl64","arg",6);
}

int main (void) {
   do_fcntl64();
}
fcntl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fcntl ____________________________________________
void do_fcntl() {
   fcntl(4,5,6); 
   dmsg("fcntl","fd",4);
   dmsg("fcntl","cmd",5);
   dmsg("fcntl","arg",6); 
}

int main (void) {
   do_fcntl();
}
fdatasync.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fdatasync ________________________________________
void do_fdatasync() {
   fdatasync(11);
   dmsg("fdatasync","fd",11);
}

int main (void) {
   do_fdatasync();
}
fgetxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fgetxattr ________________________________________
void do_fgetxattr() {
   char name[] = "allthatIgot";
   void *foo;
   fgetxattr(43,name,foo,61);
   dmsg("fgetxattr","fildes",43);
   dmsg("fgetxattr","path_uaddr",(int)&name);
   dmsg("fgetxattr","size",61);
}

int main (void) {
   do_fgetxattr();
}
flistxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//flistxattr _______________________________________
void do_flistxattr(){
   char path[] = "just/look";
   flistxattr(52,path,sizeof(path));
   dmsg("flistxattr","fildes",52);
   dmsg("flistxattr","list_uaddr",(int)&path);
   dmsg("flistxattr","size",sizeof(path));
}

int main (void) {
   do_flistxattr();
}
flock.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/file.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//flock ____________________________________________
void do_flock() {
   flock(6,55);
   dmsg("flock","fd",6);
   dmsg("flock","operation",55);
}

int main (void) {
   do_flock();
}
fremovexattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fremovexattr _____________________________________
void do_fremovexattr() {
   char name[] = "hardtosay";
   fremovexattr(93,name);
   dmsg("fremovexattr","fildes",93);
   dmsg("fremovexattr","name_uaddr",(int)&name);
}

int main (void) {
   do_fremovexattr();
}
fsetxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fsetxattr ________________________________________
void do_fsetxattr() {
   char name[] = "openmouth";
   void *foo;
   fsetxattr(34,name,foo,9,44);
   dmsg("fsetxattr","fildes",34);
   dmsg("fsetxattr","name_uaddr",(int)&name);
   dmsg("fsetxattr","size",9);
   dmsg("fsetxattr","flags",44);
}

int main (void) {
   do_fsetxattr();
}
fstat64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fstat64 __________________________________________
void do_fstat64() {
   struct stat s;
   fstat64(9,&s);
   dmsg("fstat64","fd",9);
   dmsg("fstat64","buf_uaddr",(int)&s);
}

int main (void) {
   do_fstat64();
}
fstat.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fstat ____________________________________________
void do_fstat() {
   struct stat s;
   fstat(2,&s);
   dmsg("fstat","fd",2);
   dmsg("fstat","buf_uaddr",(int)&s);
}

int main (void) {
   do_fstat();
}
fstatfs64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/vfs.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fstatfs64 ________________________________________
void do_fstatfs64() {
   struct statfs s;
   fstatfs64(64,8,&s);
   dmsg("fstatfs64","fd",64);
   dmsg("fstatfs64","sz",8);
   dmsg("fstatfs64","buf_uaddr",(int)&s);
}

int main (void) {
   do_fstatfs64();
}
fstatfs.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/vfs.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fstatfs __________________________________________
void do_fstatfs() {
   struct statfs s;
   fstatfs(54,&s);
   dmsg("fstatfs","fd",54);
   dmsg("fstatfs","s",(int)&s);
}

int main (void) {
   do_fstatfs();
}
fsync.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//fsync ____________________________________________
void do_fsync() {
   fsync(99);
   dmsg("fsync","fd",99);
}

int main (void) {
   do_fsync();
}
ftruncate64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ftruncate64 ______________________________________
void do_ftruncate64() {
   ftruncate64(79,54);
   dmsg("ftruncate64","fd",79);
   dmsg("ftruncate64","length",54);
}

int main (void) {
   do_ftruncate64();
}
ftruncate.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ftruncate ________________________________________
void do_ftruncate() {
   ftruncate(73,51);
   dmsg("ftruncate","fd",73); 
   dmsg("ftruncate","length",51); 
}

int main (void) {
   do_ftruncate();
}
futex.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//futex ____________________________________________
void do_futex() {
   int counter;
   struct timespec ts;
   syscall(SYS_futex,&counter,FUTEX_WAIT,76,&ts);
   dmsg("futex","futex_uaddr",(int)&counter);
   dmsg("futex","op",FUTEX_WAIT);
   dmsg("futex","val",76);
   dmsg("futex","timeout_uaddr",(int)&ts);
}

int main (void) {
   do_futex();
}
getcwd.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getcwd ___________________________________________
void do_getcwd() {
   char buf[100];
   getcwd(buf,sizeof(buf));
   dmsg("getcwd","buf_uaddr",(int)&buf);
   dmsg("getcwd","size",sizeof(buf));
}

int main (void) {
   do_getcwd();
}
getdents64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getdents64 _______________________________________
void do_getdents64() {
   struct dirent dirp;
   syscall(SYS_getdents64,71,&dirp,99);
   dmsg("getdents64","fd",71);
   dmsg("getdents64","dirp_uaddr",(int)&dirp);
   dmsg("getdents64","count",99);
}

int main (void) {
   do_getdents64();
}
getdents.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getdents _________________________________________
void do_getdents() {
   struct dirent dirp;
   syscall(SYS_getdents,71,&dirp,99); 
   dmsg("getdents","fd",71);
   dmsg("getdents","dirp_uaddr",(int)&dirp);
   dmsg("getdents","count",99);
}

int main (void) {
   do_getdents();
}
getegid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getegid __________________________________________
void do_getegid() {
   getegid();
   dmsg("getegid","void",0);
}

int main (void) {
   do_getegid();
}
geteuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//geteuid __________________________________________
void do_geteuid() {
   geteuid();
   dmsg("geteuid","void",0);
}

int main (void) {
   do_geteuid();
}
getgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getgid ___________________________________________
void do_getgid() {
   getgid();
   dmsg("getgid","void",0);
}

int main (void) {
   do_getgid();
}
getgroups.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getgroups ________________________________________
void do_getgroups() {
   gid_t list[3];
   getgroups(3,list);
   dmsg("getgroups","size",sizeof(list));
   dmsg("getgroups","list_uaddr",(int)list);
}

int main (void) {
   do_getgroups();
}
gethostname.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//gethostname ______________________________________
void do_gethostname() {
   char name[] = "myhostname";
   gethostname(name,sizeof(name));
   dmsg("gethostname","hostname_uaddr",(int)&name);
   dmsg("gethostname","len",sizeof(name));   
}

int main (void) {
   do_gethostname();
}
getitimer.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getitimer ________________________________________
void do_getitimer() {
   struct itimerval it;
   getitimer(ITIMER_VIRTUAL,&it);
   dmsg("getitimer","which",ITIMER_VIRTUAL);
   dmsg("getitimer","value_uaddr",(int)&it);
}

int main (void) {
   do_getitimer();
}
getpeername.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getpeername ______________________________________
void do_getpeername() {
   struct sockaddr sa;
   socklen_t slt;
   getpeername(34,&sa,&slt);
   dmsg("getpeername","s",34);
   dmsg("getpeername","name_uaddr",(int)&sa);
   dmsg("getpeername","namelen_uaddr",(int)&slt);
}

int main (void) {
   do_getpeername();
}
getpgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getpgid __________________________________________
void do_getpgid() {
   getpgid();
   dmsg("getpgid","void",0);
}

int main (void) {
   do_getpgid();
}
getpgrp.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getpgrp __________________________________________
void do_getpgrp() {
   getpgrp();
   dmsg("getpgrp","void",0);
}

int main (void) {
   do_getpgrp();
}
getpid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getpid ___________________________________________
void do_getpid() {
   getpid();
   dmsg("getpid","void",0);
}

int main (void) {
   do_getpid();
}
getppid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getppid __________________________________________
void do_getppid() {
   getppid();
   dmsg("getppid","void",0);
}

int main (void) {
   do_getppid();
}
getpriority.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getpriority ______________________________________
void do_getpriority() {
   getpriority(28,29);
   dmsg("getpriority","which",28);
   dmsg("getpriority","who",29);
}

int main (void) {
   do_getpriority();
}
getresgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#define _GNU_SOURCE
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getresgid ________________________________________
void do_getresgid() {
   unsigned int r,e,s;
   getresgid(&r,&e,&s);
   dmsg("getresgid","rgid_uaddr",(int)&r);
   dmsg("getresgid","egid_uaddr",(int)&e);
   dmsg("getresgid","sgid_uaddr",(int)&s);
}

int main (void) {
   do_getresgid();
}
getresuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#define _GNU_SOURCE
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getresuid ________________________________________
void do_getresuid() {
   unsigned int r,e,s;
   getresuid(&r,&e,&s);
   dmsg("getresuid","ruid_uaddr",(int)&r);
   dmsg("getresuid","euid_uaddr",(int)&e);
   dmsg("getresuid","suid_uaddr",(int)&s);
}

int main (void) {
   do_getresuid();
}
getrlimit.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getrlimit ________________________________________
void do_getrlimit() {
   struct rlimit rl;
   getrlimit(16,&rl);  
   dmsg("getrlimit","resource",16);
   dmsg("getrlimit","rlim_uaddr",(int)&rl); 
}

int main (void) {
   do_getrlimit();
}
getrusage.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getrusage ________________________________________
void do_getrusage() {
   struct rusage ru;
   getrusage(65,&ru);
   dmsg("getrusage","who",65);
   dmsg("getrusage","usage_uaddr",(int)&ru);
}

int main (void) {
   do_getrusage();
}
getsid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getsid ___________________________________________
void do_getsid() {
   getsid(2);
   dmsg("getsid","pid",2);
}

int main (void) {
   do_getsid();
}
getsockname.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getsockname ______________________________________
void do_getsockname() {
   struct sockaddr sa;
   socklen_t nl;
   getsockname(76,&sa,&nl); 
   dmsg("getsockname","s",76);
   dmsg("getsockname","name_uaddr",(int)&sa);
   dmsg("getsockname","namelen_uaddr",(int)&nl);
}

int main (void) {
   do_getsockname();
}
getsockopt.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getsockopt _______________________________________
void do_getsockopt() {
   void *optval;
   socklen_t len;
   getsockopt(5,6,7,optval,&len);
   dmsg("getsockopt","fd",4);
   dmsg("getsockopt","level",9);
   dmsg("getsockopt","optname",1);
   dmsg("getsockopt","optval",(int)&optval);
   dmsg("getsockopt","optlen",(int)&len);
}

int main (void) {
   do_getsockopt();
}
gettid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <linux/unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//gettid ___________________________________________
void do_gettid() {
   //gettid();
   dmsg("gettid","void",0);
}

int main (void) {
   do_gettid();
}
gettimeofday.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//gettimeofday _____________________________________
void do_gettimeofday() {
   struct timeval tv;
   struct timezone tz;
   gettimeofday(&tv,&tz);
   dmsg("gettimeofday","tv_uaddr",(int)&tv);
   dmsg("gettimeofday","tz_uaddr",(int)&tz);
}

int main (void) {
   do_gettimeofday();
}
getuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getuid ___________________________________________
void do_getuid() {
   getuid();
   dmsg("getuid","void",0);
}

int main (void) {
   do_getuid();
}
getxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//getxattr _________________________________________
void do_getxattr() {
   char path[] = "/open/up/your/eyes";
   char name[] = "consequence";
   void *foo;
   getxattr(path,name,foo,74);
   dmsg("getxattr","path_uaddr",(int)&path);
   dmsg("getxattr","name_uaddr",(int)&name);
   dmsg("getxattr","size",74);
}

int main (void) {
   do_getxattr();
}
init_module.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//init_module ______________________________________
void do_init_module() {
   void *func;
   char args[0];
   init_module(func,0,&args);
   dmsg("init_module","umod_uaddr",(int)&func);
   dmsg("init_module","len",0);
   dmsg("init_module","uargs_uaddr",(int)&args);
}

int main (void) {
   do_init_module();
}
ioctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/ioctl.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ioctl ____________________________________________
void do_ioctl() {
   ioctl(19,97,67);
   dmsg("ioctl","fd",19);
   dmsg("ioctl","request",97);
   dmsg("ioctl","argp",67);
}

int main (void) {
   do_ioctl();
}
ioperm.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/io.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ioperm ___________________________________________ 
void do_ioperm() {
   ioperm(7,3,7);
   dmsg("ioperm","from",7);
   dmsg("ioperm","num",3);
   dmsg("ioperm","turn_on",7);   
}

int main (void) {
   do_ioperm();
}
iopl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/io.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//iopl _____________________________________________
void do_iopl() {
   iopl(7);
   dmsg("iopl","level",7);   
}

int main (void) {
   do_iopl();
}
kill.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//kill _____________________________________________
void do_kill() {
   kill(4,6);
   dmsg("kill","pid",4);
   dmsg("kill","sig",6);
} 

int main (void) {
   do_kill();
}
lchown.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lchown ___________________________________________
void do_lchown() {
   char path[] = "do_sys_lchown";
   lchown(path,17,71);
   dmsg("lchown","path_uaddr",(int)&path);
   dmsg("lchown","owner",17);
   dmsg("lchown","group",71);
}

int main (void) {
   do_lchown();
}
lgetxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lgetxattr ________________________________________
void do_lgetxattr() {
   char path[] = "/do/deep";
   char name[] = "offgaurd";
   void *foo;
   lgetxattr(path,name,foo,22);
   dmsg("lgetxattr","path_uaddr",(int)&path);
   dmsg("lgetxattr","name_uaddr",(int)&name);
   dmsg("lgetxattr","size",22);
}

int main (void) {
   do_lgetxattr();
}
link.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//link _____________________________________________
void do_link() {
   char path[] = "/home/ajkd";
   char oldpath[] = "does/not/exist";
   link(path,oldpath);
   dmsg("link","oldpath_uaddr",(int)&path);
   dmsg("link","newpath_uaddr",(int)&oldpath);
}

int main (void) {
   do_link();
}
listen.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//listen ___________________________________________
void do_listen() {
   listen(5,9);
   dmsg("listen","s",5);
   dmsg("listen","backlog",9);
}

int main (void) {
   do_listen();
}
listxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//listxattr ________________________________________
void do_listxattr() {
   char path[] = "most/perfect/and/pure";
   char list[] = "almostfeelslikea";
   listxattr(path,list,sizeof(list));
   dmsg("listxattr","path_uaddr",(int)&path);
   dmsg("listxattr","list_uaddr",(int)&list);
   dmsg("listxattr","size",sizeof(list));
}

int main (void) {
   do_listxattr();
}
llistxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//llistxattr _______________________________________
void do_llistxattr() {
   char path[] = "is/it/worth/it";
   char list[] = "canyouevenhearme";
   llistxattr(path,list,sizeof(list));
   dmsg("llistxattr","path_uaddr",(int)&path);
   dmsg("llistxattr","list_uaddr",(int)&list);
   dmsg("llistxattr","size",sizeof(list));
}

int main (void) {
   do_llistxattr();
}
llseek.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//llseek ___________________________________________
void do_llseek() {
   loff_t lt;
   llseek(5,7,3,&lt,99);
   dmsg("llseek","fd",5);
   dmsg("llseek","offset_high",7);
   dmsg("llseek","offset_low",3);
   dmsg("llseek","result_uaddr",(int)&lt);
   dmsg("llseek","origin",99);
}

int main (void) {
   do_llseek();
}
lremovexattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lremovexattr _____________________________________
void do_lremovexattr() {
   char path[] = "/a/thousand/times";
   char name[] = "yearsgoneby";
   lremovexattr(path,name);
   dmsg("lremovexattr","path_uaddr",(int)&path);
   dmsg("lremovexattr","name_uaddr",(int)&name);
}

int main (void) {
   do_lremovexattr();
}
lseek.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lseek ____________________________________________
void do_lseek() {
   lseek(9,13,58);
   dmsg("lseek","fd",9);
   dmsg("lseek","offset",13);
   dmsg("lseek","whence",58);
}

int main (void) {
   do_lseek();
}
lsetxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lsetxattr ________________________________________
void do_lsetxattr() {
   char path[] = "/this/is/a/test";
   char name[] = "ofwillpower";
   void *foo;
   lsetxattr(path,name,foo,86,68);
   dmsg("lsetxattr","path_uaddr",(int)&path);
   dmsg("lsetxattr","name_uaddr",(int)&name);
   dmsg("lsetxattr","size",86);
   dmsg("lsetxattr","flags",68);
}

int main (void) {
   do_lsetxattr();
}
lstat64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lstat64 __________________________________________ 
void do_lstat64() {
   struct stat s;
   char name[] = "whiteknuckles";
   lstat64(name,&s);
   dmsg("lstat64","filename_uaddr",(int)&name);
   dmsg("lstat64","buf_uaddr",(int)&s);
}

int main (void) {
   do_lstat64();
}
lstat.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//lstat ____________________________________________
void do_lstat() {
   char filename[] = "trythisone";
   struct stat s;
   lstat(filename,&s);
   dmsg("lstat","file_name_uaddr",(int)&filename);
   dmsg("lstat","buf_uaddr",(int)&s);
}

int main (void) {
   do_lstat();
}
madvise.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//madvise __________________________________________
void do_madvise() {
   void *addr;
   madvise(addr,9,18);
   dmsg("madvise","start",(int)addr);
   dmsg("madvise","length",9);
   dmsg("madvise","advice",18);
}

int main (void) {
   do_madvise();
}
mincore.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mincore __________________________________________
void do_mincore() {
   void *addr;
   unsigned char vec;
   mincore(addr,61,&vec);
   dmsg("mincore","start",(int)addr);
   dmsg("mincore","length",61);
   dmsg("mincore","vec_uaddr",(int)&vec);
}

int main (void) {
   do_mincore();
}
mkdir.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mkdir ____________________________________________
void do_mkdir() {
   char path[] = "/crde";
   mkdir(path,755);
   dmsg("mkdir","pathname_uaddr",(int)&path);
   dmsg("mkdir","mode",755);
}

int main (void) {
   do_mkdir();
}
mknod.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mknod ____________________________________________
void do_mknod() {
   char name[] = "/home/test";
   mknod(name,5,4);
   dmsg("mknod","pathname_uaddr",(int)&name);
   dmsg("mknod","mode",5);
   dmsg("mknod","dev",4);
}

int main (void) {
   do_mknod();
}
mlockall.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mlockall _________________________________________
void do_mlockall() {
   mlockall(33);
   dmsg("mlockall","flags",33);
}

int main (void) {
   do_mlockall();
}
mlock.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mlock ____________________________________________
void do_mlock() {
   void *addr;
   mlock(addr,43);
   dmsg("mlock","addr",(int)addr);
   dmsg("mlock","len",43);
}

int main (void) {
   do_mlock();
}
mmap2.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mmap2 ____________________________________________
void do_mmap2() {
   syscall(SYS_mmap2,87,65,47,82,88,15);
   dmsg("mmap2","addr",87);
   dmsg("mmap2","len",65);
   dmsg("mmap2","prot",47);
   dmsg("mmap2","flags",82);
   dmsg("mmap2","fd",88);
   dmsg("mmap2","pgoff",15);   
}

int main (void) {
   do_mmap2();
}
modify_ldt.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
//#include <linux/ldt.h>
#include <linux/unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//modify_ldt _______________________________________
void do_modify_ldt() {
   void *ptr;
   modify_ldt(12,ptr,93);
   dmsg("modify_ldt","func",12);
   dmsg("modify_ldt","bytecount",93);
}

int main (void) {
   do_modify_ldt();
}
mount.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mount.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mount ____________________________________________
void do_mount() {
   char dev[] = "hda2";
   char dir[] = "/home/krstaffo";
   char type[] = "xfs";
   void *data;
   mount(dev,dir,type,23,data);
   dmsg("mount","source_uaddr",(int)&dev);
   dmsg("mount","target_uaddr",(int)&dir);
   dmsg("mount","filesystemtype_uaddr",(int)&type);
   dmsg("mount","mountflags",23);
}

int main (void) {
   do_mount();
}
mprotect.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mprotect _________________________________________
void do_mprotect() {
   void *mem;
   mprotect(mem,5,9);
   dmsg("mprotect","len",5);
   dmsg("mprotect","prot",9);
}

int main (void) {
   do_mprotect();
}
mq_getsetattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>
#include <mqueue.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mq_getsetattr ____________________________________
void do_mq_getsetattr() {
   struct mq_attr mqa,mqb;
   syscall(SYS_mq_getsetattr,19,&mqa,&mqb);
   dmsg("mq_getsetattr","mqdes",19);
   dmsg("mq_getsetattr","u_mqstat_uaddr",(int)&mqa);
   dmsg("mq_getsetattr","u_omqstat_uaddr",(int)&mqb);
}

int main (void) {
   do_mq_getsetattr();
}
mq_notify.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <mqueue.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mq_notify ________________________________________
void do_mq_notify() {
   struct sigevent se;
   syscall(SYS_mq_notify,19,&se);
   dmsg("mq_notify","mqdes",19);
   dmsg("mq_notify","notification_uaddr",(int)&se);
}

int main (void) {
   do_mq_notify();
}
mq_open.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <mqueue.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mq_open __________________________________________
void do_mq_open() {
   char name[] = "systemtap";
   struct mq_attr mqa;
   syscall(SYS_mq_open,name,88,900,&mqa);
   dmsg("mq_open","name_uaddr",(int)&name);
   dmsg("mq_open","oflag",88);
   dmsg("mq_open","mode",900);
   dmsg("mq_open","u_attr_uaddr",(int)&mqa);
}

int main (void) {
   do_mq_open();
}
mq_timedreceive.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <mqueue.h>
#include <time.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mq_timedreceive __________________________________
void do_mq_timedreceive() {
   char buf[3];
   unsigned int prio;
   struct timespec ts;
   syscall(SYS_mq_timedreceive,61,buf,sizeof(buf),&prio,&ts);
   dmsg("mq_timedreceive","mqdes",61);
   dmsg("mq_timedreceive","msg_ptr_uaddr",(int)&buf);
   dmsg("mq_timedreceive","msg_len",sizeof(buf));
   dmsg("mq_timedreceive","msg_prio_uaddr",(int)&prio);
   dmsg("mq_timedreceive","abs_timeout_uaddr",(int)&ts);
}

int main (void) {
   do_mq_timedreceive();
}
mq_timedsend.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <mqueue.h>
#include <time.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mq_timedsend _____________________________________
void do_mq_timedsend() {
   char buf[3];
   struct timespec ts;
   syscall(SYS_mq_timedsend,61,buf,sizeof(buf),7,&ts);
   dmsg("mq_timedsend","mqdes",61);
   dmsg("mq_timedsend","msg_ptr_uaddr",(int)&buf);
   dmsg("mq_timedsend","msg_len",sizeof(buf));
   dmsg("mq_timedsend","msg_prio",7);
   dmsg("mq_timedsend","abs_timeout_uaddr",(int)&ts);
}

int main (void) {
   do_mq_timedsend();
}
mq_unlink.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <mqueue.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mq_unlink ________________________________________
void do_mq_unlink() {
   char name[] = "systemtap";
   syscall(SYS_mq_unlink,name);
   dmsg("mq_unlink","u_name_uaddr",(int)&name);
}

int main (void) {
   do_mq_unlink();
}
mremap.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//mremap ___________________________________________
void do_mremap() {
   void *old;
   mremap(old,8,7,6);
   dmsg("mremap","old_size",8);
   dmsg("mremap","new_size",7);
   dmsg("mremap","flags",6);
}

int main (void) {
   do_mremap();
}
msgctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//msgctl ___________________________________________
void do_msgctl() {
   struct msqid_ds buf;
   msgctl(45,99,&buf);
   dmsg("msgctl","msqid",45);
   dmsg("msgctl","cmd",99);
   dmsg("msgctl","buf_uaddr",(int)&buf);
}

int main (void) {
   do_msgctl();
}
msgget.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//msgget ___________________________________________
void do_msgget() {
   msgget(4,90);
   dmsg("msgget","key",4);
   dmsg("msgget","msgflg",90);   
}

int main (void) {
   do_msgget();
}
msgrcv.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//msgrcv ___________________________________________
void do_msgrcv() {
   struct msgbasd {
      long mtype;
      char mtext[1];
   };
   struct msgbasd msgb;
   msgrcv(21,&msgb,39,16,90);
   dmsg("msgrcv","msqid",21);
   dmsg("msgrcv","msgp_uaddr",(int)&msgb);
   dmsg("msgrcv","msgsz",39);
   dmsg("msgrcv","msgtyp",16);
   dmsg("msgrcv","msgflg",90);
}

int main (void) {
   do_msgrcv();
}
msgsnd.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//msgsnd ___________________________________________
void do_msgsnd() {
   struct msgbasd {
      long mtype;     
      char mtext[1];
   };
   struct msgbasd msgb;
   msgsnd(19,&msgb,87,50);
   dmsg("msgsnd","msqid",19);
   dmsg("msgsnd","msgp_uaddr",(int)&msgb);
   dmsg("msgsnd","msgsz",87);
   dmsg("msgsnd","msgflg",50);
}

int main (void) {
   do_msgsnd();
}
msync.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//msync ____________________________________________
void do_msync() {
   void *addr;
   msync(addr,9,3);
   dmsg("msync","start",(int)addr);
   dmsg("msync","length",9);
   dmsg("msync","flags",3);
}

int main (void) {
   do_msync();
}
munlockall.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//munlockall _______________________________________
void do_munlockall() {
   munlockall();
   dmsg("munlockall","void",0);
}

int main (void) {
   do_munlockall();
}
munlock.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//munlock __________________________________________
void do_munlock() {
   void *addr;
   mlock(addr,43);
   dmsg("munlock","addr",(int)addr);
   dmsg("munlock","len",43);
}

int main (void) {
   do_munlock();
}
munmap.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//munmap ___________________________________________
void do_munmap() {
   void *addr;
   munmap(addr,66);
   dmsg("munmap","start",(int)addr);
   dmsg("munmap","length",66); 
}

int main (void) {
   do_munmap();
}
nanosleep.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//nanosleep ________________________________________
void do_nanosleep() {
   struct timespec rqtp, rmtp;
   rqtp.tv_sec = 1; 
   rqtp.tv_nsec = 1;
   nanosleep(&rqtp,&rmtp);
   dmsg("nanosleep","req_uaddr",(int)&rqtp);
   dmsg("nanosleep","rem_uaddr",(int)&rmtp);
}

int main (void) {
   do_nanosleep();
}
nice.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//nice _____________________________________________
void do_nice() {
   int res = nice(3);
   dmsg("nice","inc",3);
}

int main (void) {
   do_nice();
}
old_getrlimit.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//old_getrlimit ____________________________________
void do_old_getrlimit() {
   struct rlimit rl;
   getrlimit(14,&rl);
   dmsg("old_getrlimit","resource",14);
   dmsg("old_getrlimit","rlim_uaddr",(int)&rl);
}

int main (void) {
   do_old_getrlimit();
}
oldumount.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mount.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//oldumount ________________________________________
void do_oldumount() {
   char dev[] = "hda1";
   umount(dev);
   dmsg("oldumount","target_uaddr",(int)&dev);
}

int main (void) {
   do_oldumount();
}
open.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//open _____________________________________________
void do_open() {
   char path[] = "/a7x/try/again";
   open(path,73,145);
   dmsg("open","filename_uaddr",(int)&path);
   dmsg("open","flags",73);
   dmsg("open","mode",145);
}

int main (void) {
   do_open();
}
pause.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//pause ____________________________________________
void do_pause() {
   pause();
   dmsg("pause","void",0);
}

int main (void) {
   do_pause();
}
personality.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <linux/personality.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//personality ______________________________________
void do_personality() {
   personality(9);
   dmsg("personality","persona",9);
}

int main (void) {
   do_personality();
}
pipe.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//pipe _____________________________________________
void do_pipe() {
   int fildes[2];
   pipe(fildes);
   dmsg("pipe","fildes_uaddr",(int)&fildes);
}

int main (void) {
   do_pipe();
}
pivot_root.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//pivot_root _______________________________________
void do_pivot_root() {
   char new_root[] = "newroot";
   char put_old[] = "put_old";
   pivot_root(new_root,put_old);
   dmsg("pivot_root","new_root_uaddr",(int)&new_root);
   dmsg("pivot_root", "old_root_uaddr",(int)&put_old);   
}

int main (void) {
   do_pivot_root();
}
poll.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/poll.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//poll _____________________________________________
void do_poll() {
   struct pollfd pfd[4];
   poll(pfd,4,3);
   dmsg("poll","ufds_uaddr",(int)&pfd);
   dmsg("poll","nfds",16);
   dmsg("poll","timeout",3);
}

int main (void) {
   do_poll();
}
prctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/prctl.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//prctl ____________________________________________
void do_prctl() {
   prctl(7,6,5,4,3);
   dmsg("prctl","option",7);
   dmsg("prctl","arg2",6);
   dmsg("prctl","arg3",5);
   dmsg("prctl","arg4",4);
   dmsg("prctl","arg5",3);
}

int main (void) {
   do_prctl();
}
pread64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#define _XOPEN_SOURCE 500
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//pread64 __________________________________________
void do_pread64() {
   void *foo;
   pread64(7,foo,98,6);
   dmsg("pread64","fd",7);
   dmsg("pread64","buf_uaddr",(int)&foo);
   dmsg("pread64","count",98);
   dmsg("pread64","offset",6);
}

int main (void) {
   do_pread64();
}
ptrace.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/ptrace.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ptrace ___________________________________________
void do_ptrace() {
   void *addr,*data;
   ptrace(PTRACE_TRACEME,89,addr,data);
   dmsg("ptrace","request",PTRACE_TRACEME);
   dmsg("ptrace","pid",89);
   dmsg("ptrace","addr",(int)addr);
   dmsg("ptrace","data",(int)data);
}

int main (void) {
   do_ptrace();
}
pwrite64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#define _XOPEN_SOURCE 500
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//pwrite64 __________________________________________
void do_pwrite64() {
   void *foo;
   pwrite64(88,foo,64,1);
   dmsg("pwrite64","fd",88);
   dmsg("pwrite64","buf_uaddr",(int)&foo);
   dmsg("pwrite64","count",64);
   dmsg("pwrite64","offset",1);
}

int main (void) {
   do_pwrite64();
}
quotactl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <linux/quota.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//quotactl _________________________________________
void do_quotactl() {
   char special[] = "nullterminated";
   quotactl(9,special,7,(caddr_t)991);
   dmsg("quotactl","cmd",9);
   dmsg("quotactl","special_uaddr",(int)&special);
   dmsg("quotactl","id",7);
}

int main (void) {
   do_quotactl();
}
readahead.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//readahead ________________________________________
void do_readahead() {
   readahead(61,49,30);
   dmsg("readahead","fd",61);
   dmsg("readahead","offset",49);
   dmsg("readahead","count",30);
}

int main (void) {
   do_readahead();
}
read.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//read _____________________________________________
void do_read() {
   char buf[50];
   read(8,&buf,sizeof(buf));
   dmsg("read","fd",8);
   dmsg("read","buf_uaddr",(int)&buf);
   dmsg("read","count",sizeof(buf));
}

int main (void) {
   do_read();
}
readlink.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//readlink _________________________________________
void do_readlink() {
   char path[] = "/inte/lsa/mach";
   char buf[100];
   readlink(path,buf,sizeof(buf));
   dmsg("readlink","path_uaddr",(int)&path);
   dmsg("readlink","buf_uaddr",(int)&buf);
   dmsg("readlink","bufsiz",sizeof(buf));
}

int main (void) {
   do_readlink();
}
readv.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/uio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//readv ____________________________________________
void do_readv() {
   struct iovec iov;
   readv(5,&iov,4);
   dmsg("readv","fd",5);
   dmsg("readv","vector_uaddr",(int)&iov);
   dmsg("readv","count",4);
}

int main (void) {
   do_readv();
}
reboot.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <linux/reboot.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//reboot ___________________________________________
void do_reboot() {
   void *dummy;
   reboot(9,10,5,dummy);
   dmsg("reboot","magic",9);
   dmsg("reboot","magic2",10);
   dmsg("reboot","flag",5);
}

int main (void) {
   do_reboot();
}
recv.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//recv _____________________________________________
void do_recv() {
   void *buf;
   struct sockaddr sa;
   int siz = sizeof(sa);
   recv(63,buf,8,12);
   dmsg("recv","s",63);
   dmsg("recv","len",8);
   dmsg("recv","flags",12);
}

int main (void) {
   do_recv();
}
recvfrom.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//recvfrom _________________________________________
void do_recvfrom() {
   void *buf;
   struct sockaddr sa;
   socklen_t siz = sizeof(sa);
   recvfrom(63,buf,8,12,&sa,&siz);
   dmsg("recvfrom","s",8);
   dmsg("recvfrom","len",77);
   dmsg("recvfrom","flags",65);
   dmsg("recvfrom","from_uaddr",(int)&sa);
   dmsg("recvfrom","fromlen",(int)&siz);
}

int main (void) {
   do_recvfrom();
}
recvmsg.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//recvmsg __________________________________________
void do_recvmsg() {
   struct msghdr mh;
   recvmsg(49,&mh,9);
   dmsg("recvmsg","s",49);
   dmsg("recvmsg","msg_uaddr",(int)&mh);
   dmsg("recvmsg","flags",9);
}

int main (void) {
   do_recvmsg();
}
remap_file_pages.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mman.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//remap_file_pages _________________________________
void do_remap_file_pages() {
   void *old;
   remap_file_pages(old,97,96,95,94);
   dmsg("remap_file_pages","size",97);
   dmsg("remap_file_pages","prot",96);
   dmsg("remap_file_pages","pgoff",95);
   dmsg("remap_file_pages","flags",94);
}

int main (void) {
   do_remap_file_pages();
}
removexattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//removexattr ______________________________________
void do_removexattr() {
   char path[] = "/in/your/eyes";
   char name[] = "holdyourbreath";
   removexattr(path,name);
   dmsg("removexattr","path_uaddr",(int)&path);
   dmsg("removexattr","name_uaddr",(int)&name);
}

int main (void) {
   do_removexattr();
}
rename.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rename ___________________________________________
void do_rename() {
   char path[] = "/home/mouth";
   char oldpath[] = "does/not/exist";
   rename(path,oldpath);
   dmsg("rename","oldpath_uaddr",(int)&path);
   dmsg("rename","newpath_uaddr",(int)&oldpath);
}

int main (void) {
   do_rename();
}
restart_syscall.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//restart_syscall __________________________________
void do_restart_syscall() {
   syscall(SYS_restart_syscall);
   dmsg("restart_syscall","void",0);
}

int main (void) {
   do_restart_syscall();
}
rmdir.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rmdir ____________________________________________
void do_rmdir() {
   char dir[] = "/this/dir/is/imaginary";
   rmdir(dir);
   dmsg("rmdir","pathname_uaddr",(int)&dir);
}

int main (void) {
   do_rmdir();
}
rt_sigaction.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rt_sigaction _____________________________________
void do_rt_sigaction() {
   struct sigaction act, oct;
   void *restorer;
   syscall(SYS_rt_sigaction,81,&act,&oct,20,restorer);
   dmsg("rt_sigaction","sig",81);
   dmsg("rt_sigaction","act_uaddr",(int)&act);
   dmsg("rt_sigaction","oact_uaddr",(int)&oct);
   dmsg("rt_sigaction","sgsetsize",20);
}

int main (void) {
   do_rt_sigaction();
}
rt_sigpending.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rt_sigpending ____________________________________
void do_rt_sigpending() {
   sigset_t sst;
   syscall(SYS_rt_sigpending,&sst,0);
   dmsg("rt_sigpending","set_uaddr",(int)&sst);
   dmsg("rt_sigpending","sigsetsize",0);
}

int main (void) {
   do_rt_sigpending();
}
rt_sigprocmask.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rt_sigprocmask ___________________________________
void do_rt_sigprocmask() {
   sigset_t s, os;
   syscall(SYS_rt_sigprocmask,4,&s,&os,0);
   dmsg("rt_sigprocmask","how",4);
   dmsg("rt_sigprocmask","set_uaddr",(int)&s);
   dmsg("rt_sigprocmask","oset_uaddr",(int)&os);
   dmsg("rt_sigprocmask","sigsetsize",0);
}

int main (void) {
   do_rt_sigprocmask();
}
rt_sigqueueinfo.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rt_sigqueueinfo __________________________________
void do_rt_sigqueueinfo() {
   siginfo_t uinfo;   
   syscall(SYS_rt_sigqueueinfo,44,97,&uinfo);
   dmsg("rt_sigqueueinfo","pid",44);
   dmsg("rt_sigqueueinfo","sig",97);
   dmsg("rt_sigqueueinfo","uinfo_uaddr",(int)&uinfo); 
}

int main (void) {
   do_rt_sigqueueinfo();
}
rt_sigtimedwait.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//rt_sigtimedwait __________________________________
void do_rt_sigtimedwait() {
   sigset_t uthese,uinfo;
   struct timespec ts;
   syscall(SYS_rt_sigtimedwait,&uthese,&uinfo,&ts,0);
   dmsg("rt_sigtimedwait","uthese_uaddr",(int)&uthese);
   dmsg("rt_sigtimedwait","uinfo_uaddr",(int)&uinfo);
   dmsg("rt_sigtimedwait","uts_uaddr",(int)&ts);
   dmsg("rt_sigtimedwait","sigsetsize",0);
}

int main (void) {
   do_rt_sigtimedwait();
}
sched_getaffinity.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_getaffinity ________________________________
void do_sched_getaffinity() {
   unsigned long mask = 1;
   sched_getaffinity(getpid(),sizeof(mask),&mask);
   dmsg("sched_getaffinity","pid",55);
   dmsg("sched_getaffinity","len",sizeof(mask));
   dmsg("sched_getaffinity","mask_uaddr",(int)&mask);
}

int main (void) {
   do_sched_getaffinity();
}
sched_getparam.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_getparam ___________________________________
void do_sched_getparam() {
   struct sched_param sp;
   sched_getparam(35,&sp);
   dmsg("sched_getparam","pid",35);
   dmsg("sched_getparam","p_uaddr",(int)&sp);
}

int main (void) {
   do_sched_getparam();
}
sched_get_priority_max.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_get_priority_max ___________________________
void do_sched_get_priority_max() {
   sched_get_priority_max(65);
   dmsg("sched_get_priority_max","policy",65);
}

int main (void) {
   do_sched_get_priority_max();
}
sched_get_priority_min.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_get_priority_min ___________________________
void do_sched_get_priority_min() {
   sched_get_priority_min(75);
   dmsg("sched_get_priority_min","policy",75);
}

int main (void) {
   do_sched_get_priority_min();
}
sched_getscheduler.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_getscheduler _______________________________
void do_sched_getscheduler() {
   sched_getscheduler(25);
   dmsg("sched_getscheduler","pid",25);
}

int main (void) {
   do_sched_getscheduler();
}
sched_rr_get_interval.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_rr_get_interval ____________________________
void do_sched_rr_get_interval() {
   struct timespec tsa;
   sched_rr_get_interval(101,&tsa);
   dmsg("sched_rr_get_interval","pid",101);
   dmsg("sched_rr_get_interval","tp_uaddr",(int)&tsa);
}

int main (void) {
   do_sched_rr_get_interval();
}
sched_setaffinity.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_setaffinity ________________________________
void do_sched_setaffinity() {
   unsigned long mask = 1;
   sched_setaffinity(45,sizeof(mask),&mask);
   dmsg("sched_setaffinity","pid",45);
   dmsg("sched_setaffinity","len",sizeof(mask));
   dmsg("sched_setaffinity","mask_uaddr",(int)&mask);
}

int main (void) {
   do_sched_setaffinity();
}
sched_setparam.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_setparam ___________________________________
void do_sched_setparam() {
   struct sched_param sp;
   sched_getparam(99,&sp);
   dmsg("sched_setparam","pid",99);
   dmsg("sched_setparam","p_uaddr",(int)&sp);
}

int main (void) {
   do_sched_setparam();
}
sched_setscheduler.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_setscheduler _______________________________
void do_sched_setscheduler() {
   struct sched_param sp;
   sched_setscheduler(77,88,&sp);
   dmsg("sched_setscheduler","pid",77);
   dmsg("sched_setscheduler","policy",88);
   dmsg("sched_setscheduler","p_uaddr",(int)&sp);
}

int main (void) {
   do_sched_setscheduler();
}
sched_yield.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sched.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sched_yield ______________________________________
void do_sched_yield() {
   sched_yield();
   dmsg("sched_yield","policy",0);
}

int main (void) {
   do_sched_yield();
}
select.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//select ___________________________________________
void do_select() {
   fd_set fds1,fds2,fds3;
   struct timeval tv;
   select(6,&fds1,&fds2,&fds3,&tv);
   dmsg("select","n",6);
   dmsg("select","readfds_uaddr",(int)&fds1);
   dmsg("select","writefds_uaddr",(int)&fds2);
   dmsg("select","exceptfds_uaddr",(int)&fds3);
   dmsg("select","timeout_uaddr",(int)&tv);
}

int main (void) {
   do_select();
}
semctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//semctl ___________________________________________
void do_semctl() {
   semctl(19,73,24);
   dmsg("semctl","semid",19);
   dmsg("semctl","semnum",73);
   dmsg("semctl","cmd",24); 
} 

int main (void) {
   do_semctl();
}
semget.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//semget ___________________________________________
void do_semget() {
   semget(4,6,89);
   dmsg("semget","key",4);
   dmsg("semget","nsems",6);
   dmsg("semget","semflg",89); 
}

int main (void) {
   do_semget();
}
semop.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//semop ____________________________________________
void do_semop() {
   struct sembuf sops;
   semop(84,&sops,48);
   dmsg("semop","semid",84);
   dmsg("semop","tsops_uaddr",(int)&sops);
   dmsg("semop","nsops",48);
}

int main (void) {
   do_semop();
}
semtimedop.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//semtimedop _______________________________________
void do_semtimedop() {
   struct sembuf sops;
   struct timespec ts;
   semtimedop(44,&sops,8,&ts);
   dmsg("semtimedop","semid",44);
   dmsg("semtimedop","sops_uaddr",(int)&sops);
   dmsg("semtimedop","nsops",8);
   dmsg("semtimedop","timeout_uaddr",(int)&ts);
}

int main (void) {
   do_semtimedop();
}
send.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//send _____________________________________________
void do_send() {
   void *buf;
   send(8,buf,77,65);
   dmsg("send","s",8);
   dmsg("send","len",77);
   dmsg("send","flags",65);
}

int main (void) {
   do_send();
}
sendfile64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/sendfile.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sendfile64 _______________________________________
void do_sendfile64() {
   off_t offset;
   sendfile64(8,7,&offset,54);
   dmsg("sendfile","out_fd",8);
   dmsg("sendfile","in_fd",7);
   dmsg("sendfile","offset_uaddr",(int)&offset);
   dmsg("sendfile","count",54);
}

int main (void) {
   do_sendfile64();
}
sendfile.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/sendfile.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sendfile _________________________________________
void do_sendfile() {
   off_t offset;
   sendfile(4,6,&offset,99);
   dmsg("sendfile","out_fd",4);
   dmsg("sendfile","in_fd",6);
   dmsg("sendfile","offset_uaddr",(int)&offset);
   dmsg("sendfile","count",99);
}

int main (void) {
   do_sendfile();
}
sendmsg.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sendmsg __________________________________________
void do_sendmsg() {
   struct msghdr msg;
   sendmsg(5,&msg,99);
   dmsg("sendmsg","s",5);
   dmsg("sendmsg","msg_uaddr",(int)&msg);
   dmsg("sendmsg","flags",99);
}

int main (void) {
   do_sendmsg();
}
sendto.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sendto ___________________________________________
void do_sendto() {
   void *buf;
   struct sockaddr sa;
   sendto(8,buf,77,65,&sa,sizeof(sa));
   dmsg("sendto","s",8);
   dmsg("sendto","len",77);
   dmsg("sendto","flags",65);
   dmsg("sendto","to_uaddr",(int)&sa);
   dmsg("sendto","tolen",sizeof(sa));
}

int main (void) {
   do_sendto();
}
setdomainname.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setdomainname ______________________________________
void do_setdomainname() {
   char name[] = "myhostname";
   setdomainname(name,sizeof(name));
   dmsg("setdomainname","hostname_uaddr",(int)&name);
   dmsg("setdomainname","len",sizeof(name));
}

int main (void) {
   do_setdomainname();
}
setfsgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setfsgid _________________________________________
void do_setfsgid() {
   setfsgid(6);
   dmsg("setfsgid","fsgid",6);
}

int main (void) {
   do_setfsgid();
}
setfsuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setfsuid _________________________________________
void do_setfsuid() {
   setfsuid(5);
   dmsg("setfsuid","fsuid",5);
}

int main (void) {
   do_setfsuid();
}
setgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setgid ___________________________________________
void do_setgid() {
   setgid(3);
   dmsg("setgid","gid",3);
}

int main (void) {
   do_setgid();
}
setgroups.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setgroups ________________________________________
void do_setgroups() {
   gid_t list[2];
   setgroups(2,list);
   dmsg("setgroups","size",2);
   dmsg("setgroups","list_uaddr",(int)&list);
}

int main (void) {
   do_setgroups();
}
sethostname.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sethostname ______________________________________
void do_sethostname() {
   char name[] = "myhostname";
   sethostname(name,sizeof(name));
   dmsg("sethostname","hostname_uaddr",(int)&name);
   dmsg("sethostname","len",sizeof(name));
}

int main (void) {
   do_sethostname();
}
setitimer.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setitimer ________________________________________
void do_setitimer() {
   struct itimerval it1, it2;
   setitimer(ITIMER_PROF,&it1,&it2);
   dmsg("setitimer","which",ITIMER_PROF);
   dmsg("setitimer","value_uaddr",(int)&it1);
   dmsg("setitimer","ovalue_uaddr",(int)&it2);
}

int main (void) {
   do_setitimer();
}
setpgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setpgid __________________________________________
void do_setpgid() {
   setpgid(9,10);
   dmsg("setpgid","pid",1);
   dmsg("setpgid","pgid",10);
}

int main (void) {
   do_setpgid();
}
setpriority.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setpriority ______________________________________
void do_setpriority() {
   setpriority(25,26,27);
   dmsg("setpriority","which",25);
   dmsg("setpriority","who",26);
   dmsg("setpriority","niceval",27);
}

int main (void) {
   do_setpriority();
}
setregid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setregid _________________________________________
void do_setregid() {
   setregid(7,8);
   dmsg("setregid","rgid",7);
   dmsg("setregid","egid",8);
}

int main (void) {
   do_setregid();
}
setresgid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#define _GNU_SOURCE
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setresgid ________________________________________
void do_setresgid() {
   setresgid(14,15,16);
   dmsg("setresgid","rgid",14);
   dmsg("setresgid","egid",15);
   dmsg("setresgid","sgid",16);
}

int main (void) {
   do_setresgid();
}
setresuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#define _GNU_SOURCE
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setresuid ________________________________________
void do_setresuid() {
   setresuid(11,12,13);
   dmsg("setresuid","ruid",11);
   dmsg("setresuid","euid",12);
   dmsg("setresuid","suid",13);
}

int main (void) {
   do_setresuid();
}
setreuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setreuid _________________________________________
void do_setreuid() {
   setreuid(7,8);
   dmsg("setreuid","ruid",7);
   dmsg("setreuid","euid",8);
}

int main (void) {
   do_setreuid();
}
setrlimit.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setrlimit ________________________________________
void do_setrlimit() {
   struct rlimit rl;
   setrlimit(44,&rl);
   dmsg("setrlimit","resource",44);
   dmsg("setrlimit","rlim_uaddr",(int)&rl);
}

int main (void) {
   do_setrlimit();
}
setsid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setsid ___________________________________________
void do_setsid() {
   setsid();
   dmsg("setsid","void",0);
}

int main (void) {
   do_setsid();
}
setsockopt.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setsockopt _______________________________________
void do_setsockopt() {
   void *optval;
   socklen_t len = 5;
   setsockopt(4,9,1,optval,len);
   dmsg("setsockopt","fd",4);
   dmsg("setsockopt","level",9);
   dmsg("setsockopt","optname",1);
   dmsg("setsockopt","optval",(int)&optval);
   dmsg("setsockopt","optlen",len);
}

int main (void) {
   do_setsockopt();
}
set_tid_address.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//set_tid_address __________________________________
void do_set_tid_address() {
   int tidptr;
   syscall(SYS_set_tid_address,&tidptr);
   dmsg("set_tid_address","tidptr_uaddr",(int)&tidptr);
}

int main (void) {
   do_set_tid_address();
}
settimeofday.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//settimeofday _____________________________________
void do_settimeofday() {
   struct timeval tv;
   struct timezone tz;
   settimeofday(&tv,&tz);
   dmsg("settimeofday","tv_uaddr",(int)&tv);
   dmsg("settimeofday","tz_uaddr",(int)&tz);
}

int main (void) {
   do_settimeofday();
}
setuid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setuid ___________________________________________
void do_setuid() {
   setuid(4);
   dmsg("setuid","uid",4);
}

int main (void) {
   do_setuid();
}
setxattr.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <attr/xattr.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//setxattr _________________________________________
void do_setxattr() {
   char path[] = "/would/have/done/things";
   char name[] = "upagainstawall";
   void *foo;
   setxattr(path,name,foo,77,99);
   dmsg("setxattr","path_uaddr",(int)&path);
   dmsg("setxattr","name_uaddr",(int)&name);
   dmsg("setxattr","size",77);
   dmsg("setxattr","flags",99);
}

int main (void) {
   do_setxattr();
}
sgetmask.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <signal.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sgetmask _________________________________________
void do_sgetmask() {
   syscall(SYS_sgetmask);
   dmsg("sgetmask","void",0);
}

int main (void) {
   do_sgetmask();
}
shmat.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/shm.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//shmat ____________________________________________
void do_shmat() {
   //void *goo;
   shmat(16,NULL,19);
   dmsg("shmat","shmid",16);
   dmsg("shmat","shmflg",19);   
}

int main (void) {
   do_shmat();
}
shmctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//shmctl ___________________________________________
void do_shmctl() {
   struct shmid_ds buf;
   shmctl(2,6,&buf);
   dmsg("shmctl","shmid",2);
   dmsg("shmctl","cmd",6);
   dmsg("shmctl","buf_uaddr",(int)&buf);
}

int main (void) {
   do_shmctl();
}
shmdt.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/shm.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//shmdt ____________________________________________
void do_shmdt() {
   void *foo;
   shmdt(foo);   
}

int main (void) {
   do_shmdt();
}
shmget.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//shmget ___________________________________________
void do_shmget() {
   shmget(33,55,88);
   dmsg("shmat","key",33);
   dmsg("shmat","size",55);
   dmsg("shmat","shmflg",88);
}

int main (void) {
   do_shmget();
}
shutdown.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//shutdown _________________________________________
void do_shutdown() {
   shutdown(76,74);
   dmsg("shutdown","s",76);
   dmsg("shutdown","how",74);
}

int main (void) {
   do_shutdown();
}
signal.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//signal ___________________________________________
void do_signal() {
   void *foo;
   signal(4,SIG_IGN);
   dmsg("signal","sig",4); 
}

int main (void) {
   do_signal();
}
sigpending.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sigpending _______________________________________
void do_sigpending() {
   sigset_t s;
   sigpending(&s);
   dmsg("sigpending","set_uaddr",(int)&s);
}

int main (void) {
   do_sigpending();
}
sigprocmask.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <signal.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sigprocmask ______________________________________
void do_sigprocmask() {
   sigset_t s, o;
   sigprocmask(SIG_BLOCK|SIG_SETMASK,&s,&o);
   dmsg("sigprocmask","how",SIG_SETMASK);
   dmsg("sigprocmask","set_uaddr",(int)&s);
   dmsg("sigprocmask","oldset_uaddr",(int)&o);
}

int main (void) {
   do_sigprocmask();
}
socket.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//socket ___________________________________________
void do_socket() {
   socket(4,9,2);
   dmsg("socket","family",4);
   dmsg("socket","type",9);
   dmsg("socket","protocol",2);
}

int main (void) {
   do_socket();
}
socketcall.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//socketcall _______________________________________
void do_socketcall() {
   unsigned long how;
   syscall(SYS_socketcall,7,&how);
   dmsg("socketcall","call",7);
   dmsg("socketcall","args_uaddr",(int)&how);
}

int main (void) {
   do_socketcall();
}
socketpair.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//socketpair _______________________________________
void do_socketpair() {
   int usv;
   socketpair(9,6,8,&usv);
   dmsg("socketpair","family",9);
   dmsg("socketpair","type",6);
   dmsg("socketpair","protocol",8);
   dmsg("socketpair","sv_uaddr",(int)&usv);
}

int main (void) {
   do_socketpair();
}
ssetmask.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <signal.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ssetmask _________________________________________
void do_ssetmask() {
   syscall(SYS_ssetmask);
   dmsg("ssetmask","void",0);
}

int main (void) {
   do_ssetmask();
}
stat64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//stat64 ___________________________________________
void do_stat64() {   
   struct stat s;
   char name[] = "runningonempty";
   stat64(name,&s);
   dmsg("stat64","filename_uaddr",(int)&name);
   dmsg("stat64","buf_uaddr",(int)&s);
}

int main (void) {
   do_stat64();
}
stat.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//stat _____________________________________________
void do_stat() {
   char filename[] = "thelastamericanpatriot";
   struct stat s;
   stat(filename,&s);
   dmsg("stat","filename_uaddr",(int)&filename);
   dmsg("stat","buf_uaddr",(int)&s);
}

int main (void) {
   do_stat();
}
statfs64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/vfs.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//statfs64 _________________________________________
void do_statfs64() {
   char filename[] = "ridingonthisone";
   struct statfs s;
   statfs64(filename,88,&s);
   dmsg("statfs64","path_uaddr",(int)&filename);
   dmsg("statfs64","sz",88);
   dmsg("statfs64","buf_uaddr",(int)&s);
}

int main (void) {
   do_statfs64();
}
statfs.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/vfs.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//statfs ___________________________________________
void do_statfs() {
   char filename[] = "daylight";
   struct statfs s;
   statfs(filename,&s);
   dmsg("statfs","filename",(int)&filename);
   dmsg("statfs","buf_uaddr",(int)&s);
}

int main (void) {
   do_statfs();
}
stime.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//stime ____________________________________________
void do_stime() {
   time_t timet;
   stime(&timet);
   dmsg("stime","t_uaddr",(int)&timet);
}

int main (void) {
   do_stime();
}
swapoff.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <asm/page.h> 
#include <sys/swap.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//swapoff __________________________________________
void do_swapoff() {
   char path[] = "sys_swapoff";
   swapoff(path);
   dmsg("swapoff","path_uaddr",(int)&path);
}

int main (void) {
   do_swapoff();
}
swapon.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <asm/page.h> 
#include <sys/swap.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//swapon ___________________________________________
void do_swapon() {
   char path[] = "sys_swapon";
   swapon(path,99);
   dmsg("swapon","path_uaddr",(int)&path);
   dmsg("swapon","swapflags",99); 
}

int main (void) {
   do_swapon();
}
symlink.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//symlink __________________________________________
void do_symlink() {
   char path[] = "/home/arh";
   char oldpath[] = "does/not/exist";
   symlink(path,oldpath);
   dmsg("symlink","oldpath_uaddr",(int)&path);
   dmsg("symlink","newpath_uaddr",(int)&oldpath);
}

int main (void) {
   do_symlink();
}
sync.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sync _____________________________________________
void do_sync() {
   sync();
   dmsg("sync","void",0);
}

int main (void) {
   do_sync();
}
sysctl.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <linux/sysctl.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sysctl ___________________________________________
void do_sysctl() {
   struct __sysctl_args args;
   sysctl(&args);
   dmsg("sysctl","args_uaddr",(int)&args);
}

int main (void) {
   do_sysctl();
}
sysfs.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sysfs ____________________________________________
void do_sysfs() {
   char buf[25];
   syscall(SYS_sysfs,8,9,&buf);
   dmsg("sysfs","option",8);
   dmsg("sysfs","arg1",9);
   dmsg("sysfs","arg2",(int)&buf);
}

int main (void) {
   do_sysfs();
}
sysinfo.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/sysinfo.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//sysinfo __________________________________________
void do_sysinfo() {
   struct sysinfo si;
   sysinfo(&si);  
   dmsg("sysinfo","info_uaddr",(int)&si); 
}

int main (void) {
   do_sysinfo();
}
syslog.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/klog.h>
#include <unistd.h>
#include <linux/unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//syslog ___________________________________________
void do_syslog() {
   char buf[24];
   syslog(5,&buf,99);
   dmsg("syslog","type",5);
   dmsg("syslog","bufp_uaddr",(int)&buf);
   dmsg("syslog","len",99);
}

int main (void) {
   do_syslog();
}
tgkill.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <syscall.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//tgkill ___________________________________________
void do_tgkill() {
  syscall(SYS_tgkill,55,49,7);
  dmsg("tgkill","tgid",55);
  dmsg("tgkill","pid",49);
  dmsg("tgkill","sig",7);
}

int main (void) {
   do_tgkill();
}
time.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//time _____________________________________________
void do_time() {
   time_t timet;
   time(&timet);
   dmsg("time","t_uaddr",(int)&timet);
}

int main (void) {
   do_time();
}
timer_create.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <signal.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//timer_create _____________________________________
void do_timer_create() {
   timer_t created;
   timer_create(CLOCK_REALTIME,NULL,&created);
   dmsg("timer_create","clockid",CLOCK_REALTIME);
   dmsg("timer_create","timerid_uaddr",(int)&created);
}

int main (void) {
   do_timer_create();
}
timer_delete.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//timer_delete _____________________________________
void do_timer_delete() {
   timer_t created;
   timer_create(CLOCK_REALTIME,NULL,&created);
   timer_delete(created);
   dmsg("timer_delete","timerid",(int)created);
}

int main (void) {
   do_timer_delete();
}
timer_getoverrun.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//timer_getoverrun _________________________________
void do_timer_getoverrun() { 
   timer_t created;
   timer_create(CLOCK_REALTIME,NULL,&created);
   timer_getoverrun(created);
   dmsg("timer_getoverrun","timerid",(int)created);
}

int main (void) {
   do_timer_getoverrun();
}
timer_gettime.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//timer_gettime ____________________________________
void do_timer_gettime() {
   timer_t created;
   struct itimerspec its;
   timer_create(CLOCK_REALTIME,NULL,&created);
   timer_gettime(created,&its);
   dmsg("timer_gettime","timerid",(int)created);
   dmsg("timer_gettime","value_uaddr",(int)&its);
}

int main (void) {
   do_timer_gettime();
}
timer_settime.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <time.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//timer_settime ____________________________________
void do_timer_settime() {
   timer_t created;
   struct itimerspec its,bits;
   timer_create(CLOCK_REALTIME,NULL,&created);
   timer_settime(created,0,&its,&bits);
   dmsg("timer_settime","timerid",(int)created);
   dmsg("timer_settime","flags",0);
   dmsg("timer_settime","value_uaddr",(int)&its);
   dmsg("timer_settime","ovalue_uaddr",(int)&bits);
}

int main (void) {
   do_timer_settime();
}
times.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/times.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//times ____________________________________________
void do_times() {
   struct tms tms;
   times(&tms);
   dmsg("times","buf_uaddr",(int)&tms);
}

int main (void) {
   do_times();
}
tkill.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <linux/unistd.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//tkill ____________________________________________
void do_tkill() {
   syscall(SYS_tkill,44,4);
   dmsg("tkill","pid",44);
   dmsg("tkill","sig",4);
}

int main (void) {
   do_tkill();
}
truncate64.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//truncate64 _______________________________________
void do_truncate64() {
   char path[] = "/only/chasing/safety";
   truncate64(path,22);
   dmsg("truncate64","path_uaddr",(int)&path);
   dmsg("truncate64","length",22);   
}

int main (void) {
   do_truncate64();
}
truncate.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//truncate _________________________________________
void do_truncate() {
   char path[] = "/antarctica/found/in/the/flood";
   truncate(path,9);
   dmsg("truncate","path_uaddr",(int)&path);
   dmsg("truncate","length",9);
}

int main (void) {
   do_truncate();
}
umask.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//umask ____________________________________________
void do_umask() {
   umask(88);
   dmsg("umask","mask",88);   
}

int main (void) {
   do_umask();
}
umount.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/mount.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//umount ___________________________________________
void do_umount() {
   char dev[] = "hda1";
   umount(dev); 
   dmsg("umount","target_uaddr",(int)&dev);
}

int main (void) {
   do_umount();
}
unlink.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//unlink ___________________________________________
void do_unlink() {
   char path[] = "/home/krstaffo/foofile.htm";
   unlink(path);
   dmsg("unlink","pathname_uaddr",(int)&path);
}

int main (void) {
   do_unlink();
}
uselib.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//uselib ___________________________________________
void do_uselib() {
   char lib[] = "library";
   uselib(lib);
   dmsg("uselib","library_uaddr",(int)&lib);
}

int main (void) {
   do_uselib();
}
ustat.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <ustat.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//ustat ____________________________________________
void do_ustat() { 
   struct ustat s;
   ustat(3,&s);
   dmsg("ustat","dev",3);
   dmsg("ustat","ubuf_uaddr",(int)&s);
}

int main (void) {
   do_ustat();
}
utime.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <utime.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//utime ____________________________________________
void do_utime() {
   struct utimbuf utb;
   char path[] = "sys_utime";
   utime(path,&utb);
   dmsg("utime","filename_uaddr",(int)&path);
   dmsg("utime","buf_uaddr",(int)&utb);
}

int main (void) {
   do_utime();
}
utimes.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <utime.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//utimes ___________________________________________
void do_utimes() {
   char path[] = "sys_utimes";
   struct timeval tvp;
   utimes(path,&tvp);
   dmsg("utimes","filename_uaddr",(int)&tvp);
   dmsg("utimes","tvp_uaddr",(int)&tvp);
}

int main (void) {
   do_utimes();
}
vhangup.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//vhangup __________________________________________
void do_vhangup() {
   vhangup();
   dmsg("vhangup","void",0);
}

int main (void) {
   do_vhangup();
}
wait4.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//wait4 ____________________________________________
void do_wait4() { 
   int staddr;
   struct rusage ru;
   wait4(1,&staddr,9,&ru);
   dmsg("wait4","pid",1);
   dmsg("wait4","status_uaddr",(int)&staddr);
   dmsg("wait4","options",9);
   dmsg("wait4","rusage_uaddr",(int)&ru);
}

int main (void) {
   do_wait4();
}
waitid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/wait.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//waitid ___________________________________________
void do_waitid() {
   struct siginfo si;
   struct rusage ru;
   waitid(4,5,&si,7);
   dmsg("waitid","which",4);
   dmsg("waitid","pid",5);
   dmsg("waitid","infop_uaddr",(int)&si); 
   dmsg("waitid","options",7);
}

int main (void) {
   do_waitid();
}
waitpid.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//waitpid __________________________________________
void do_waitpid() {
   int staddr;
   waitpid(4,&staddr,6);
   dmsg("waitpid","pid",4);
   dmsg("waitpid","infop_uaddr",(int)&staddr);
   dmsg("waitpid","options",6);
}

int main (void) {
   do_waitpid();
}
write.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//write ____________________________________________
void do_write() {
   char buf[33];
   write(6,&buf,sizeof(buf));
   dmsg("write","fd",6);
   dmsg("write","buf_uaddr",(int)&buf);
   dmsg("write","count",sizeof(buf));
}

int main (void) {
   do_write();
}
writev.c ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <stdio.h>
#include <sys/uio.h>

void dmsg(char *func,char *var,int val) {
   printf("%s: %s = %d\n",func,var,val);
}

//writev ____________________________________________
void do_writev() {
   struct iovec iov;
   writev(6,&iov,9);
   dmsg("writev","fd",6);
   dmsg("writev","vector_uaddr",(int)&iov);
   dmsg("writev","count",9);
}

int main (void) {
   do_writev();
}
