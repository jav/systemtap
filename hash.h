#include <string>

extern "C" {
#include <string.h>
#include <mdfour.h>
}

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

  void result(std::string& r);
};

void find_hash (systemtap_session& s, const std::string& script);
