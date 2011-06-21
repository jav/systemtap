#include <string>
#include <vector>

// Grabbed from linux/module.h kernel include.
#define MODULE_NAME_LEN (64 - sizeof(unsigned long))

class hash;

void find_script_hash (systemtap_session& s, const std::string& script);
void find_stapconf_hash (systemtap_session& s);
std::string find_tracequery_hash (systemtap_session& s,
                                  const std::vector<std::string>& headers);
std::string find_typequery_hash (systemtap_session& s, const std::string& name);
std::string find_uprobes_hash (systemtap_session& s);

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
