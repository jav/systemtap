#include "../transport/transport_msgs.h"

/*
 * stp external API functions
 */
extern int init_stp(const char *relay_filebase, int print_summary);
extern int stp_main_loop(void);
extern int send_request(int type, void *data, int len);
