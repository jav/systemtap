#include <string>

extern "C" {
#include <string.h>
#include <mdfour.h>
}

// Grabbed from linux/module.h kernel include.
#define MODULE_NAME_LEN (64 - sizeof(unsigned long))

class hash
{
private:
  struct mdfour md4;

public:
  hash() { start(); }

  void start();

  void add(const unsigned char *buffer, size_t size);
  void add(const int x) { add((const unsigned char *)&x, sizeof(x)); }
  void add(const long x) { add((const unsigned char *)&x, sizeof(x)); }
  void add(const long long x) { add((const unsigned char *)&x, sizeof(x)); }
  void add(const unsigned int x) { add((const unsigned char *)&x, sizeof(x)); }
  void add(const unsigned long x) { add((const unsigned char *)&x,
					sizeof(x)); }
  void add(const unsigned long long x) { add((const unsigned char *)&x,
					     sizeof(x)); }
  void add(const char *s) { add((const unsigned char *)s, strlen(s)); }
  void add(const std::string& s) { add((const unsigned char *)s.c_str(),
				       s.length()); }
  void add_file(const std::string& filename);

  void result(std::string& r);
};

void find_hash (systemtap_session& s, const std::string& script);
std::string find_tracequery_hash (systemtap_session& s, const std::string& header);
std::string find_typequery_hash (systemtap_session& s, const std::string& name);

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
