/* SystemTap transport values */
enum
{
	STP_TRANSPORT_PROC = 1,
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
	STP_OOB_DATA,
	STP_SYSTEM,
	STP_SYMBOLS,
	STP_MODULE,
};

/* control channel command structs */
struct _stp_buf_info
{
	int32_t cpu;
	uint32_t produced;
	uint32_t consumed;
	int32_t flushing;
};

struct _stp_consumed_info
{
	int32_t cpu;
	uint32_t consumed;
};

struct _stp_transport_info
{
	uint32_t buf_size;
	uint32_t subbuf_size;
	uint32_t n_subbufs;
	int32_t transport_mode;
	int32_t merge;		// merge relayfs output?
	int32_t target;		// target pid
#if 0
	char cmd[256];		// cmd to process data
#endif
};

struct _stp_transport_start
{
	int32_t pid;	// pid for streaming data
};

struct _stp_cmd_info
{
	char cmd[128];
};

struct _stp_symbol_req
{
	int32_t endian;
	int32_t ptr_size;
};

	
