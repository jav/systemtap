#ifndef UTT_H
#define UTT_H

enum {
	Utt_trace_setup = 1,
	Utt_trace_running,
	Utt_trace_stopped,
};

struct utt_trace {
	int trace_state;
	struct rchan *rchan;
	unsigned long *sequence;
	struct dentry *dropped_file;
	atomic_t dropped;
	struct dentry *utt_tree_root;
	void *private_data;
};

#define UTT_TRACE_ROOT_NAME_SIZE	32	/* Largest string for a root dir identifier */
#define UTT_TRACE_NAME_SIZE		32	/* Largest string for a trace identifier */

/*
 * User setup structure
 */
struct utt_trace_setup {
	char root[UTT_TRACE_ROOT_NAME_SIZE];	/* input */
	char name[UTT_TRACE_NAME_SIZE];		/* input */
	u32 buf_size;				/* input */
	u32 buf_nr;				/* input */
	int is_global;				/* input */
	int err;				/* output */
};


extern struct utt_trace *utt_trace_setup(struct utt_trace_setup *utts);
extern int utt_trace_startstop(struct utt_trace *utt, int start,
			       unsigned int *trace_seq);
extern void utt_trace_cleanup(struct utt_trace *utt);
extern int utt_trace_remove(struct utt_trace *utt);

#endif
