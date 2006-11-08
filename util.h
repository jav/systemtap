#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>


const char *get_home_directory(void);
int copy_file(const char *src, const char *dest);
int create_dir(const char *dir);
void tokenize(const std::string& str, std::vector<std::string>& tokens,
	      const std::string& delimiters);
bool find_executable(const char *name, std::string& retpath);


// stringification generics


template <typename T>
inline std::string
stringify(T t)
{
  std::ostringstream s;
  s << t;
  return s.str ();
}


template <typename OUT, typename IN> 
inline OUT lex_cast(IN const & in)
{
  std::stringstream ss;
  OUT out;
  if (!(ss << in && ss >> out))
    throw std::runtime_error("bad lexical cast");
  return out;
}


template <typename OUT, typename IN> 
inline OUT
lex_cast_hex(IN const & in)
{
  std::stringstream ss;
  OUT out;
  if (!(ss << std::ios::hex << std::ios::showbase << in && ss >> out))
    throw std::runtime_error("bad lexical cast");
  return out;
}


// Return as quoted string, so that when compiled as a C literal, it
// would print to the user out nicely.
template <typename IN> 
inline std::string
lex_cast_qstring(IN const & in)
{
  std::stringstream ss;
  std::string out, out2;
  if (!(ss << in))
    throw std::runtime_error("bad lexical cast");
  out = ss.str();
  out2 += '"';
  for (unsigned i=0; i<out.length(); i++)
    {
      if (out[i] == '"' || out[i] == '\\') // XXX others?
	out2 += '\\';
      out2 += out[i];
    }
  out2 += '"';
  return out2;
}
