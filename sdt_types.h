/* Copyright (C) 2005-2011 Red Hat Inc.

   This file is part of systemtap, and is free software in the public domain.
*/

#ifndef _SDT_TYPES_H
#define _SDT_TYPES_H    1

#define UPROBE1_TYPE 0x31425250 /* "PRB1" (little-endian) */
#define UPROBE2_TYPE 0x32425055 /* "UPB2" */
#define UPROBE3_TYPE 0x33425055 /* "UPB3" */

typedef enum
  {
    uprobe1_type = UPROBE1_TYPE,
    uprobe2_type = UPROBE2_TYPE,
    uprobe3_type = UPROBE3_TYPE,
    unknown_probe_type = -1,
  } stap_sdt_probe_type;

typedef struct
{
  __uint32_t type_a;
  __uint32_t type_b;
  __uint64_t name;
  __uint64_t arg;
}  stap_sdt_probe_entry_v1;

typedef struct
{
  __uint32_t type_a;
  __uint32_t type_b;
  __uint64_t name;
  __uint64_t provider;
  __uint64_t arg_count;
  __uint64_t arg_string;
  __uint64_t pc;
  __uint64_t semaphore;
}   stap_sdt_probe_entry_v2;

#endif /* _SDT_TYPES_H */

