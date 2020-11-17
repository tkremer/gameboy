/*

  eeprom functions and simple eepromfs file system.

  Copyright (c) 2016 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef __EEPROM_H__
#define __EEPROM_H__

#include <avr/io.h>
#include <avr/eeprom.h>
#include <util/atomic.h>
#include <stdint.h>
#include <stdbool.h>

#define eeprom_execute_write()  \
  asm volatile ("sbi %0,%1\n\t" \
      "sbi %0,%2" ::            \
      "i" (_SFR_IO_ADDR(EECR)), \
       "i" (EEMPE),             \
        "i" (EEPE):)

// FIXME: Do we need to set value ^= oldval^0xff for write-only?
void eeprom_put_byte(size_t addr, uint8_t value)
{
  uint8_t oldval;
  uint8_t mode;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    eeprom_busy_wait();
    oldval = eeprom_read_byte((uint8_t*)addr);
    if (value & ~oldval) {  // bits need to be erased
      mode = (value == 0xff) ? 1 : 0; // erase only vs. erase+write
    } else {
      if (value == oldval) // nothing to be done
        return;
      mode = 2;            // write only
      value |= ~oldval; // TODO: do we actually need this? Does it work?
    }
    EEAR = addr; // we cannot rely on eeprom_read_byte to leave it alone.
    EEDR = value;
    EECR = mode << EEPM0;
    eeprom_execute_write();
  }
}

void eeprom_erase_byte(size_t addr)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    eeprom_busy_wait();
    EEAR = addr;
    EECR = 1 << EEPM0;
    eeprom_execute_write();
  }
}

void eeprom_put(size_t dest, void* src, int count)
{
  uint8_t *_src = (uint8_t*)src;
  for (int i = 0; i < count; i++)
    eeprom_put_byte(dest+i,_src[i]);
}

/*

  The eepromfs functions implement a simple wear-levelling file system for
  the eeprom. Up to 255 variables of EEPROMFS_BLOCKSIZE bytes each can be
  stored (given enough space), each identified by a number (0..254).

  Updating a variable means erasing its old location and writing it to a new
  location, preceded by its number (which marks the location as "occupied".
  The number 0xff outside any block marks a free byte (ready for writing to
  the eeprom without erasing), enough consecutive such numbers form a free
  block to write to.
  Only (blocksize+1)-aligned blocks are allowed.

  Since the variables may be stored at any place in the file system, it is
  recommended to make an index before use. If no index is to be used, NULL may
  be provided as index parameter.

  example for a blocksize of 4:

  FF FF FF FF FF 01 12 34 56 78 FF FF FF FF FF 00 CA FE BA FF FF FF FF FF
  <--  free  --><ix><- var 1 -><--   free  --><ix><- var 0 -> <- free  ->
                 ^                    ^
     index[1] --/         index[0] --/

  Note that all variables that need to be indexed should exist exactly once.
  Additional files may exist but will consume (unretrievable) memory.

  eeprom memory consumption is vars*(1+blocksize) (plus 1+blocksize for a free block for the next write operation)
  Note that the wear-levelling effect relies on sparse usage of the file system.

  ATmega328P has 1024 bytes eeprom ( -> 203 32-bit variables)
  AT90USB162 has 512 bytes eeprom  ( -> 101 32-bit variables)
*/


#ifndef EEPROMFS_BLOCKSIZE
#define EEPROMFS_BLOCKSIZE 4
#endif

// FIXME: hard-coded eeprom sizes for controllers should be provided in a standard header file. (but are not)
// FIXME: Found it: It's called "E2END" and contains the last eeprom location.
//        TODO: test it.
#ifndef EEPROM_SIZE
#  if defined(__AVR_ATmega328P__)
#    define EEPROM_SIZE 1024
#  elif defined(__AVR_AT90USB162__)
#    define EEPROM_SIZE 512
#  endif
#endif

// EEPROMFS_VARS may define the number of variables, so the index structure can
// allocate that many slots.
#ifndef EEPROMFS_INDEX_SIZE
#  ifdef EEPROMFS_VARS
#    define EEPROMFS_INDEX_SIZE EEPROMFS_VARS
#  else
#    define EEPROMFS_INDEX_SIZE 0
#  endif
#endif

// if EEPROMFS_VARS and EEPROMFS_INDEX_SIZE were not defined, this would simply
// be the header member of a larger index structure.
// why this is a struct: because there were more members once and we may put
// more members in there later if it seems suitable.
typedef struct {
  size_t index[EEPROMFS_INDEX_SIZE]; // any undefined index will be 0. (no marker space in front)
} eepromfs_index_t;

/*

#define EEPROM_SIZE sizeof(int32_t)
#define EEPROM_VARS 4
eepromfs_index_t eep_index;
#include <eeprom.h>
#define eevar1 0
#define eevar2 1

int32_t v1 = v1_default;
eepromfs_get(eep_index,eevar1,&v1);

*/

// returns true, if all variables were found.
// if fsck==true, erases any superfluous variables and blocks.
bool eepromfs_index(eepromfs_index_t* index, int count, bool fsck)
{
  size_t addr = 0;
  int counted = 0;
  int blocksize = EEPROMFS_BLOCKSIZE;
  if (count == 0)
    count = EEPROMFS_INDEX_SIZE;
  for (int i = 0; i < count; i++)
    index->index[i] = 0;
  while ((fsck || counted < count) && addr <= EEPROM_SIZE-(1+blocksize)) {
    uint8_t val = eeprom_read_byte((void*)addr);
    if (val != 0xff && val < count && index->index[val] == 0) {
      // we want the first match for consistency with eepromfs_find.
      index->index[val] = addr+1;
      counted++;
    } else if (fsck) {
      // val != 0xff: superfluous variable found. Erase.
      // val == 0xff: check the whole block to be erased.
      if (val != 0xff)
        eeprom_erase_byte(addr);
      for (int i = 0; i < blocksize; i++)
        eeprom_put_byte(addr+i,0xff); // erase if neccessary
    }
    addr += blocksize+1;
      // we only accept well-aligned blocks. Saves us in access times.
  }
  return counted == count;
}

void eepromfs_fsck(int count)
{
  struct {
    eepromfs_index_t index;
    size_t v[count];
  } ix;
  eepromfs_index(&ix.index,count,true);
}

// searches the eeprom for a variable. Returns the first matching offset.
size_t eepromfs_find(uint8_t var)
{
  size_t addr = 0;
  int blocksize = EEPROMFS_BLOCKSIZE;
  while (addr <= EEPROM_SIZE-(1+blocksize)) {
    uint8_t val = eeprom_read_byte((void*)addr);
    if (val != 0xff) {
      if (val == var) {
        return addr+1;
      }
    }
    addr += blocksize+1;
  }
  return 0;
}

static inline size_t eepromfs_getaddr(eepromfs_index_t* index, uint8_t var)
{
  return index == NULL ? eepromfs_find(var) : index->index[var];
}

// returns true iff variable is defined (and loads it into dest).
bool eepromfs_get(eepromfs_index_t* index, uint8_t var, void* dest)
{
  int blocksize = EEPROMFS_BLOCKSIZE;
  size_t addr = eepromfs_getaddr(index,var);
  if (addr == 0)
    return false;
    // nothing stored. Prepopulate *dest with a default before calling.
  eeprom_read_block(dest,(void*)addr,blocksize);
  return true;
}

// FIXED: find the eeprom end, restart from beginning, skip used blocks.
// FIXED: start searching at the point after index[var].
// DONE: Would it be better if we aligned to (1+blocksize)-blocks?
// TODO: How do we handle device shutdowns in mid-write?
//   -> write new, delete after. If we assume that a write always writes one of
//   {old,new,0xff}, we will always have a recoverable state (with fsck=1).
void eepromfs_put(eepromfs_index_t* index, uint8_t var, void* value)
{
  int blocksize = EEPROMFS_BLOCKSIZE;
  int chunksize = blocksize+1;
  size_t old = eepromfs_getaddr(index,var);
  if (old != 0) {
    // a value is already there. Check if it is different and if we can
    // overwrite it without erasing.
    uint8_t *_val = (uint8_t*)value;
    bool neednew = false, changed = false;
    for (int i = 0; i < blocksize; i++) {
      uint8_t val = _val[i];
      uint8_t oldval = eeprom_read_byte((void*)old+i);
      if (val != oldval) {
        changed = true;
        if (val & ~oldval) {
          neednew = true;
          break;
        }
      }
    }
    if (!neednew) {
      if (changed)
        eeprom_put(old,value,blocksize); // will overwrite without erasing.
      return;
    }
  }

  size_t next = old != 0 ? old+blocksize : 0; // FIXME: use rand() when old==0.
  if ((next+chunksize) > EEPROM_SIZE) {
    next = 0;
  }
  int free = 0;
  while (free < chunksize) {
    uint8_t x = eeprom_read_byte((void*)(next+free));
    if (x == 0xff) {
      free++;
    } else {
      next = next+chunksize;
      if ((next+chunksize) > EEPROM_SIZE) {
        next = 0;
      }
      free = 0;
    }
  }
  // DONE: check if we can overwrite without erasing
  // first the data, then the marker -> more atomic.
  eeprom_put(next+1,value,blocksize);
   // -> additional dirty empty cell. (Even in mid-failure case)
  eeprom_put_byte(next,var);
   // -> additional clean cell for same var.
   // -> mid-failure case: different cell iff written value may be arbitrary.
  next++;
  if (old != 0) {
    eeprom_erase_byte(old-1); // This one has to be erased unconditionally
    // -> now additional dirty empty cell again.
   // -> mid-failure case: different cell iff written value may be arbitrary.
    for (int i = 0; i < blocksize; i++)
      eeprom_put_byte(old+i,0xff); // erase if neccessary
      //eeprom_erase_byte(old+i);
  }
  if (index != NULL)
    index->index[var] = next;
}

// Note that deleting a variable thwarts wear-levelling efforts
// because we do not remember where it was saved before.
bool eepromfs_delete(eepromfs_index_t* index, uint8_t var)
{
  int blocksize = EEPROMFS_BLOCKSIZE;
  size_t addr = eepromfs_getaddr(index,var);
  if (addr == 0)
    return false;
  for (int i = -1; i < blocksize; i++)
    eeprom_put_byte(addr+i,0xff); // erase if neccessary
    //eeprom_erase_byte(addr+i);
  if (index != NULL)
    index->index[var] = 0;
  return true;
}

#endif
