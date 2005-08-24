#include "../transport/transport_msgs.h"

#ifdef DEBUG
#define dbug(args...) {fprintf(stderr,"%s:%d ",__FUNCTION__, __LINE__); fprintf(stderr,args); }
#else
#define dbug(args...) ;
#endif /* DEBUG */

/*
 * stp external API functions
 */
extern int init_stp(const char *relay_filebase, int print_summary);
extern int stp_main_loop(void);
extern int send_request(int type, void *data, int len);
