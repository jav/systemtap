#define SYSTEMTAP_CACHE_MAX_FILENAME "cache_mb_limit"
#define SYSTEMTAP_CACHE_DEFAULT_MB 64

struct cache_ent_info {
  std::string path;
  size_t size;
  long weight;  //lower == removed earlier
};

struct weight_sorter {
  bool operator() (const struct cache_ent_info& c1, const struct cache_ent_info& c2) const
  { return c1.weight < c2.weight;}
};

void add_to_cache(systemtap_session& s);
bool get_from_cache(systemtap_session& s);
void clean_cache(systemtap_session& s);
long get_cache_file_size(const std::string &cache_ent_path);
long get_cache_file_weight(const std::string &cache_ent_path);
void unlink_cache_entry(const std::string &cache_ent_path);

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
