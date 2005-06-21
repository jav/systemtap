/* SystemTap control channel command values */
enum
{
	STP_BUF_INFO = 1,
	STP_SUBBUFS_CONSUMED,
        STP_REALTIME_DATA,
        STP_TRANSPORT_MODE,
        STP_EXIT,
};

/* SystemTap transport options */
enum
{
	STP_TRANSPORT_NETLINK = 1,
	STP_TRANSPORT_RELAYFS
};

/*
 * stp external API functions
 */
extern int init_stp(const char *modname,
		    const char *relay_filebase,
		    int print_summary);
extern int stp_main_loop(void);
extern int send_request(int type, void *data, int len);
