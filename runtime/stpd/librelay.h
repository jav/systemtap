/* SystemTap control channel command values */
enum
{
	STP_BUF_INFO = 1,
	STP_SUBBUFS_CONSUMED,
        STP_REALTIME_DATA,
        STP_EXIT,
};

/*
 * stp external API functions
 */
extern int init_stp(const char *modname,
		    const char *relay_filebase,
		    const char *out_filebase,
		    unsigned sub_buf_size,
		    unsigned n_sub_bufs,
		    int print_summary);

extern int stp_main_loop(void);
extern int send_request(int type, void *data, int len);
