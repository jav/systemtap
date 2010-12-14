#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>
extern "C" {
#include <stdint.h>
}

const char *get_home_directory(void);
size_t get_file_size(const std::string &path);
size_t get_file_size(int fd);
bool file_exists (const std::string &path);
bool copy_file(const std::string& src, const std::string& dest,
               bool verbose=false);
int create_dir(const char *dir, int mode = 0777);
int remove_file_or_dir(const char *dir);
bool in_group_id (gid_t target_gid);
std::string getmemusage ();
void tokenize(const std::string& str, std::vector<std::string>& tokens,
	      const std::string& delimiters);
std::string find_executable(const std::string& name,
			    const std::string& env_path = "PATH");
const std::string cmdstr_quoted(const std::string& cmd);
std::string git_revision(const std::string& path);
int stap_waitpid(int verbose, pid_t pid);
pid_t stap_spawn(int verbose, const std::string& command);
int stap_system(int verbose, const std::string& command);
int stap_system_read(int verbose, const std::string& command, std::ostream& out);
int kill_stap_spawn(int sig);
void assert_regexp_match (const std::string& name, const std::string& value, const std::string& re);
int regexp_match (const std::string& value, const std::string& re, std::vector<std::string>& matches);
bool contains_glob_chars (const std::string &str);
std::string normalize_machine(const std::string& machine);

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


// We want [u]int8_t to be treated numerically, not just extracting a char.
template <>
inline int8_t lex_cast(std::string const & in)
{
  int16_t out = lex_cast<int16_t>(in);
  if (out < -128 || out > 127)
    throw std::runtime_error("bad lexical cast");
  return out;
}
template <>
inline uint8_t lex_cast(std::string const & in)
{
  uint16_t out = lex_cast<uint16_t>(in);
  if (out > 0xff && out < 0xff80) // don't error if it looks sign-extended
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


// Returns whether a string starts with the given prefix
inline bool
startswith(const std::string & s, const char * prefix)
{
  return (s.compare(0, std::strlen(prefix), prefix) == 0);
}


// Returns whether a string ends with the given suffix
inline bool
endswith(const std::string & s, const char * suffix)
{
  size_t s_len = s.size(), suffix_len = std::strlen(suffix);
  if (suffix_len > s_len)
    return false;
  return (s.compare(s_len - suffix_len, suffix_len, suffix) == 0);
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
