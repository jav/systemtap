/* -*- linux-c -*-
 * Symbols and modules functions for staprun.
 *
 * Copyright (C) 2006-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "staprun.h"

/* send symbol data */
static int send_data(void *data, int len)
{
	int32_t type = STP_SYMBOLS;
	if (write(control_channel, &type, 4) <= 0)
		return -1;
	return write(control_channel, data, len);
}

/* Get the sections for a module. Put them in the supplied buffer */
/* in the following order: */
/* [struct _stp_msg_module][struct _stp_symbol sections ...][string data]*/
/* Return the total length of all the data. */

#define SECDIR "/sys/module/%s/sections"
static int get_sections(char *name, char *data_start, int datalen)
{
	char dir[STP_MODULE_NAME_LEN + sizeof(SECDIR)];
	char filename[STP_MODULE_NAME_LEN + 256]; 
	char buf[32], strdata_start[32768];
	char *strdata=strdata_start, *data=data_start;
	int fd, len, res;
	struct _stp_msg_module *mod = (struct _stp_msg_module *)data_start;
	struct dirent *d;
	DIR *secdir;
	void *sec;
	int struct_symbol_size = kernel_ptr_size == 8 ? sizeof(struct _stp_symbol64) : sizeof(struct _stp_symbol32);
	uint64_t sec_addr;

	/* start of data is a struct _stp_msg_module */
	data += sizeof(struct _stp_msg_module);

	res = snprintf(dir, sizeof(dir), SECDIR, name);
	if (res >= (int)sizeof(dir)) {
		_err("Couldn't fit module \"%s\" into dir buffer.\n"	\
		    "This should never happen. Please file a bug report.\n", name);
		return -1;
	}
	
	if ((secdir = opendir(dir)) == NULL)
		return 0;

	/* Initialize mod. */
	memset(mod, 0, sizeof(struct _stp_msg_module));

	/* Copy name in and check for overflow. */
	strncpy(mod->name, name, STP_MODULE_NAME_LEN);
	if (mod->name[STP_MODULE_NAME_LEN - 1] != '\0') {
		_err("Couldn't fit module \"%s\" into mod->name buffer.\n" \
		    "This should never happen. Please file a bug report.\n", name);
		return -1;
	}

	while ((d = readdir(secdir))) {
		char *secname = d->d_name;

		/* Copy filename in and check for overflow. */
		res = snprintf(filename, sizeof(filename), "/sys/module/%s/sections/%s", name, secname);
		if (res >= (int)sizeof(filename)) {
			_err("Couldn't fit secname \"%s\" into filename buffer.\n" \
			    "This should never happen. Please file a bug report.\n", secname);
			closedir(secdir);
			return -1;
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
				sec = data;
				if (data - data_start + struct_symbol_size > datalen)
					goto err1;
				data += struct_symbol_size;

				sec_addr = (uint64_t)strtoull(buf,NULL,16);
				if (kernel_ptr_size == 8) {
					((struct _stp_symbol64 *)sec)->addr = sec_addr; 
					((struct _stp_symbol64 *)sec)->symbol = (uint64_t)(strdata - strdata_start);
				} else {
					((struct _stp_symbol32 *)sec)->addr = (uint32_t)sec_addr;
					((struct _stp_symbol32 *)sec)->symbol = (uint32_t)(strdata - strdata_start);
				}
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
					if (mod->data == 0 || sec_addr < mod->data)
						mod->data = sec_addr;
				}
				if (!strcmp(secname, ".text"))
					mod->text = sec_addr;
				if (!strcmp(secname, ".gnu.linkonce.this_module"))
					mod->module = sec_addr;
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
	return -1;
}
#undef SECDIR

/* 
 * For modules, we send the name, section names, and offsets
 */
static int send_module (char *mname)
{
	char data[65536];
	int len;
	*(int32_t *)data = STP_MODULE;
	len = get_sections(mname, data + sizeof(int32_t),
			   sizeof(data) - sizeof(int32_t));
	if (len > 0) {
		if (write(control_channel, data, len + sizeof(int32_t)) <= 0) {
			_err("Loading of module %s failed. Exiting...\n", mname);
			return -1;
		}
	}
	return len;
}

/*
 * Send either all modules, or a specific one.
 * Returns:
 *   >=0 : OK
 *    -1 : serious error (exit)
 */
int do_module (void *data)
{
	struct _stp_msg_module *mod = (struct _stp_msg_module *)data;

	if (mod->name[0] == 0) {
		struct dirent *d;
		DIR *moddir = opendir("/sys/module");
		if (moddir) {
			while ((d = readdir(moddir)))
				if (send_module(d->d_name) < 0) {
					closedir(moddir);
					return -1;
				}
			closedir(moddir);
		}
		send_request(STP_MODULE, data, 1);
		return 0;
	}

	return send_module(mod->name);
}

#define MAX_SYMBOLS 32*1024

/*
 * Read /proc/kallsyms and send all kernel symbols to the
 * systemtap module.  Ignore module symbols; the systemtap module
 * can access them directly.
 */
int do_kernel_symbols(void)
{
	FILE *kallsyms=NULL;
	char *name, *mod, *dataptr, *datamax, type, *data_base=NULL;
	unsigned long long addr;
	void *syms = NULL;
	int ret, num_syms, i = 0, struct_symbol_size;
	int max_syms= MAX_SYMBOLS, data_basesize = MAX_SYMBOLS*32;

	if (kernel_ptr_size == 8) 
		struct_symbol_size = sizeof(struct _stp_symbol64);
	else
		struct_symbol_size = sizeof(struct _stp_symbol32);

	syms = malloc(max_syms * struct_symbol_size);
	data_base = malloc(data_basesize);
	if (data_base == NULL || syms == NULL) {
		_err("Failed to allocate memory for symbols\n");
		goto err;
	}
	dataptr = data_base;
	datamax = data_base + data_basesize;

	kallsyms = fopen ("/proc/kallsyms", "r");
	if (!kallsyms) {
		_perr("Fatal error: Unable to open /proc/kallsyms");
		goto err;
	}

	/* put empty string in data */
	*dataptr++ = 0;

	while ((ret = fscanf(kallsyms, "%llx %c %as [%as", &addr, &type, &name, &mod))>0 
	       && dataptr < datamax) {
		if (ret < 3)
			continue;
		if (ret > 3) {
			/* ignore modules */
			free(name);
			free(mod);
			/* modules are loaded above the kernel, so if we */
			/* are getting modules, then we're done. */
			break;
		}

		if (type == 't' || type == 'T' || type == 'A') {
			if (kernel_ptr_size == 8) {
                                ((struct _stp_symbol64 *)syms)[i].addr = (uint64_t)addr;
                                ((struct _stp_symbol64 *)syms)[i].symbol = (uint64_t)(dataptr - data_base);
                        } else {
                                ((struct _stp_symbol32 *)syms)[i].addr = (uint32_t)addr;
                                ((struct _stp_symbol32 *)syms)[i].symbol = (uint32_t)(dataptr - data_base);
                        }
			if (dataptr >= datamax - strlen(name)) {
				char *db;
				data_basesize *= 2;
				db = realloc(data_base, data_basesize);
				if (db == NULL) {
					_err("Could not allocate enough space for symbols.\n");
					goto err;
				}
				dataptr = db + (dataptr - data_base);
				datamax = db + data_basesize;
				data_base = db;
			}
			strcpy(dataptr, name);
			dataptr += strlen(name) + 1;
			free(name);
			i++;
			if (i >= max_syms) {
				max_syms *= 2;
				syms = realloc(syms, max_syms*struct_symbol_size);
				if (syms == NULL) {
					_err("Could not allocate enough space for symbols.\n");
					goto err;
				}
			}
		}
	}
	num_syms = i;
	if (num_syms <= 0)
		goto err;

	/* send header */
	struct _stp_msg_symbol_hdr smsh;
	smsh.num_syms = num_syms;
	smsh.sym_size = (uint32_t)(dataptr - data_base);
	if (send_request(STP_SYMBOLS, &smsh, sizeof(smsh)) <= 0)
		goto err;

	/* send syms */
	if (send_data(syms, num_syms*struct_symbol_size) < 0)
		goto err;
	
	/* send data */
	if (send_data(data_base, dataptr-data_base) < 0)
		goto err;

	free(data_base);
	free(syms);
	fclose(kallsyms);
	return 0;

err:
	if (data_base)
		free(data_base);
	if (syms)
		free(syms);
	if (kallsyms)
		fclose(kallsyms);

	_err("Loading of symbols failed. Exiting...\n");
	return -1;
}
