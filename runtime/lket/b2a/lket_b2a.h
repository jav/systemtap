#ifndef _LKET_B2A_H
#define _LKET_B2A_H

#include <glib.h>

#ifdef HAS_MYSQL
#include "/usr/include/mysql/mysql.h"
#endif

#define LKET_MAGIC	0xAEFCDB6B

#define MAX_STRINGLEN	256

#define MAX_GRPID  255
#define MAX_HOOKID  255
#define MAX_EVT_TYPES 2

#define DEFAULT_OUTFILE_NAME "lket.out"

/* Group ID Definitions */
int _GROUP_REGEVT = 1;
int _GROUP_PROCESS = 3;
int _GROUP_CPUFREQ = 15;

/* hookIDs defined inside each group */
int _HOOKID_REGSYSEVT = 1;
int _HOOKID_REGUSREVT = 3;
int _HOOKID_REGEVTDESC = 5;

int _HOOKID_PROCESS_SNAPSHOT = 1;
int _HOOKID_PROCESS_EXECVE = 3;
int _HOOKID_PROCESS_FORK = 5;

//int _HOOKID_INIT_CPUFREQ = 1;
int _HOOKID_SWITCH_CPUFREQ = 1;

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

#define MAX_FIELDS  32 /* max fields in a record */
#define MAX_FIELDNAME_LEN 16 /* max len of a field */

typedef struct {
#ifdef HAS_MYSQL
	GTree *entrytime;
#endif
	char evt_fmt[MAX_FIELDS][7]; /* e.g. INT8,STRING,INT16,... */
	char evt_names[MAX_FIELDS][MAX_FIELDNAME_LEN]; /* e.g. protocal,dev_name,buff_len,... */
	char fmt[256];  /* e.g. %1b,%0s,%2b,... */
	int count; /* # of fields */
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
static void print_pkt_header(lket_pkt_header *phdr);

void register_appname(int i, FILE *fp, lket_pkt_header *phdr);
gint compareFunc(gconstpointer a, gconstpointer b, gpointer user_data);
void destroyTreeData(gpointer data);
void register_evt_desc(FILE *infp, size_t size);
void register_events(int evt_type, FILE *infp, size_t size);
int dump_data(lket_pkt_header header, FILE *infp);
char *get_fmtstr(char *fmt);

#endif
