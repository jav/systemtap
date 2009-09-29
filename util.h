#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>

const char *get_home_directory(void);
size_t get_file_size(const std::string &path);
bool file_exists (const std::string &path);
bool copy_file(const std::string& src, const std::string& dest,
               bool verbose=false);
int create_dir(const char *dir);
int remove_file_or_dir(const char *dir);
void tokenize(const std::string& str, std::vector<std::string>& tokens,
	      const std::string& delimiters);
std::string find_executable(const std::string& name);
const std::string cmdstr_quoted(const std::string& cmd);
std::string git_revision(const std::string& path);
int stap_system(int verbose, const std::string& command);
int kill_stap_spawn(int sig);


// stringification generics


template <typename IN>
inline std::string lex_cast(IN const & in)
{
  std::ostringstream ss;
  if (!(ss << in))
    throw std::runtime_error("bad lexical cast");
  return ss.str();
}


template <typename OUT>
inline OUT lex_cast(std::string const & in)
{
  std::istringstream ss(in);
  OUT out;
  if (!(ss >> out && ss.eof()))
    throw std::runtime_error("bad lexical cast");
  return out;
}


template <typename IN>
inline std::string
lex_cast_hex(IN const & in)
{
  std::ostringstream ss;
  if (!(ss << std::showbase << std::hex << in))
    throw std::runtime_error("bad lexical cast");
  return ss.str();
}


// Return as quoted string, so that when compiled as a C literal, it
// would print to the user out nicely.
template <typename IN>
inline std::string
lex_cast_qstring(IN const & in)
{
  std::stringstream ss;
  if (!(ss << in))
    throw std::runtime_error("bad lexical cast");
  return lex_cast_qstring(ss.str());
}


template <>
inline std::string
lex_cast_qstring(std::string const & in)
{
  std::string out;
  out += '"';
  for (const char *p = in.c_str(); *p; ++p)
    {
      unsigned char c = *p;
      if (! isprint(c))
        {
          out += '\\';
          // quick & dirty octal converter
          out += "01234567" [(c >> 6) & 0x07];
          out += "01234567" [(c >> 3) & 0x07];
          out += "01234567" [(c >> 0) & 0x07];
        }
      else if (c == '"' || c == '\\')
        {
          out += '\\';
          out += c;
        }
      else
        out += c;
    }
  out += '"';
  return out;
}


// Delete all values from a map-like container and clear it
// (The template is permissive -- be good!)
template <typename T>
void delete_map(T& t)
{
  for (typename T::iterator i = t.begin(); i != t.end(); ++i)
    delete i->second;
  t.clear();
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
