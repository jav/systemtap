/*
 crash shared object for retrieving systemtap buffer
 Copyright (c) 2007 Hitachi,Ltd.,
 Created by Satoru Moriya &lt;satoru.moriya.br@hitachi.com&gt;
 
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

#define STPLOG_NO_MOD  -1
#define STPLOG_NO_SYM  -2

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
static int old_format;

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

	readmem(stp_utt_addr, KVADDR, &stp_utt, sizeof(void*),
		"stp_utt", FAULT_ON_ERROR);
	return (stp_utt + sizeof(int));
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

static void output_cpu_logs(char *filename)
{
	int i, max = 256;
	struct per_cpu_data *pcd;
	size_t n, idx, start, end, ready, len;
	unsigned padding;
	char fname[max + 1], *source;
	DIR *dir;

	/* check and create log directory */
	dir = opendir(filename);
	if (dir) {
		closedir(dir);
	} else {
		if (mkdir(filename, S_IRWXU) < 0) {
			error(FATAL, "cannot create log directory '%s\n'", filename);
		}
	}

	/* allocate subbuf memory */
	subbuf = GETBUF(chan.subbuf_size);
	if (!subbuf) {
		error(FATAL, "cannot allocate memory\n");
	}

	fname[max] = '\0';
	for (i = 0; i < kt->cpus; i++) {
		int adjust = 0;
		pcd = &per_cpu[i];

		if (pcd->buf.offset == 0 || 
		    pcd->buf.offset == chan.subbuf_size + 1) {
			adjust = 0;
		} else {
			adjust = 1;
		}
		ready = pcd->buf.subbufs_produced + adjust;

		if (ready > chan.n_subbufs) {
			start = ready;
			end = start + chan.n_subbufs;
		} else {
			start = 0;
			end = ready;
		}
		/* print information */
		fprintf(fp, "--- generating 'cpu%d' ---\n", i);
		fprintf(fp, "  subbufs ready on relayfs:%ld\n", (long)ready);
		fprintf(fp, "    n_subbufs:%ld, read from:%ld to:%ld (offset:%ld)\n\n",
			(long)chan.n_subbufs, (long)start, (long)end, (long)pcd->buf.offset);

		/* create log file */
		snprintf(fname, max, "%s/cpu%d", filename, i);
		outfp = fopen(fname, "w");
		if (!outfp) {
			error(FATAL, "cannot create log file '%s'\n", fname);
		}
		for (n = start; n < end; n++) {
			/* read relayfs subbufs and write to log file */
			idx = n % chan.n_subbufs;
			source = pcd->buf.start + idx * chan.subbuf_size;
			readmem((ulong)pcd->buf.padding + sizeof(padding) * idx,
				KVADDR, &padding, sizeof(padding),
				"padding", FAULT_ON_ERROR);
			if (n == end - 1 &&  0 < pcd->buf.offset &&
			    pcd->buf.offset < chan.subbuf_size) {
				len = pcd->buf.offset;
			} else {
				len = chan.subbuf_size;
			}			
			if (old_format == 1) {
				source += sizeof(padding);
				len -= sizeof(padding) + padding;
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
		if (is_global == 1)
			break;
	}
	if (subbuf) {
		FREEBUF(subbuf);
		subbuf = NULL;
	}
	return;
}

static void do_staplog(char *module, char *filename)
{
	setup_global_data(module);
	output_cpu_logs(filename);
	return;
}

void cmd_staplog(void)
{

	int c;
	char *module = NULL;
	char *filename = NULL;

	while ((c = getopt(argcnt, args, "o:")) != EOF) {
		switch (c) {
		case 'o':
			filename = optarg;
			break;
		default:
			argerrs++;
			break;
		}
	}
	module = args[optind];

	if (!module || argerrs)
		cmd_usage(pc->curcmd, SYNOPSIS);

	if (filename == NULL && module != NULL)
		filename = module;
	do_staplog(module, filename);
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
	"[-o dir_name] module_name",
	"  Retrieve SystemTap's log data and write them to files.\n",
	"    module_name     All valid SystemTap log data made by the trace",
	"                    module which name is 'module_name' are written",
	"                    into log files. If you don't use -o option, the",
	"                    log files are created in `module_name` directory.", 
	"                    The name of each log file is cpu0, cpu1...cpuN. ",
	"                    They have same format data as channel buffer",
	"                    except padding(This command removes padding). ",
	"",
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
