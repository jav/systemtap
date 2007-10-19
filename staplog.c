/*
 crash shared object for retrieving systemtap buffer
 Copyright (c) 2007 Hitachi,Ltd.,
 Created by Satoru Moriya <satoru.moriya.br@hitachi.com>
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


/* crash/defs.h defines NR_CPUS based upon architecture macros
   X86, X86_64, etc.  See crash/configure.c (!). */
#ifdef __alpha__
#define ALPHA
#endif
#ifdef __i386__
#define X86
#endif
#ifdef __powerpc__
#define PPC
#endif
#ifdef __ia64__
#define IA64
#endif
#ifdef __s390__
#define S390
#endif
#ifdef __s390x__
#define S390X
#endif
#ifdef __powerpc64__
#define PPC64
#endif
#ifdef __x86_64__
#define X86_64
#endif

#include <crash/defs.h>

struct rchan_offsets {
	long	subbuf_size;
	long	n_subbufs;
	long	buf;
	long	buf_start;
	long	buf_offset;
	long	buf_subbufs_produced;
	long	buf_padding;
};

struct fake_rchan_buf {
	void	*start;
	size_t	offset;
	size_t	subbufs_produced;
	size_t	*padding;
};

struct fake_rchan {
	size_t	subbuf_size;
	size_t	n_subbufs;
};

struct per_cpu_data {
	struct fake_rchan_buf	buf;
};

static struct rchan_offsets rchan_offsets;
static struct fake_rchan chan;
static struct per_cpu_data per_cpu[NR_CPUS];
static FILE *outfp;
static char *subbuf;
static int is_global;
static int is_broken;
static int old_format;
static int retrieve_all;

void cmd_staplog(void);
void cmd_staplog_cleanup(void);
char *help_staplog[];
char *help_staplog_cleanup[];

static struct command_table_entry command_table[] = {
	{"staplog", cmd_staplog, help_staplog, 0},
	{"staplog_cleanup", cmd_staplog_cleanup, help_staplog_cleanup, CLEANUP},
	{NULL, NULL, NULL, 0},
};

static void get_rchan_offsets(void)
{
	rchan_offsets.subbuf_size = MEMBER_OFFSET("rchan", "subbuf_size");
	if (rchan_offsets.subbuf_size < 0)
		goto ERR;
	rchan_offsets.n_subbufs = MEMBER_OFFSET("rchan", "n_subbufs");
	if (rchan_offsets.n_subbufs < 0)
		goto ERR;
	rchan_offsets.buf = MEMBER_OFFSET("rchan", "buf");
	if (rchan_offsets.buf < 0)
		goto ERR;
	rchan_offsets.buf_start = MEMBER_OFFSET("rchan_buf", "start");
	if (rchan_offsets.buf_start < 0)
		goto ERR;
	rchan_offsets.buf_offset = MEMBER_OFFSET("rchan_buf", "offset");
	if (rchan_offsets.buf_offset < 0)
		goto ERR;
	rchan_offsets.buf_subbufs_produced
		= MEMBER_OFFSET("rchan_buf", "subbufs_produced");
	if (rchan_offsets.buf_subbufs_produced < 0)
		goto ERR;
	rchan_offsets.buf_padding = MEMBER_OFFSET("rchan_buf", "padding");
	if (rchan_offsets.buf_padding < 0)
		goto ERR;
	return;
ERR:
	error(FATAL, "cannot get rchan offset\n");
}

static ulong get_rchan(ulong chan_addr) 
{
	ulong rchan;

	readmem(chan_addr, KVADDR, &rchan, sizeof(void*),
		"stp_channel", FAULT_ON_ERROR);
	readmem(rchan + rchan_offsets.subbuf_size,
		KVADDR, &chan.subbuf_size, sizeof(size_t),
		"stp_channel.subbuf_size", FAULT_ON_ERROR);
	readmem(rchan + rchan_offsets.n_subbufs,
		KVADDR, &chan.n_subbufs, sizeof(size_t),
		"stp_channel.n_subbufs", FAULT_ON_ERROR);
	return rchan;
}

static void get_rchan_buf(int cpu, ulong rchan) 
{
	ulong rchan_buf;
	struct per_cpu_data *pcd;

	pcd = &per_cpu[cpu];
	readmem(rchan + rchan_offsets.buf + sizeof(void*) * cpu,
		KVADDR, &rchan_buf, sizeof(void*),
		"stp_channel.buf", FAULT_ON_ERROR);
	readmem(rchan_buf + rchan_offsets.buf_start,
		KVADDR, &pcd->buf.start, sizeof(void*),
		"stp_channel.buf.start", FAULT_ON_ERROR);
	readmem(rchan_buf + rchan_offsets.buf_offset,
		KVADDR, &pcd->buf.offset, sizeof(size_t),
		"stp_channel.buf.offset", FAULT_ON_ERROR);
	readmem(rchan_buf + rchan_offsets.buf_subbufs_produced,
		KVADDR, &pcd->buf.subbufs_produced, sizeof(size_t),
		"stp_channel.buf.subbufs_produced", FAULT_ON_ERROR);
	readmem(rchan_buf + rchan_offsets.buf_padding,
		KVADDR, &pcd->buf.padding, sizeof(size_t*),
		"stp_channel.buf.padding", FAULT_ON_ERROR);
	return;
}

static ulong get_rchan_addr(ulong stp_utt_addr)
{
	ulong stp_utt;
	long offset;

	readmem(stp_utt_addr, KVADDR, &stp_utt, sizeof(void*),
		"stp_utt", FAULT_ON_ERROR);

	/*
	 * If we couldn't get the member offset of struct utt_trace.rchan,
	 * i.e. the debuginfo of the trace module isn't available, we use
	 * sizeof(long) as the offset instead. Currently struct utt_trace
	 * is defined as below:
	 *
	 *     struct utt_trace {
	 *             int trace_state;
	 *             struct rchan *rchan;
	 *             ...
	 *     }
	 *
	 * Although the type of the preceding member is int, sizeof(long)
	 * is OK, because rchan is aligned with long size on both 32-bit
	 * and 64-bit environment. When the definision of struct utt_trace
	 * changed, we must check if this code is correct.
	 */
	if ((offset = MEMBER_OFFSET("utt_trace", "rchan")) < 0) {
		error(WARNING, "The debuginfo of the trace module hasn't been loaded. "
		      "You may not be able to retrieve the correct trace data.\n");
		offset = sizeof(long);
	}

	return (stp_utt + (ulong)offset);
}

static int check_global_buffer(ulong rchan)
{
	int cpu;
	ulong rchan_buf[2];
	
	for (cpu = 0; cpu < 2; cpu++) {
		readmem(rchan + rchan_offsets.buf + sizeof(void*) * cpu,
			KVADDR, &rchan_buf[cpu], sizeof(void*),
			"stp_channel.buf", FAULT_ON_ERROR);
	}
	if (rchan_buf[0] == rchan_buf[1])
		return 1;
	return 0;
}

static void setup_global_data(char *module) 
{
	int i;
	ulong stp_utt_addr = 0;
	ulong stp_rchan_addr = 0;
 	ulong rchan;

	stp_utt_addr = symbol_value_module("_stp_utt", module);
	if (stp_utt_addr == 0) {
		stp_rchan_addr = symbol_value_module("_stp_chan", module);
		if (stp_rchan_addr == 0) {
			error(FATAL, "Failed to find _stp_utt/_stp_chan.\n",
			      module);
		}
		old_format = 1;
	} else {
		stp_rchan_addr = get_rchan_addr(stp_utt_addr);
		if (stp_rchan_addr == 0) {
			error(FATAL, "Failed to find _stp_utt/_stp_chan.\n",
			      module);
		}
	}
	rchan = get_rchan(stp_rchan_addr);
	for (i = 0; i < kt->cpus; i++)
		get_rchan_buf(i, rchan);

	if (kt->cpus > 1) {
		is_global = check_global_buffer(rchan);
	}
	return;
}

static char *create_output_filename(int cpu)
{
	size_t max = 128;
	char *fname;

	fname = (char *)malloc(sizeof(char) * max);
	if (is_global) {
		sprintf(fname, "global");
	} else {
		sprintf(fname, "cpu%d", cpu);
	}
	if (is_broken) {
		strcat(fname, ".may_broken");
	}
	return fname;
}

static void print_rchan_info(size_t start, size_t end, size_t ready, 
			     size_t offset, char *dname, char *fname, int cpu)
{
	size_t start_subbuf, end_subbuf;
	
	start_subbuf = start ? start - chan.n_subbufs : start;
	end_subbuf = start ? end - 1 - chan.n_subbufs : end - 1;
	
	fprintf(fp, "--- generating '%s/%s' ---\n", dname, fname);
	if (is_broken) {
		fprintf(fp, "  read subbuf %ld(%ld) (offset:%ld-%ld)\n",
			(long)end_subbuf - chan.n_subbufs, 
			(long)(end_subbuf % chan.n_subbufs),
			(long)offset,
			(long)chan.subbuf_size);
	} else {
		fprintf(fp, "  subbufs ready on relayfs:%ld\n", (long)ready);
		fprintf(fp, "  n_subbufs:%ld, read subbuf from:%ld(%ld) "
			"to:%ld(%ld) (offset:0-%ld)\n\n",
			(long)chan.n_subbufs, 
			(long)start_subbuf,
			(long)(start_subbuf % chan.n_subbufs),
			(long)end_subbuf, 
			(long)(end_subbuf % chan.n_subbufs),
			(long)offset);
	}
}

static void create_output_dir(char *dirname)
{
	DIR *dir;
	dir = opendir(dirname);
	if (dir) {
		closedir(dir);
	} else {
		if (mkdir(dirname, S_IRWXU) < 0) {
			error(FATAL, "cannot create log directory '%s\n'", dirname);
		}
	}
}

static FILE *open_output_file(char *dname, char *fname)
{
	FILE *filp = NULL;
	char *output_file;
	size_t dlength, flength;

	dlength = strlen(dname);
	flength = strlen(fname);

	output_file = (char *)malloc(sizeof(char) * (dlength + flength) + 1);
	output_file[dlength + flength] = '\0';

	create_output_dir(dname);
	sprintf(output_file,"%s/%s", dname, fname);

	filp = fopen(output_file, "w");
	if (!filp) {
		error(FATAL, "cannot create log file '%s'\n", output_file);
	}

	return filp;
}

static void output_cpu_logs(char *dirname)
{
	int i;
	struct per_cpu_data *pcd;
	size_t n, idx, start, end, len;
	size_t padding;
	char *source, *fname;

	/* allocate subbuf memory */
	subbuf = GETBUF(chan.subbuf_size);
	if (!subbuf) {
		error(FATAL, "cannot allocate memory\n");
	}

	for (i = 0; i < kt->cpus; i++) {
		is_broken = 0;
		pcd = &per_cpu[i];

		if (pcd->buf.subbufs_produced == 0 && pcd->buf.offset == 0) {
			if (is_global == 1) {
				error(WARNING, "There is no data in the relay buffer.\n");
				break;
			} else {
				error(WARNING, "[cpu:%d]There is no data in the relay buffer.\n", i);
				continue;
			}
		}

		if (pcd->buf.subbufs_produced >= chan.n_subbufs) {
			start = pcd->buf.subbufs_produced + 1;
			end = start + chan.n_subbufs;
		} else {
			start = 0;
			end = pcd->buf.subbufs_produced + 1;
		}

		fname = create_output_filename(i);
		print_rchan_info(start, end, pcd->buf.subbufs_produced + 1,
				 pcd->buf.offset, dirname, fname, i);
		outfp = open_output_file(dirname, fname);

		for (n = start; n < end; n++) {
			/* read relayfs subbufs and write to log file */
			idx = n % chan.n_subbufs;
			source = pcd->buf.start + idx * chan.subbuf_size;
			readmem((ulong)pcd->buf.padding + sizeof(padding) * idx,
				KVADDR, &padding, sizeof(padding),
				"padding", FAULT_ON_ERROR);
			if (n == end - 1 && 
			    pcd->buf.offset < chan.subbuf_size) {
				len = pcd->buf.offset;
			} else {
				len = chan.subbuf_size;
			}			
			if (old_format == 1) {
				source += sizeof(unsigned int);
				len -= sizeof(unsigned int) + padding;
			} else {
				len -= padding;
			}
			if (len) {
				readmem((ulong)source, KVADDR, subbuf, len,
					"subbuf", FAULT_ON_ERROR);
				if (fwrite(subbuf, len, 1, outfp) != 1) {
					error(FATAL, "cannot write log data\n");
				}
			}
		}
		fclose(outfp);
		outfp = NULL;
		free(fname);
		fname = NULL;

		/*
		 * -a option retrieve the old data of subbuffer where the  
		 * probe record is written at that time.
		 */
		if (retrieve_all == 1 && 
		    pcd->buf.subbufs_produced >= chan.n_subbufs) {
			is_broken = 1;
			fname = create_output_filename(i);
			print_rchan_info(start, end, 
					 pcd->buf.subbufs_produced + 1, 
					 pcd->buf.offset, dirname, fname, i);
			outfp = open_output_file(dirname, fname);
			idx = (end - 1) % chan.n_subbufs;
			source = pcd->buf.start + idx * chan.subbuf_size + 
				 pcd->buf.offset;
			readmem((ulong)pcd->buf.padding + sizeof(padding)*idx,
				KVADDR, &padding, sizeof(padding),
				"padding", FAULT_ON_ERROR);
			len = chan.subbuf_size - pcd->buf.offset;
			if (len) {
				readmem((ulong)source, KVADDR, subbuf, len,
					"may_broken_subbuf", FAULT_ON_ERROR);
				if(fwrite(subbuf, len, 1, outfp) != 1) {
					error(FATAL, 
					      "cannot write log data(may_broken)\n");
				}
			}
			fclose(outfp);
			outfp = NULL;
			free(fname);
			fname = NULL;
		}
		if (is_global == 1)
			break;
	}
	if (subbuf) {
		FREEBUF(subbuf);
		subbuf = NULL;
	}
	return;
}

static void do_staplog(char *module, char *dirname)
{
	setup_global_data(module);
	output_cpu_logs(dirname);
	return;
}

void cmd_staplog(void)
{

	int c;
	char *module = NULL;
	char *dirname = NULL;

	while ((c = getopt(argcnt, args, "+ao:")) != EOF) {
		switch (c) {
		case 'a':
			retrieve_all = 1;
			break;
		case 'o':
			dirname = optarg;
			break;
		default:
			argerrs++;
			break;
		}
	}
	module = args[optind];

	if (!module || argerrs)
		cmd_usage(pc->curcmd, SYNOPSIS);

	if (dirname == NULL && module != NULL)
		dirname = module;
	do_staplog(module, dirname);
	return;
}

void cmd_staplog_cleanup(void)
{
	if (outfp) {
		fclose(outfp);
		outfp = NULL;
	}
	return;
}

char *help_staplog[] = {
	"systemtaplog",
	"Retrieve SystemTap log data",
	"[-a] [-o dir_name] module_name",
	"  Retrieve SystemTap's log data and write them to files.\n",
	"  All valid SystemTap's log data made by the trace module which name",
	"  is 'module_name' are written into log files. This command starts",
	"  to retrieve log data from the subbuffer which is next to current",
	"  written subbuffer. Therefore some old data in the current written",
	"  subbuffer may not be retrieved. But -a option retrieve these data",
	"  and write them into another log file which have the special ",
	"  postfix `.may_broken`.",
	"  If you don't use -o option, the log files are created in",
	"  `module_name` directory. The name of each log file is cpu0, cpu1..",
	"  ...cpuN. This command don't change the log data format, but remove",
 	"  only padding.",
	"",
	"              -a    Retrieve the old data which is recorded in",
	"                    current written subbufer and create another file",
	"                    that have the special postfix `.may_broken`",
	"                    for these data.",
	"    -o file_name    Specify the output directory.",
	NULL,
};

char *help_staplog_cleanup[] = {
	"systemtaplog cleanup (hidden)",
	"Cleanup command for staplog",
	"",
	"  This command is called during restore_sanity() prior to each ",
	"  command prompt to close the files which was opened and failed to",
	"  close by staplog command.",
	NULL,
};

static void __attribute__ ((constructor)) _init(void) 
{
	get_rchan_offsets();
	register_extension(command_table);
	return;
}


static void __attribute__ ((destructor)) _fini(void)
{
	return;
}
