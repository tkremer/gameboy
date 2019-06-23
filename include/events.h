/*

  High-level timer event subsystem.
  Manages a queue of timed events and a time source measured in ticks-since-reset. Consumes Timer 1.

  Copyright (c) 2018 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef _EVENTS_H_
#define _EVENTS_H_

// must be at most 254
#ifndef EVENT_QUEUE_SIZE
# define EVENT_QUEUE_SIZE 16
#endif

//#ifndef EVENT_TIMER_SCALE
//# define EVENT_TIMER_SCALE TIMER_SCALE_1
//#endif

typedef void (*event_handler_fun_t)(void*);

typedef struct event_t {
  uint32_t time;
  event_handler_fun_t handler;
  void *param;
} event_t;

/*
  the event queue is a ring buffer from index first_ix to exclusive free_ix.
  since first_ix == free_ix would both mark a full and an empty queue, the
  empty queue is specially marked with first_ix == EVENT_INDEX_EMPTY.
*/

#define EVENT_INDEX_EMPTY 0xff
//const uint8_t EVENT_INDEX_EMPTY = 0xff;

event_t event_queue[EVENT_QUEUE_SIZE];
volatile uint8_t event_first_ix = EVENT_INDEX_EMPTY,
                 event_free_ix = 0;
volatile uint16_t event_time_high = 0;

uint32_t get_time(void);
bool enqueue_event(const event_t* ev);
bool enqueue_event_abs(uint32_t time, event_handler_fun_t h, void* param);
bool enqueue_event_rel(uint32_t time, event_handler_fun_t h, void* param);
bool dequeue_events(event_handler_fun_t h);

void events_start(uint8_t scale);
static inline void events_stop(void);
static inline void events_clear(void);

static inline void events_stop(void)
{
  Timer_SetScale(1,TIMER_SCALE_STOPPED);
}

static inline void events_clear(void)
{
  event_first_ix = EVENT_INDEX_EMPTY; // don't even need atomic here.
}

#endif
