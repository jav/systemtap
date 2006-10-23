#include <string>
#include <vector>

const char *get_home_directory(void);

int copy_file(const char *src, const char *dest);

int create_dir(const char *dir);

void tokenize(const std::string& str, std::vector<std::string>& tokens,
	      const std::string& delimiters);

bool find_executable(const char *name, std::string& retpath);
