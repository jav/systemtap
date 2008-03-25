/* -*- linux-c -*-
 * Unwind data functions for staprun.
 *
 * Copyright (C) 2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "staprun.h"
#include <elfutils/libdwfl.h>
#include <dwarf.h>

static char debuginfo_path_arr[] = "-:.debug:/usr/lib/debug";
static char *debuginfo_path = debuginfo_path_arr;
static const Dwfl_Callbacks kernel_callbacks = {
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.debuginfo_path = &debuginfo_path,
	.find_elf = dwfl_linux_kernel_find_elf,
	.section_address = dwfl_linux_kernel_module_section_address,
};

void *get_module_unwind_data(Dwfl * dwfl, const char *name, int *len)
{
	Dwarf_Addr bias = 0;
	Dwarf *dw;
	GElf_Ehdr *ehdr, ehdr_mem;
	GElf_Shdr *shdr, shdr_mem;
	Elf_Scn *scn = NULL;
	Elf_Data *data = NULL;

	Dwfl_Module *mod = dwfl_report_module(dwfl, name, 0, 0);
	dwfl_report_end(dwfl, NULL, NULL);
	dw = dwfl_module_getdwarf(mod, &bias);
	Elf *elf = dwarf_getelf(dw);
	ehdr = gelf_getehdr(elf, &ehdr_mem);
	while ((scn = elf_nextscn(elf, scn))) {
		shdr = gelf_getshdr(scn, &shdr_mem);
		if (strcmp(elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name), ".debug_frame") == 0) {
			data = elf_rawdata(scn, NULL);
			break;
		}
	}

	if (data == NULL) {
		*len = 0;
		dbug(2, "module %s returns NULL\n", name);
		return NULL;
	}
	dbug(2, "module %s returns %d\n", name, (int)data->d_size);
	*len = data->d_size;
	return data->d_buf;
}

void send_unwind_data(const char *name)
{
	struct _stp_msg_unwind *un;
	int unwind_data_len = 0;
	void *unwind_data = NULL;
	char *buf;

	dbug(2, "module %s\n", name);
	if (strcmp(name, "*")) {
		Dwfl *dwfl = dwfl_begin(&kernel_callbacks);

		if (name[0] == 0)
			unwind_data = get_module_unwind_data(dwfl, "kernel", &unwind_data_len);
		else
			unwind_data = get_module_unwind_data(dwfl, name, &unwind_data_len);

		/* yuck */
		buf = (char *)malloc(unwind_data_len + sizeof(*un) + sizeof(uint32_t));
		if (!buf) {
			err("malloc failed\n");
			return;
		}
		memcpy(buf + sizeof(*un) + sizeof(uint32_t), unwind_data, unwind_data_len);
		dwfl_end(dwfl);
	} else {
		buf = (char *)malloc(sizeof(*un) + sizeof(uint32_t));
		if (!buf) {
			err("malloc failed\n");
			return;
		}
	}
		
	un = (struct _stp_msg_unwind *)(buf + sizeof(uint32_t));
	strncpy(un->name, name, sizeof(un->name));
	un->unwind_len = unwind_data_len;
	*(uint32_t *) buf = STP_UNWIND;

	/* send unwind data */
	if (write(control_channel, buf, unwind_data_len + sizeof(*un) + sizeof(uint32_t)) <= 0)
		err("write failed\n");
}
