#include "sdt.h" /* Really <sys/sdt.h>, but pick current source version. */
#include <stdint.h>
#include <values.h>

int
main (int argc, char **argv) 
{
  char char_var = '~';
  STAP_PROBE1(provider,char_var,char_var);
  const char const_char_var = '!';
  STAP_PROBE1(provider,const_char_var,const_char_var);
  volatile char volatile_char_var = '!';
  STAP_PROBE1(provider,volatile_char_var,volatile_char_var);
  char *ptr_char_var = &char_var;
  STAP_PROBE2(provider,ptr_char_var,ptr_char_var,&char_var);
  const char *ptr_const_char_var = &char_var;
  STAP_PROBE2(provider,ptr_const_char_var,ptr_const_char_var,&char_var);
  char * const char_ptr_const_var = &char_var;
  STAP_PROBE2(provider,char_ptr_const_var,char_ptr_const_var,&char_var);
  volatile char *ptr_volatile_char_var = &char_var;
  STAP_PROBE2(provider,ptr_volatile_char_var,ptr_volatile_char_var,&char_var);
  char * volatile char_ptr_volatile_var = &char_var;
  STAP_PROBE2(provider,char_ptr_volatile_var,char_ptr_volatile_var,&char_var);
  short int short_int_var = 32767;
  STAP_PROBE1(provider,short_int_var,short_int_var);
  const short int const_short_int_var = -32767;
  STAP_PROBE1(provider,const_short_int_var,const_short_int_var);
  volatile short int volatile_short_int_var = -32767;
  STAP_PROBE1(provider,volatile_short_int_var,volatile_short_int_var);
  short int *ptr_short_int_var = &short_int_var;
  STAP_PROBE2(provider,ptr_short_int_var,ptr_short_int_var,&short_int_var);
  const short int *ptr_const_short_int_var = &short_int_var;
  STAP_PROBE2(provider,ptr_const_short_int_var,ptr_const_short_int_var,&short_int_var);
  short int * const short_int_ptr_const_var = &short_int_var;
  STAP_PROBE2(provider,short_int_ptr_const_var,short_int_ptr_const_var,&short_int_var);
  volatile short int *ptr_volatile_short_int_var = &short_int_var;
  STAP_PROBE2(provider,ptr_volatile_short_int_var,ptr_volatile_short_int_var,&short_int_var);
  short int * volatile short_int_ptr_volatile_var = &short_int_var;
  STAP_PROBE2(provider,short_int_ptr_volatile_var,short_int_ptr_volatile_var,&short_int_var);
  int int_var = 65536;
  STAP_PROBE1(provider,int_var,int_var);
  const int const_int_var = -65536;
  STAP_PROBE1(provider,const_int_var,const_int_var);
  volatile int volatile_int_var = -65536;
  STAP_PROBE1(provider,volatile_int_var,volatile_int_var);
  int *ptr_int_var = &int_var;
  STAP_PROBE2(provider,ptr_int_var,ptr_int_var,&int_var);
  const int *ptr_const_int_var = &int_var;
  STAP_PROBE2(provider,ptr_const_int_var,ptr_const_int_var,&int_var);
  int * const int_ptr_const_var = &int_var;
  STAP_PROBE2(provider,int_ptr_const_var,int_ptr_const_var,&int_var);
  volatile int *ptr_volatile_int_var = &int_var;
  STAP_PROBE2(provider,ptr_volatile_int_var,ptr_volatile_int_var,&int_var);
  int * volatile int_ptr_volatile_var = &int_var;
  STAP_PROBE2(provider,int_ptr_volatile_var,int_ptr_volatile_var,&int_var);
  long int long_int_var = 65536;
  STAP_PROBE1(provider,long_int_var,long_int_var);
  const long int const_long_int_var = -65536;
  STAP_PROBE1(provider,const_long_int_var,const_long_int_var);
  volatile long int volatile_long_int_var = -65536;
  STAP_PROBE1(provider,volatile_long_int_var,volatile_long_int_var);
  long int *ptr_long_int_var = &long_int_var;
  STAP_PROBE2(provider,ptr_long_int_var,ptr_long_int_var,&long_int_var);
  const long int *ptr_const_long_int_var = &long_int_var;
  STAP_PROBE2(provider,ptr_const_long_int_var,ptr_const_long_int_var,&long_int_var);
  long int * const long_int_ptr_const_var = &long_int_var;
  STAP_PROBE2(provider,long_int_ptr_const_var,long_int_ptr_const_var,&long_int_var);
  volatile long int *ptr_volatile_long_int_var = &long_int_var;
  STAP_PROBE2(provider,ptr_volatile_long_int_var,ptr_volatile_long_int_var,&long_int_var);
  long int * volatile long_int_ptr_volatile_var = &long_int_var;
  STAP_PROBE2(provider,long_int_ptr_volatile_var,long_int_ptr_volatile_var,&long_int_var);
  long long int long_long_int_var = 65536;
  STAP_PROBE1(provider,long_long_int_var,long_long_int_var);
  const long long int const_long_long_int_var = -65536;
  STAP_PROBE1(provider,const_long_long_int_var,const_long_long_int_var);
  volatile long long int volatile_long_long_int_var = -65536;
  STAP_PROBE1(provider,volatile_long_long_int_var,volatile_long_long_int_var);
  long long int *ptr_long_long_int_var = &long_long_int_var;
  STAP_PROBE2(provider,ptr_long_long_int_var,ptr_long_long_int_var,&long_long_int_var);
  const long long int *ptr_const_long_long_int_var = &long_long_int_var;
  STAP_PROBE2(provider,ptr_const_long_long_int_var,ptr_const_long_long_int_var,&long_long_int_var);
  long long int * const long_long_int_ptr_const_var = &long_long_int_var;
  STAP_PROBE2(provider,long_long_int_ptr_const_var,long_long_int_ptr_const_var,&long_long_int_var);
  volatile long long int *ptr_volatile_long_long_int_var = &long_long_int_var;
  STAP_PROBE2(provider,ptr_volatile_long_long_int_var,ptr_volatile_long_long_int_var,&long_long_int_var);
  long long int * volatile long_long_int_ptr_volatile_var = &long_long_int_var;
  STAP_PROBE2(provider,long_long_int_ptr_volatile_var,long_long_int_ptr_volatile_var,&long_long_int_var);

  char arr_char [2] = "!~";
  STAP_PROBE1(provider,arr_char,&arr_char);
  struct {
    int int_var;
  } arr_struct [2] = {{
      .int_var=1,
    },{
      .int_var=2,
    }};
  STAP_PROBE1(provider,arr_struct,&arr_struct);
  struct {
    unsigned int bit1_0:1;
    unsigned int bit1_1:1;
    char char_2;
    unsigned int bit1_6:1;
    unsigned int bit1_7:1;
    char char_8;
    unsigned int bit1_9:1;
    unsigned int bit1_10:1;
  } bitfields_small_var = {
    .bit1_0=1,
    .bit1_1=0,
    .char_2='a',
    .bit1_6=1,
    .bit1_7=0,
    .char_8='z',
    .bit1_9=1,
    .bit1_10=0,
  };
  STAP_PROBE8(provider,bitfields_small_var,
	      (int)bitfields_small_var.bit1_0,
	      (int)bitfields_small_var.bit1_1,
	      bitfields_small_var.char_2,
	      (int)bitfields_small_var.bit1_6,
	      (int)bitfields_small_var.bit1_7,
	      bitfields_small_var.char_8,
	      (int)bitfields_small_var.bit1_9,
	      (int)bitfields_small_var.bit1_10);
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
  } bitfields_bit_var = {
    .char_0='A',
    .bit1_4=-1,
    .bit1_5=1,
    .bit2_6=1,
    .bit2_8=3,
    .bit3_10=3,
    .bit3_13=7,
    .bit9_16=255,
    .bit9_25=511,
    .char_34='Z',
  };
  STAP_PROBE10(provider,bitfields_bit_var,bitfields_bit_var.char_0,
	       (int)bitfields_bit_var.bit1_4,
	       (int)bitfields_bit_var.bit1_5,
	       (int)bitfields_bit_var.bit2_6,
	       (int)bitfields_bit_var.bit2_8,
	       (int)bitfields_bit_var.bit3_10,
	       (int)bitfields_bit_var.bit3_13,
	       (int)bitfields_bit_var.bit9_16,
	       (int)bitfields_bit_var.bit9_25,
	       bitfields_bit_var.char_34);
  enum  {
    red = 0,
    green = 1,
    blue = 2
  } primary_colors_var = green;
  STAP_PROBE1(provider,primary_colors_var,primary_colors_var);
  return 0;
}

