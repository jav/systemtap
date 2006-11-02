#include "../transport/transport_msgs.h"

#ifdef DEBUG
#define dbug(args...) {fprintf(stderr,"%s:%d ",__FUNCTION__, __LINE__); fprintf(stderr,args); }
#else
#define dbug(args...) ;
#endif /* DEBUG */

/*
 * functions
 */
int init_stp(int print_summary);
int stp_main_loop(void);
int send_request(int type, void *data, int len);
void cleanup_and_exit (int);
int do_module(void *);
void do_kernel_symbols(void);

/*
 * variables 
 */
extern int control_channel;
