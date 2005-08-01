/* SystemTap transport values */
enum
{
	STP_TRANSPORT_NETLINK = 1,
	STP_TRANSPORT_RELAYFS
};

/* stp control channel command values */
enum
{
	STP_BUF_INFO = 1,
	STP_SUBBUFS_CONSUMED,
        STP_REALTIME_DATA,
        STP_TRANSPORT_INFO,
	STP_START,
        STP_EXIT,
};

/* control channel command structs */
struct buf_info
{
	int cpu;
	unsigned produced;
	unsigned consumed;
};

struct consumed_info
{
	int cpu;
	unsigned consumed;
};

struct transport_info
{
	unsigned subbuf_size;
	unsigned n_subbufs;
	int transport_mode;
	int target;		// target pid
	char cmd[256];		// cmd to process data
};

struct transport_start
{
	int pid;	// pid for streaming data
};

