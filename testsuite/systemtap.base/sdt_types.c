#include "sys/sdt.h"
#include <stdint.h>
#include <values.h>

struct opaque;

int
main (int argc, char **argv)
{

  char char_var = '~';
  const char const_char_var = '!';
  volatile char volatile_char_var = '!';
  char *ptr_char_var = &char_var;
  const char *ptr_const_char_var = &char_var;
  char * const char_ptr_const_var = &char_var;
  volatile char *ptr_volatile_char_var = &char_var;
  char * volatile char_ptr_volatile_var = &char_var;

  short int short_int_var = 32767;
  const short int const_short_int_var = -32767;
  volatile short int volatile_short_int_var = -32767;
  short int *ptr_short_int_var = &short_int_var;
  const short int *ptr_const_short_int_var = &short_int_var;
  short int * const short_int_ptr_const_var = &short_int_var;
  volatile short int *ptr_volatile_short_int_var = &short_int_var;
  short int * volatile short_int_ptr_volatile_var = &short_int_var;
  unsigned short int short_uint_var = (unsigned short)0xffff8001;

  int int_var = 65536;
  const int const_int_var = -65536;
  volatile int volatile_int_var = -65536;
  int *ptr_int_var = &int_var;
  const int *ptr_const_int_var = &int_var;
  int * const int_ptr_const_var = &int_var;
  volatile int *ptr_volatile_int_var = &int_var;
  int * volatile int_ptr_volatile_var = &int_var;
  unsigned int uint_var = (unsigned int)0xffff8001;

  long int long_int_var = 65536;
  const long int const_long_int_var = -65536;
  volatile long int volatile_long_int_var = -65536;
  long int *ptr_long_int_var = &long_int_var;
  const long int *ptr_const_long_int_var = &long_int_var;
  long int * const long_int_ptr_const_var = &long_int_var;
  volatile long int *ptr_volatile_long_int_var = &long_int_var;
  long int * volatile long_int_ptr_volatile_var = &long_int_var;

  /* c89 doesn't define __STDC_VERSION. With -pedantic warns about long long. */
#if ! defined NO_LONG_LONG && __SIZEOF_SIZE_T__ == 8
  long long int long_long_int_var = 65536;
  const long long int const_long_long_int_var = -65536;
  volatile long long int volatile_long_long_int_var = -65536;
  long long int *ptr_long_long_int_var = &long_long_int_var;
  const long long int *ptr_const_long_long_int_var = &long_long_int_var;
  long long int * const long_long_int_ptr_const_var = &long_long_int_var;
  volatile long long int *ptr_volatile_long_long_int_var = &long_long_int_var;
  long long int * volatile long_long_int_ptr_volatile_var = &long_long_int_var;
#endif

#if defined(STAP_SDT_V1)
#define ARRAY(x) (&(x)[0])
#else
#define ARRAY(x) (x)
#endif
  char arr_char [] = "!~";

  struct {
    unsigned int bit1_0:1;
    unsigned int bit1_1:1;
    char char_2;
    unsigned int bit1_6:1;
    unsigned int bit1_7:1;
    char char_8;
    unsigned int bit1_9:1;
    unsigned int bit1_10:1;
  } bitfields_a_var = { 1, 0, 'a', 1, 0, 'z', 1, 0};

  struct {
    unsigned char char_0;
    int bit1_4:1;
    unsigned int bit1_5:1;
    int bit2_6:2;
    unsigned int bit2_8:2;
    int bit3_10:3;
    unsigned int bit3_13:3;
    int bit9_16:9;
    unsigned int bit9_25:9;
    char char_34;
  } bitfields_b_var = {'A', -1, 1, 1, 3, 3, 7, 255, 511, 'Z'};

# if !defined(__cplusplus) || \
	((__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)) && __GXX_EXPERIMENTAL_CXX0X__)
  struct {
    int int_var;
  } arr_struct [2] = {{1},{2}};

  enum  {
    red = 0,
    green = 1,
    blue = 2
  } primary_colors_var = green;

  struct opaque_struct *incomplete_struct_type = 0;
# endif

  /* gnu90 and gnu99 don't support this so for now don't test it
     enum opaque_enum *incomplete_enum_type = 0; */

  /* char */
  STAP_PROBE1(provider,char_var,char_var);
  STAP_PROBE1(provider,const_char_var,const_char_var);
  STAP_PROBE1(provider,volatile_char_var,volatile_char_var);
  STAP_PROBE3(provider,ptr_char_var,ptr_char_var,&char_var,char_var);
  STAP_PROBE2(provider,ptr_const_char_var,ptr_const_char_var,&char_var);
  STAP_PROBE2(provider,char_ptr_const_var,char_ptr_const_var,&char_var);
  STAP_PROBE2(provider,ptr_volatile_char_var,ptr_volatile_char_var,&char_var);
  STAP_PROBE2(provider,char_ptr_volatile_var,char_ptr_volatile_var,&char_var);

  /* short */
  STAP_PROBE1(provider,short_int_var,short_int_var);
  STAP_PROBE1(provider,const_short_int_var,const_short_int_var);
  STAP_PROBE1(provider,volatile_short_int_var,volatile_short_int_var);
  STAP_PROBE3(provider,ptr_short_int_var,ptr_short_int_var,&short_int_var, short_int_var);
  STAP_PROBE2(provider,ptr_const_short_int_var,ptr_const_short_int_var,&short_int_var);
  STAP_PROBE2(provider,short_int_ptr_const_var,short_int_ptr_const_var,&short_int_var);
  STAP_PROBE2(provider,ptr_volatile_short_int_var,ptr_volatile_short_int_var,&short_int_var);
  STAP_PROBE2(provider,short_int_ptr_volatile_var,short_int_ptr_volatile_var,&short_int_var);
  STAP_PROBE3(provider,unsigned_short_int_var,short_uint_var, 0x8001, &short_uint_var);

  /* int */
  STAP_PROBE1(provider,int_var,int_var);
  STAP_PROBE1(provider,const_int_var,const_int_var);
  STAP_PROBE1(provider,volatile_int_var,volatile_int_var);
  STAP_PROBE3(provider,ptr_int_var,ptr_int_var,&int_var,int_var);
  STAP_PROBE2(provider,ptr_const_int_var,ptr_const_int_var,&int_var);
  STAP_PROBE2(provider,int_ptr_const_var,int_ptr_const_var,&int_var);
  STAP_PROBE2(provider,ptr_volatile_int_var,ptr_volatile_int_var,&int_var);
  STAP_PROBE2(provider,int_ptr_volatile_var,int_ptr_volatile_var,&int_var);
  STAP_PROBE3(provider,unsigned_int_var,uint_var, 0x8001, &uint_var);

  /* long */
  STAP_PROBE1(provider,long_int_var,long_int_var);
  STAP_PROBE1(provider,const_long_int_var,const_long_int_var);
  STAP_PROBE1(provider,volatile_long_int_var,volatile_long_int_var);
  STAP_PROBE2(provider,ptr_long_int_var,ptr_long_int_var,&long_int_var);
  STAP_PROBE2(provider,ptr_const_long_int_var,ptr_const_long_int_var,&long_int_var);
  STAP_PROBE2(provider,long_int_ptr_const_var,long_int_ptr_const_var,&long_int_var);
  STAP_PROBE2(provider,ptr_volatile_long_int_var,ptr_volatile_long_int_var,&long_int_var);
  STAP_PROBE2(provider,long_int_ptr_volatile_var,long_int_ptr_volatile_var,&long_int_var);

  /* long long */
#if ! defined NO_LONG_LONG && __SIZEOF_SIZE_T__ == 8
  STAP_PROBE1(provider,long_long_int_var,long_long_int_var);
  STAP_PROBE1(provider,const_long_long_int_var,const_long_long_int_var);
  STAP_PROBE1(provider,volatile_long_long_int_var,volatile_long_long_int_var);
  STAP_PROBE3(provider,ptr_long_long_int_var,ptr_long_long_int_var,&long_long_int_var,long_long_int_var);
  STAP_PROBE2(provider,ptr_const_long_long_int_var,ptr_const_long_long_int_var,&long_long_int_var);
  STAP_PROBE2(provider,long_long_int_ptr_const_var,long_long_int_ptr_const_var,&long_long_int_var);
  STAP_PROBE2(provider,ptr_volatile_long_long_int_var,ptr_volatile_long_long_int_var,&long_long_int_var);
  STAP_PROBE2(provider,long_long_int_ptr_volatile_var,long_long_int_ptr_volatile_var,&long_long_int_var);
#endif

  STAP_PROBE1(provider,arr_char,ARRAY(arr_char));
# if !defined(__cplusplus) || \
	((__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)) && __GXX_EXPERIMENTAL_CXX0X__)
  STAP_PROBE1(provider,arr_struct,ARRAY(arr_struct));
# endif

  STAP_PROBE8(provider,bitfields_a_var,
	      (int)bitfields_a_var.bit1_0,
	      (int)bitfields_a_var.bit1_1,
	      bitfields_a_var.char_2,
	      (int)bitfields_a_var.bit1_6,
	      (int)bitfields_a_var.bit1_7,
	      bitfields_a_var.char_8,
	      (int)bitfields_a_var.bit1_9,
	      (int)bitfields_a_var.bit1_10);

  STAP_PROBE10(provider,bitfields_b_var,bitfields_b_var.char_0,
	       (int)bitfields_b_var.bit1_4,
	       (int)bitfields_b_var.bit1_5,
	       (int)bitfields_b_var.bit2_6,
	       (int)bitfields_b_var.bit2_8,
	       (int)bitfields_b_var.bit3_10,
	       (int)bitfields_b_var.bit3_13,
	       (int)bitfields_b_var.bit9_16,
	       (int)bitfields_b_var.bit9_25,
	       bitfields_b_var.char_34);

# if !defined(__cplusplus) || \
	((__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)) && __GXX_EXPERIMENTAL_CXX0X__)
  STAP_PROBE1(provider,primary_colors_var,primary_colors_var);
# endif

  STAP_PROBE3(provider,constants,0x7fffffff,'~',"constants");

# if !defined(__cplusplus) || \
	((__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)) && __GXX_EXPERIMENTAL_CXX0X__)
  STAP_PROBE1(provider, incomplete_struct_type, incomplete_struct_type);
# endif

  STAP_PROBE(provider,something__dash__dash__something);

  return 0;
}
