/* -*- linux-c -*- 
 * Map of addresses to disallow.
 * Copyright (C) 2005-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ADDR_MAP_C_
#define _ADDR_MAP_C_ 1

/** @file addr-map
 * @brief Implements functions used by the deref macro to blacklist
 * certain addresses.
 */

struct addr_map_entry
{
  unsigned long min;
  unsigned long max;
};

struct addr_map
{
  size_t size;
  struct addr_map_entry entries[0];
};

static DEFINE_SPINLOCK(addr_map_lock);

struct addr_map* blackmap;

/* Find address of entry where we can insert a new one. */
static size_t
upper_bound(unsigned long addr, struct addr_map* map)
{
  size_t start = 0;
  size_t end = map->size;
  struct addr_map_entry *entry = 0;

  if (end == 0)
    return 0;
  do
    {
      size_t new_idx;
      if (addr < map->entries[start].min)
        return start;
      if (addr >= map->entries[end-1].max)
        return end;
      new_idx = (end + start) / 2;
      entry = &map->entries[new_idx];
      if (addr < entry->min)
        end = new_idx;
      else
        start = new_idx + 1;
    } while (end != start);
  return entry - &map->entries[0];
}

static struct addr_map_entry*
lookup_addr_aux(unsigned long addr, struct addr_map* map)
{
  size_t start = 0;
  size_t end;
  if (!map)
    return 0;
  end = map->size;
  if (map->size == 0)
    return 0;

  do
    {
      int entry_idx;
      struct addr_map_entry *entry = 0;
      if (addr < map->entries[start].min || addr >= map->entries[end - 1].max)
        return 0;
      entry_idx = (end + start) / 2;
      entry = &map->entries[entry_idx];
      if (entry->min <= addr && entry->max > addr)
        return entry;
      if (addr < entry->min)
        end = entry_idx;
      else
        start = entry_idx + 1;
    } while (start < end);
  return 0;
}

int
lookup_bad_addr(unsigned long addr)
{
  struct addr_map_entry* result = 0;
  spin_lock(&addr_map_lock);
  result = lookup_addr_aux(addr, blackmap);
  spin_unlock(&addr_map_lock);
  if (result)
    return 1;
  else
    return 0;
}


int
add_bad_addr_entry(unsigned long min_addr, unsigned long max_addr,
                   struct addr_map_entry** existing_min,
                   struct addr_map_entry** existing_max)
{
  struct addr_map* new_map = 0;
  struct addr_map* old_map = 0;
  struct addr_map_entry* min_entry = 0;
  struct addr_map_entry* max_entry = 0;
  struct addr_map_entry* new_entry = 0;
  size_t existing = 0;

  /* Loop allocating memory for a new entry in the map. */
  while (1)
    {
      size_t old_size = 0;
      spin_lock(&addr_map_lock);
      old_map = blackmap;
      if (old_map)
        old_size = old_map->size;
      /* Either this is the first time through the loop, or we
         allocated a map previous time, but someone has come in and
         added an entry while we were sleeping. */
      if (!new_map || (new_map && new_map->size < old_size + 1))
        {
          spin_unlock(&addr_map_lock);
          if (new_map)
            {
              kfree(new_map);
              new_map = 0;
            }
          new_map = kmalloc(sizeof(*new_map)
                            + sizeof(*new_entry) * (old_size + 1),
                            GFP_KERNEL);
          if (!new_map)
            return -ENOMEM;
          new_map->size = old_size + 1;
        }
      else
        break;
    }
  if (!blackmap)
    {
      existing = 0;
    }
  else
    {
      min_entry = lookup_addr_aux(min_addr, blackmap);
      max_entry = lookup_addr_aux(max_addr, blackmap);
      if (min_entry || max_entry)
        {
          if (existing_min)
            *existing_min = min_entry;
          if (existing_max)
            *existing_max = max_entry;
          spin_unlock(&addr_map_lock);
          kfree(new_map);
          return 1;
        }
      existing = upper_bound(min_addr, old_map);
    }
  new_entry = &new_map->entries[existing];
  new_entry->min = min_addr;
  new_entry->max = max_addr;
  if (old_map)
    {
      memcpy(&new_map->entries, old_map->entries,
             existing * sizeof(*new_entry));
      if (old_map->size > existing)
        memcpy(new_entry + 1, &old_map->entries[existing],
               (old_map->size - existing) * sizeof(*new_entry));
    }
  blackmap = new_map;
  spin_unlock(&addr_map_lock);
  if (old_map)
    kfree(old_map);
  return 0;
}

void
delete_bad_addr_entry(struct addr_map_entry* entry)
{
}

#endif
