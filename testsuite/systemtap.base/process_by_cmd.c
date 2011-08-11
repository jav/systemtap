#include "stdio.h"
#include "sys/sdt.h"

void third(){}
void second(){third();}
void first(){second();}

int main()
{
  l1:
    STAP_PROBE(process_by_cmd, main_start);
  first();
  STAP_PROBE(process_by_cmd, main_end);
  return 0;
}
