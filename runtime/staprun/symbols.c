/* -*- linux-c -*-
 * Symbols and modules functions for staprun.
 *
 * Copyright (C) 2006, 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "staprun.h"
#include "../sym.h"

static int send_data(void *data, int len)
{
	return write(control_channel, data, len);
}

/* Get the sections for a module. Put them in the supplied buffer */
/* in the following order: */
/* [struct _stp_module][struct _stp_symbol sections ...][string data] */
/* Return the total length of all the data. */

#define SECDIR "/sys/module/%s/sections"
static int get_sections(char *name, char *data_start, int datalen)
{
	char dir[STP_MODULE_NAME_LEN + sizeof(SECDIR)];
	char filename[STP_MODULE_NAME_LEN + 256]; 
	char buf[32], strdata_start[32768];
	char *strdata=strdata_start, *data=data_start;
	int fd, len, res;
	struct _stp_module *mod = (struct _stp_module *)data_start;
	struct dirent *d;
	DIR *secdir;
	struct _stp_symbol *sec;

	/* start of data is a struct _stp_module */
	data += sizeof(struct _stp_module);

	res = snprintf(dir, sizeof(dir), SECDIR, name);
	if (res >= (int)sizeof(dir)) {
		_err("Couldn't fit module \"%s\" into dir buffer.\n"	\
		    "This should never happen. Please file a bug report.\n", name);
		exit(1);
	}
	
	if ((secdir = opendir(dir)) == NULL)
		return 0;

	/* Initialize mod. */
	memset(mod, 0, sizeof(struct _stp_module));

	/* Copy name in and check for overflow. */
	strncpy(mod->name, name, STP_MODULE_NAME_LEN);
	if (mod->name[STP_MODULE_NAME_LEN - 1] != '\0') {
		_err("Couldn't fit module \"%s\" into mod->name buffer.\n" \
		    "This should never happen. Please file a bug report.\n", name);
		exit(1);
	}

	while ((d = readdir(secdir))) {
		char *secname = d->d_name;

		/* Copy filename in and check for overflow. */
		res = snprintf(filename, sizeof(filename), "/sys/module/%s/sections/%s", name, secname);
		if (res >= (int)sizeof(filename)) {
			_err("Couldn't fit secname \"%s\" into filename buffer.\n" \
			    "This should never happen. Please file a bug report.\n", secname);
			closedir(secdir);
			exit(1);
		}
		
		/* filter out some non-useful stuff */
		if (!strncmp(secname,"__",2) 
		    || !strcmp(secname,".") 
		    || !strcmp(secname,"..") 
		    || !strcmp(secname,".module_sig") 
		    || !strcmp(secname,".modinfo") 
		    || !strcmp(secname,".strtab") 
		    || !strcmp(secname,".symtab") ) {
			continue;
		}
		if (!strncmp(secname, ".gnu.linkonce", 13) 
		    && strcmp(secname, ".gnu.linkonce.this_module"))
			continue;

		if ((fd = open(filename,O_RDONLY)) >= 0) {
			if (read(fd, buf, 32) > 0) {
				/* create next section */
				sec = (struct _stp_symbol *)data;
				if (data - data_start + (int)sizeof(struct _stp_symbol) > datalen)
					goto err1;
				data += sizeof(struct _stp_symbol);
				sec->addr = strtoul(buf,NULL,16);
				sec->symbol = (char *)(strdata - strdata_start);
				mod->num_sections++;

				/* now create string data for the
				 * section (checking for overflow) */
				if ((strdata - strdata_start + strlen(strdata))
				    >= sizeof(strdata_start))
					goto err1;
				strcpy(strdata, secname);
				strdata += strlen(secname) + 1;

				/* These sections are used a lot so keep the values handy */
				if (!strcmp(secname, ".data") || !strncmp(secname, ".rodata", 7)) {
					if (mod->data == 0 || sec->addr < mod->data)
						mod->data = sec->addr;
				}
				if (!strcmp(secname, ".text"))
					mod->text = sec->addr;
				if (!strcmp(secname, ".gnu.linkonce.this_module"))
					mod->module = sec->addr;
			}
			close(fd);
		}
	}
	closedir(secdir);

	/* consolidate buffers */
	len = strdata - strdata_start;
	if ((len + data - data_start) > datalen)
		goto err0;
	strdata = strdata_start;
	while (len--)
		*data++ = *strdata++;
	
	return data - data_start;

err1:
	close(fd);
	closedir(secdir);
err0:
	/* if this happens, something went seriously wrong. */
	_err("Unexpected error. Overflowed buffers.\n");
	exit(1);
	return 0; /* not reached */
}
#undef SECDIR

void send_module (char *mname)
{
	char data[32768];
	int len = get_sections(mname, data, sizeof(data));
	if (len) {
		if (send_request(STP_MODULE, data, len) < 0) {
			_err("Loading of module %s failed. Exiting...\n", mname);
			exit(1);
		}
	}
}

int do_module (void *data)
{
	struct _stp_module *mod = (struct _stp_module *)data;

	if (mod->name[0] == 0) {
		struct dirent *d;
		DIR *moddir = opendir("/sys/module");
		if (moddir) {
			while ((d = readdir(moddir)))
				send_module(d->d_name);
			closedir(moddir);
		}
		send_request(STP_MODULE, data, 0);
		return 1;
	}

	send_module(mod->name);
	return 0;
}

static int compar(const void *p1, const void *p2)
{
	struct _stp_symbol *s1 = (struct _stp_symbol *)p1;
	struct _stp_symbol *s2 = (struct _stp_symbol *)p2;
	if (s1->addr == s2->addr) return 0;
	if (s1->addr < s2->addr) return -1;
	return 1;
}

#define MAX_SYMBOLS 32*1024

void do_kernel_symbols(void)
{
	FILE *kallsyms=NULL;
	char *sym_base=NULL, *data_base=NULL;
	char buf[128], *ptr, *name, *data, *dataptr, *datamax, type;
	unsigned long addr;
	struct _stp_symbol *syms;
	int num_syms, i = 0, max_syms= MAX_SYMBOLS;
	int data_basesize = MAX_SYMBOLS*32;

	sym_base = malloc(max_syms*sizeof(struct _stp_symbol)+sizeof(long));
	data_base = malloc(data_basesize);
	if (data_base == NULL || sym_base == NULL) {
		_err("Failed to allocate memory for symbols\n");
		goto err;
	}
	*(int *)data_base = STP_SYMBOLS;
	dataptr = data = data_base + sizeof(long);
	datamax = data_base + data_basesize;

	*(int *)sym_base = STP_SYMBOLS;
	syms = (struct _stp_symbol *)(sym_base + sizeof(long));

	kallsyms = fopen ("/proc/kallsyms", "r");
	if (!kallsyms) {
		_perr("Fatal error: Unable to open /proc/kallsyms");
		goto err;
	}

	/* put empty string in data */
	*dataptr++ = 0;

	while (fgets_unlocked(buf, 128, kallsyms) && dataptr < datamax) {
		addr = strtoul(buf, &ptr, 16);
		while (isspace(*ptr)) ptr++;
		type = *ptr++;
		if (type == 't' || type == 'T' || type == 'A') {
			while (isspace(*ptr)) ptr++;
			name = ptr++;
			while (!isspace(*ptr)) ptr++;
			*ptr++ = 0;
			while (*ptr && *ptr != '[') ptr++;
			if (*ptr)
				continue; /* it was a module */
			syms[i].addr = addr;
			syms[i].symbol = (char *)(dataptr - data);
			while (*name) *dataptr++ = *name++;
			*dataptr++ = 0;
			i++;
			if (i >= max_syms) {
				char *s;
				max_syms *= 2;
				s = realloc(sym_base, max_syms*sizeof(struct _stp_symbol)+sizeof(long));
				if (s == NULL) {
					_err("Could not allocate enough space for symbols.\n");
					goto err;
				}
				syms = (struct _stp_symbol *)(s + sizeof(long));
				sym_base = s;
			}
			if (dataptr > datamax - 1024) {
				char *db;
				data_basesize *= 2;
				db = realloc(data_base, data_basesize);
				if (db == NULL) {
					_err("Could not allocate enough space for symbols.\n");
					goto err;
				}
				dataptr = db + (dataptr - data_base);
				data = db + sizeof(long);
				datamax = db + data_basesize;
				data_base = db;
			}
		}
	}
	num_syms = i;
	qsort(syms, num_syms, sizeof(struct _stp_symbol), compar);

	/* send header */
	*(int *)buf = STP_SYMBOLS;
	*(int *)(buf+sizeof(long)) = num_syms;
	*(int *)(buf+sizeof(long)+sizeof(int)) = (unsigned)(dataptr - data);
	if (send_data(buf, 2*sizeof(int)+sizeof(long)) < 0)
		goto err;

	/* send syms */
	if (send_data(sym_base, num_syms*sizeof(struct _stp_symbol)+sizeof(long)) < 0)
		goto err;
	
	/* send data */
	if (send_data(data_base, dataptr-data+sizeof(long)) < 0)
		goto err;

	free(data_base);
	free(sym_base);
	fclose(kallsyms);
	return;

err:
	if (data_base)
		free(data_base);
	if (sym_base)
		free(sym_base);
	if (kallsyms)
		fclose(kallsyms);

	_err("Loading of symbols failed. Exiting...\n");
	exit(1);
}
