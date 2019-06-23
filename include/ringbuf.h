/*

  Ringbuffer implementation.
  Never used...

  Copyright (c) 2015 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef __RINGBUF_H__
#define __RINGBUF_H__

// manage a ring buffer
typedef struct ringbuf_header_t {
  //uint8_t size;
  uint8_t start, count;
  uint8_t buffer[0];
} ringbuf_header_t;

typedef void ringbuf_t;

#define declare_ringbuf(name,size) struct { ringbuf_header_t hdr; uint8_t buf[size]; } name
// struct ringbuf_##name_##size_t : ringbuf_t { uint8_t buf[size]; } name;

static void ringbuf_write(ringbuf_t* _buf, const uint8_t* data, uint8_t size, uint8_t bufsize)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  if (size == 0) return;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    uint8_t start = buf->start;
    uint8_t count = buf->count;
    uint8_t end = (start+count) % bufsize;
    uint8_t newend;
    if (size >= bufsize) {
      uint8_t oversize = size-bufsize;
      data += oversize;
      memcpy(buf->buffer,data,bufsize);
      buf->start = 0;
      buf->count = bufsize;
      newend = bufsize;
      return;
    } else if ((uint16_t)count+size > bufsize) {
      newend = (start+count+size) % bufsize;
      buf->start = newend;
      buf->count = bufsize;
    } else {
      buf->count = count+size;
      newend = (end+size) % bufsize;
    }
    if (newend == 0)
      newend = bufsize;
    if (newend > end) {
      memcpy(buf->buffer+end,data,size);
    } else {
      memcpy(buf->buffer+end,data,bufsize-end);
      memcpy(buf->buffer,data+(bufsize-end),newend);
    }
  }
}

static void ringbuf_write_byte(ringbuf_t* _buf, uint8_t data, uint8_t bufsize)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    uint8_t start = buf->start;
    uint8_t count = buf->count;
    uint8_t end = (start+count) % bufsize;
    if (count == bufsize) {
      uint8_t newend = (end+1) % bufsize;
      buf->start = newend;
    } else {
      buf->count = count+1;
    }
    buf->buffer[end] = data;
  }
}

// get a pointer to a number of bytes in the buffer and the number of bytes
// that are readable from there.
static uint8_t* ringbuf_peek(ringbuf_t* _buf, uint8_t* size, uint8_t bufsize)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    uint8_t start = buf->start;
    uint8_t count = buf->count;
    if (count > bufsize-start) {
      *size = bufsize-start;
    } else {
      *size = count;
    }
    return buf->buffer+start;
  }
  return NULL; // makes compiler happy...
}

static void ringbuf_drop(ringbuf_t* _buf, uint8_t size, uint8_t bufsize)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    uint8_t start = buf->start;
    uint8_t count = buf->count;
    if (size > count) size = count;
    buf->start = (start+size) % bufsize;
    buf->count = count-size;
  }
}

static uint8_t ringbuf_read_byte(ringbuf_t* _buf, uint8_t bufsize)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    uint8_t start = buf->start;
    uint8_t count = buf->count;
    uint8_t newstart = start+1;
    if (newstart == bufsize) newstart = 0;
    buf->start = newstart;
    buf->count = count-1;
    return buf->buffer[start];
  }
}

static uint8_t ringbuf_count(ringbuf_t* _buf)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  return buf->count;
}

static uint8_t ringbuf_empty(ringbuf_t* _buf)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  return buf->count == 0;
}

static uint8_t ringbuf_full(ringbuf_t* _buf, uint8_t bufsize)
{
  ringbuf_header_t* buf = (ringbuf_header_t*)_buf;
  return buf->count == bufsize;
}

#endif
