#ifndef _LKET_B2A_H
#define _LKET_B2A_H
#include <glib.h>

#define LKET_MAGIC	0xAEFCDB6B

#define MAX_STRINGLEN	256

#define APPNAMELIST_LEN	256

#define SEQID_SIZE 4

#define EVT_SYS 1
#define EVT_USER 2

#define MAX_GRPID  255
#define MAX_HOOKID  255
#define MAX_EVT_TYPES 2

#define DEFAULT_OUTFILE_NAME "lket.out"


/* Group ID Definitions */
int _GROUP_REGEVT = 1;
int _GROUP_SYSCALL = 2;
int _GROUP_PROCESS = 3;
int _GROUP_IOSCHED = 4;
int _GROUP_TASK = 5;
int _GROUP_SCSI = 6;
int _GROUP_PAGEFAULT = 7;
int _GROUP_NETDEV = 8;
int _GROUP_IOSYSCALL = 9;
int _GROUP_AIO = 10;
int _GROUP_CPUFREQ = 15;

/* hookIDs defined inside each group */
int _HOOKID_REGSYSEVT = 1;
int _HOOKID_REGUSREVT = 2;

int _HOOKID_SYSCALL_ENTRY = 1;
int _HOOKID_SYSCALL_RETURN = 2;

int _HOOKID_PROCESS_SNAPSHOT = 1;
int _HOOKID_PROCESS_EXECVE = 2;
int _HOOKID_PROCESS_FORK = 3;

int _HOOKID_IOSCHED_NEXT_REQ = 1;
int _HOOKID_IOSCHED_ADD_REQ = 2;
int _HOOKID_IOSCHED_REMOVE_REQ = 3;

int _HOOKID_TASK_CTXSWITCH = 1;
int _HOOKID_TASK_CPUIDLE = 2;

int _HOOKID_SCSI_IOENTRY = 1;
int _HOOKID_SCSI_IO_TO_LLD = 2;
int _HOOKID_SCSI_IODONE_BY_LLD = 3;
int _HOOKID_SCSI_IOCOMP_BY_MIDLEVEL = 4;

int _HOOKID_PAGEFAULT = 1;

int _HOOKID_NETDEV_RECEIVE = 1;
int _HOOKID_NETDEV_TRANSMIT = 2;

int _HOOKID_IOSYSCALL_OPEN_ENTRY = 1;
int _HOOKID_IOSYSCALL_OPEN_RETURN = 2;

int _HOOKID_IOSYSCALL_CLOSE_ENTRY = 3;
int _HOOKID_IOSYSCALL_CLOSE_RETURN = 4;

int _HOOKID_IOSYSCALL_READ_ENTRY = 5;
int _HOOKID_IOSYSCALL_READ_RETURN = 6;

int _HOOKID_IOSYSCALL_WRITE_ENTRY = 7;
int _HOOKID_IOSYSCALL_WRITE_RETURN = 8;

int _HOOKID_IOSYSCALL_READV_ENTRY = 9;
int _HOOKID_IOSYSCALL_READV_RETURN = 10;

int _HOOKID_IOSYSCALL_WRITEV_ENTRY = 11;
int _HOOKID_IOSYSCALL_WRITEV_RETURN = 12;

int _HOOKID_IOSYSCALL_PREAD64_ENTRY = 13;
int _HOOKID_IOSYSCALL_PREAD64_RETURN = 14;

int _HOOKID_IOSYSCALL_PWRITE64_ENTRY = 15;
int _HOOKID_IOSYSCALL_PWRITE64_RETURN = 16;

int _HOOKID_IOSYSCALL_READAHEAD_ENTRY = 17;
int _HOOKID_IOSYSCALL_READAHEAD_RETURN = 18;

int _HOOKID_IOSYSCALL_SENDFILE_ENTRY = 19;
int _HOOKID_IOSYSCALL_SENDFILE_RETURN = 20;

int _HOOKID_IOSYSCALL_LSEEK_ENTRY = 21;
int _HOOKID_IOSYSCALL_LSEEK_RETURN = 22;

int _HOOKID_IOSYSCALL_LLSEEK_ENTRY = 23;
int _HOOKID_IOSYSCALL_LLSEEK_RETURN = 24;

int _HOOKID_IOSYSCALL_SYNC_ENTRY = 25;
int _HOOKID_IOSYSCALL_SYNC_RETURN = 26;

int _HOOKID_IOSYSCALL_FSYNC_ENTRY = 27;
int _HOOKID_IOSYSCALL_FSYNC_RETURN = 28;

int _HOOKID_IOSYSCALL_FDATASYNC_ENTRY = 29;
int _HOOKID_IOSYSCALL_FDATASYNC_RETURN = 30;

int _HOOKID_IOSYSCALL_FLOCK_ENTRY = 31;
int _HOOKID_IOSYSCALL_FLOCK_RETURN = 32;

int _HOOKID_AIO_IO_SETUP_ENTRY = 1;
int _HOOKID_AIO_IO_SETUP_RETURN = 2;
int _HOOKID_AIO_IO_SUBMIT_ENTRY = 3;
int _HOOKID_AIO_IO_SUBMIT_RETURN = 4;
int _HOOKID_AIO_IO_SUBMIT_ONE_ENTRY = 5;
int _HOOKID_AIO_IO_SUBMIT_ONE_RETURN = 6;
int _HOOKID_AIO_IO_GETEVENTS_ENTRY = 7;
int _HOOKID_AIO_IO_GETEVENTS_RETURN = 8;
int _HOOKID_AIO_IO_DESTROY_ENTRY = 9;
int _HOOKID_AIO_IO_DESTROY_RETURN = 10;
int _HOOKID_AIO_IO_CANCEL_ENTRY = 11;
int _HOOKID_AIO_IO_CANCEL_RETURN = 12;

int _HOOKID_INIT_CPUFREQ = 1;
int _HOOKID_SWITCH_CPUFREQ = 2;

typedef struct _lket_pkt_header {
	int16_t	total_size;
	int16_t	sys_size;
	int64_t microsecond;
	/* aggr is the bit-OP of:
	(int64_t)current->pid << 32 |
	(int32_t)GroupID << 24 | (int32_t)hookID << 16 |
	(int16_t)current->thread_info->cpu << 8
	*/
	int64_t aggr;
} __attribute__((packed)) lket_pkt_header;

#define HDR_PID(ptr)  (int32_t)(((ptr)->aggr)>>32)
#define HDR_GroupID(ptr)  (int8_t)(((ptr)->aggr)>>24) 
#define HDR_HookID(ptr)  (int8_t)(((ptr)->aggr)>>16) 
#define HDR_CpuID(ptr)  (int8_t)(((ptr)->aggr)>>8) 

typedef struct _appname_info {
	int pid;
	int ppid;
	int tid;
	long index;
	struct _appname_info *next;
} appname_info;

typedef struct {
	char evt_fmt[256][7]; /* e.g. INT8,STRING,INT16,... */
	char evt_names[256][64]; /* e.g. protocal,dev_name,buff_len,... */
	char fmt[256];  /* e.g. %1b,%0s,%2b,... */
	int count;
} event_desc;

/* 
 * search the lket_init_header structure in a set of input files 
 */
static void find_init_header(FILE **fp, const int total_infiles);

/* 
 * read the lket_pkt_header structure at the begining of the input file 
 */
static int get_pkt_header(FILE *fp, lket_pkt_header *phdr);

/* 
 * print the lket_pkt_header structure into the output file
 */
static void print_pkt_header(FILE *fp, lket_pkt_header *phdr);

void register_appname(int i, FILE *fp, lket_pkt_header *phdr);
gint compareFunc(gconstpointer a, gconstpointer b, gpointer user_data);
void destroyAppName(gpointer data);

void register_events(int evt_type, FILE *infp, size_t size);
int dump_data(lket_pkt_header header, FILE *infp);
char *get_fmtstr(char *fmt);
#endif
