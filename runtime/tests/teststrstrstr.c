#include "test.h"

/* teststrstrstr.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - str,str
   val - str
*/



static void
map_dump (MAP map)
{
  struct map_node_str *ptr;
  printf ("\n");
  for (ptr = (struct map_node_str *)map_start(map); ptr; 
       ptr = (struct map_node_str *)map_iter (map, (struct map_node *)ptr))
    printf ("map[%s,%s] = %s\n", key1str(ptr), key2str(ptr), ptr->str);
  printf ("\n");
}

static void m_print (MAP map)
{
  struct map_node_str *m = (struct map_node_str *)map->key;
  printf ("map[%s,%s]=%s\n", key1str(m), key2str(m), map_get_str(map));
}
int main ()
{
  MAP mymap = map_new(4, STRING);

  _stp_map_key2 (mymap, "two", "2" );
  _stp_map_set (mymap, "four");
  m_print (mymap);

  _stp_map_key2 (mymap, "eighty-four", "nineteen hundred");
  _stp_map_set (mymap, "nineteen hundred and eighty-three");

  _stp_map_key2 (mymap, "two-two-one-B", "Baker Street");
  _stp_map_set (mymap, "7% solution");

  m_print (mymap);
  _stp_map_key2 (mymap, "two", "2");  
  m_print (mymap);
  _stp_map_key2 (mymap, "eighty-four", "nineteen hundred");
  m_print (mymap);

  _stp_map_set (mymap, "nineteen hundred and eighty-four");
  m_print (mymap);


  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_key2 (mymap, NULL, NULL);  
  map_key_del (mymap);

  _stp_map_key2 (mymap, "0123456789", "4444");  
  _stp_map_set (mymap,"1000000");

  map_dump (mymap);

  _stp_map_key2 (mymap, "77", "66");  
  map_key_del (mymap);
  map_key_del (mymap);
  _stp_map_set (mymap,"99999999");

  /* create and delete a key */
  _stp_map_key2 (mymap, "2048", "2");
  _stp_map_set (mymap, "4096");

  _stp_map_key2 (mymap, "2048", "2");
  m_print (mymap);
  map_key_del (mymap);

  printf ("mymap[2048,2]=%s\n", map_get_str(mymap));
  _stp_map_key2 (mymap, "six", "10");
  printf ("mymap[six,10]=%s\n", map_get_str(mymap));

  map_dump(mymap);

  _stp_map_key2 (mymap, "two-two-one-B", "Baker Street"); map_key_del (mymap);
  map_dump(mymap);


  _stp_map_key2 (mymap, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL); 
  _stp_map_set (mymap, "TESTING 1,2,3");

  _stp_map_key2 (mymap, "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZhiasdhgfiudsgfiusdgfisdugfisdugfsdiufgsdfiugsdifugsdiufgsdiufgisdugfisdugfigsdfiusdgfiugsdifu sdfigsdifugsdifugsdiufgsdiufgisdugfiudsgfisudgfiusdgfiusdgfisdugfisdufgiusdfgsdiufgdsiufgsdiufgsdiufgsdiufgwiugfw89e4rf98yf897ywef98wyef98wyf98wyf89ys9d8yfsd sdfysd98fy9s8fyds98fy98dsfy89sdfy", "yw98fty98sfts98d7fts89d7f9sdfoooooooooooooooooooooooooooooooooooooooooooof8eo7stfew87fwet8tw87rf7fpowft7ewfptpwefpwetfpwepwfwetfp8we");

  _stp_map_set (mymap, "TESTING 1,2,3 ***************************************************************************************************************************************************************************************************************************************************************************** 4,5,6");
  map_dump(mymap);

  /* add 4 new entries, pushing "Ohio" out */
  int i;
  for (i = 2; i < 6; i++)
    {
      char buf[32], buf2[32], buf3[32];
      sprintf (buf, "test_number_%d", i);
      sprintf (buf2, "TEST_NUMBER_%d", i*i);
      sprintf (buf3, "TEST_NUMBER_%d", i*i*i);
      _stp_map_key2 (mymap, buf, buf2);
      _stp_map_set_str (mymap, buf3);
    }
  map_dump (mymap);

  /* delete all entries */
  for (i = 2; i < 6; i++)
    {
      char buf[32], buf2[32], buf3[32];
      sprintf (buf, "test_number_%d", i);
      sprintf (buf2, "TEST_NUMBER_%d", i*i);
      sprintf (buf3, "TEST_NUMBER_%d", i*i*i);
      _stp_map_key2 (mymap, buf, buf2);
      map_key_del (mymap);
    }

  printf ("Should be empty: ");
  map_dump (mymap);
  _stp_map_del (mymap);
  return 0;
}
