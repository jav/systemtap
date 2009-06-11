#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>

const char *get_home_directory(void);
size_t get_file_size(const std::string &path);
bool file_exists (const std::string &path);
int copy_file(const char *src, const char *dest);
int create_dir(const char *dir);
int remove_file_or_dir(const char *dir);
void tokenize(const std::string& str, std::vector<std::string>& tokens,
	      const std::string& delimiters);
std::string find_executable(const std::string& name);
const std::string cmdstr_quoted(const std::string& cmd);
std::string git_revision(const std::string& path);
int stap_system(const char *command);
int kill_stap_spawn(int sig);


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
  // NB: ss >> string out assumes that "in" renders to one word
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
  // NB: ss >> string out assumes that "in" renders to one word
  if (!(ss << "0x" << std::hex << in && ss >> out))
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
  out = ss.str(); // "in" is expected to render to more than one word
  out2 += '"';
  for (unsigned i=0; i<out.length(); i++)
    {
      char c = out[i];
      if (! isprint(c))
        {
          out2 += '\\';
          // quick & dirty octal converter
          out2 += "01234567" [(c >> 6) & 0x07];
          out2 += "01234567" [(c >> 3) & 0x07];
          out2 += "01234567" [(c >> 0) & 0x07];
        }
      else if (c == '"' || c == '\\')
        {
          out2 += '\\';
          out2 += c;
        }
      else
        out2 += c;
    }
  out2 += '"';
  return out2;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
