
/* create a map with a max of 100 elements */
MAP mymap = _stp_map_new(100, INT64);

/* mymap[birth year] = 2000 */
map_key_str (mymap, "birth year");
map_set_int64 (mymap, 2000);  
