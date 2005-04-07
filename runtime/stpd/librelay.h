/*
 * librelay.h - relay-app user space 'library' header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2005 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 *
 */

/* relay-app control channel command values */
enum
{
	RELAY_APP_BUF_INFO = 1,
	RELAY_APP_SUBBUFS_CONSUMED,
	RELAY_APP_START,
	RELAY_APP_STOP,
	RELAY_APP_CHAN_CREATE,
	RELAY_APP_CHAN_DESTROY,
	RELAY_APP_USERCMD_START = 32
};

/*
 * relay-app external API functions
 */
extern int init_relay_app(const char *relay_filebase,
			  const char *out_filebase,
			  unsigned sub_buf_size,
			  unsigned n_sub_bufs,
			  int print_summary);

extern int _init_relay_app(const char *relay_filebase,
			   const char *out_filebase,
			   unsigned sub_buf_size,
			   unsigned n_sub_bufs,
			   int print_summary,
			   int netlink_unit);

extern int relay_app_main_loop(void);
extern int send_request(int type, void *data, int len);
