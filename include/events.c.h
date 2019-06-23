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

#include "events.h"
#include "timers.h"

static inline uint8_t succmod(uint8_t i, uint8_t mod)
{
  return (i!=mod-1)?i+1:0;
}

static inline uint8_t predmod(uint8_t i, uint8_t mod)
{
  return (i!=0)?i-1:mod-1;
}

#define events_incmod(x,mod) do { x = succmod(x,(mod)); } while(0)
#define events_decmod(x,mod) do { x = predmod(x,(mod)); } while(0)

// instead of x > y. Means that x >= y > x-(1<<(sizeof(x)-1))
// <=> (x-y >= 0) for signed types:
#define gteq_mod_type(x,y) (((x) - (y)) & (1 << (sizeof(x)*8-1)) == 0)

static inline bool gteq_mod32(uint32_t x,uint32_t y) {
  return (int32_t)(x-y) >= 0;
}

static inline bool gteq_mod16(uint16_t x,uint16_t y) {
  return (int16_t)(x-y) >= 0;
}

static inline bool gteq_mod8(uint8_t x,uint8_t y) {
  return (int8_t)(x-y) >= 0;
}

static inline void events_checknclear_ovf(void) {
  bool tovf;
  tovf = (Timer_Interrupt_Flags(1) & TIMER_INTERRUPT_OVERFLOW) != 0;
  if (tovf) {
    Timer_Interrupt_Flag_Clear(1,TIMER_INTERRUPT_OVERFLOW);
    event_time_high++;
  }
}

/*
  Here be dragon races:
    Even with cli() we cannot (and don't want to) stop the hardware timer
    from increasing and possibly overflowing.
    We cannot get the value and the overflow flag at the same time
    (but would need to), so we get the flag before and after getting the value,
    and check, if it changed in between. If it did, we cannot determine whether
    we got the value before or after the overflow, so we assume it is 0 and
    after the overflow.
    [Due to a design flaw we cannot catch an overflow interrupt before an
    output-compare interrupt anyway.]

  This function takes min 26 cycles and max 37 cycles. (incl. call & ret)

*/
static /*inline*/ uint32_t get_time_sync(void) {
  uint16_t l,h;
  bool tov_before, tov_after;
  // 5 cycles (call).
  // 4 cycles (2*lds):
  h = event_time_high;
  // 1 cycle (in):
  tov_before = (Timer_Interrupt_Flags(1) & TIMER_INTERRUPT_OVERFLOW) != 0;
  // 4 cycles (2*lds):
  l = Timer_Value(1);
  // 3/2 cycles ((1/2/3)+(2/0): sbis+rjmp):
  tov_after = (Timer_Interrupt_Flags(1) & TIMER_INTERRUPT_OVERFLOW) != 0;
  if (tov_after) {
    // 2 cycles (subi+subci):
    h++;
    // we update time_high, as it does not take much time (6 cycles) and
    // is rare.
    // 4 cycles (2*sts):
    event_time_high = h;
    // 2 cycles (ldi+out):
    Timer_Interrupt_Flag_Clear(1,TIMER_INTERRUPT_OVERFLOW);
    // 3/2 cycles ((1/2/3)+(2/0): sbrc+rjmp):
    if (!tov_before) {
      // 2 cycles (2*ldi):
      l = 0;
    }
  }
  register uint32_t res __asm__("r22"); // reduces shuffling around of bytes.
  // 2 cycles (movw):
  __asm__ (
    "movw %0, %1" "\n\t"
//    "mov %A0, %A1" "\n\t"
//    "mov %B0, %B1" "\n\t"
    : "=r" (res)
    : "r" (l)
  );
  // 2 cycles (movw):
  __asm__ (
    "movw %C0, %1" "\n\t"
//    "mov %C0, %A1" "\n\t"
//    "mov %D0, %B1" "\n\t"
    : "+r" (res)
    : "r" (h)
  );
  // 5 cycles (ret):
  return res;
// This would do heavy calculating:
//  return ((uint32_t)h) << 16 | l;

// This is much better & tighter. But the compiler messes up the optimization
// by shuffling along with the registers heavily.
//... ok, finally it got the idea of the above...
// yet still it doesn't manage the "a << 16 | b" notation...
/*
  h = event_time_high;
  __asm__ (
      // Load flags
      "in __tmp_reg__, %2" "\n\t"
      // Load value
      "lds %A0, %3" "\n\t"
      "lds %B0, %3+1" "\n\t"
      // if ovf flag still cleared, we're done
      "sbis %2, %4" "\n\t"
      "rjmp L_%=" "\n\t"
      // inc(h)
      "adiw %1, 1" "\n\t"
      // if ovf flag was already set before, we're done
      "sbrc __tmp_reg__,%4" "\n\t"
      "rjmp L_%=" "\n\t"
      // ovf flag just turned on. Clear l.
      "clr %A0" "\n\t"
      "clr %B0" "\n\t"
      "L_%=:" "\n\t"
    :
      "=r" (l),
      "=w" (h)
    :
      "I" (_SFR_IO_ADDR(Timer_Interrupt_Flags(1))),
      "M" (_SFR_MEM_ADDR(Timer_Value(1))),
      "I" (0),// ( == lb(TIMER_INTERRUPT_OVERFLOW))
      "1" (h)
  );
  
  return ((uint32_t)h) << 16 | l;*/
}

uint32_t get_time(void) {
  uint32_t res;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    res = get_time_sync();
  }
  return res;
}

/*
ISR (TIMER1_OVF_vect, ISR_BLOCK)
{
  uint16_t time_high = event_time_high;
  time_high++;
  event_time_high = time_high;
  if (event_first_ix != EVENT_INDEX_EMPTY) {
    uint32_t time = event_queue[event_first_ix].time;
    if (time_high == time >> 16) {
      OCR1A = time & 0xffff;
      Timer_Interrupt_Enable(1,TIMER_INTERRUPT_OUTPUT_COMPARE_A);
    }
  }
}
*/

ISR (TIMER1_COMPA_vect, ISR_BLOCK)
{
  do {
    uint8_t first_ix = event_first_ix;
    if (first_ix == EVENT_INDEX_EMPTY) {
      events_checknclear_ovf();
      return;
    }
    event_t *ev = &event_queue[first_ix];
    uint32_t next_t = ev->time;
    uint32_t now = get_time_sync();
    if (gteq_mod32(now,next_t)) {
      uint8_t free_ix = event_free_ix;
      first_ix++;
      event_t *ev_next = ev+1;
      if (first_ix == EVENT_QUEUE_SIZE) {
        first_ix = 0;
        ev_next = &event_queue[0];
      }
      if (first_ix != free_ix) {
        uint16_t t_lo = ev_next->time /*event_queue[first_ix].time*/ & 0xffff;
        OCR1A = t_lo;
        // 4 cycles (2*lds):
        uint16_t now = Timer_Value(1);
        // 2 cycles for useless movw.
        // 5/4 cycles (1+1+(1/2)+(2/0) sub+sbc+sbrc+rjmp)
        if (gteq_mod16(now,t_lo)) {
          // 6 cycles (1+1+4 subi+sbci+2*sts)
          OCR1A = now+18;
        }
      } else {
        first_ix = EVENT_INDEX_EMPTY;
      }
      event_first_ix = first_ix;
      event_handler_fun_t h = ev->handler;
      void *p = ev->param;
      h(p);
      cli();
      // h() may sei() and even modify events.
    } else {
//  This could strengthen our ovf detection:
//      OCR1A ^= 0x8000;
      return;
    }
  } while (1);
}

// (internal) set the timer to go off at the given time or as soon as possible
// if that time has already passed, handling race conditions.
// use with interrupts disabled.
static inline void events_set_hw_timer(uint32_t time)
{
  OCR1A = time & 0xffff;
  // This is a race. We need to make sure time > get_time_sync() at the
  // time we set OCR1A. Otherwise we might be a whole cycle late.
  // altogether: 71/72/73 cycles (37 + 17 + (5/7/8) + 12).
  // Below dist is chosen so that dist >= ceil(cycles/DIV)+1.
  // 37 cycles (call-ret):
  uint32_t now = get_time_sync();
  // 4 cycles for 2*movw.
  // another 4 cycles for 2*movw.
  // 4 cycles for sub+3*sbc,
  // 3/2 cycles ((1/2/3)+(2/0) sbrc+rjmp):
  if (gteq_mod32(now,time)) { // The event is in the past. Reschedule.
    // 3 cycles (lds+andi):
    uint8_t scale = (TCCR1B >> CS10) & 7;
    uint8_t dist;
    // whole if-clause: 5/7/8 cycles.
    // 3/2 cycles (1+(1/2) cpi+brne):
    if (scale == TIMER_SCALE_DIV_8)
      // 3 cycles (ldi+rjmp)
      dist = 10;
    // 2/3 cycles (1+(1/2) cpi+breq):
    else if (scale == TIMER_SCALE_1)
      // 1 cycle (ldi)
      dist = 90; //73, but just to make sure.
    // 3 cycles (ldi+rjmp)
    else dist = 3;
    // 4 cycles for pointless 2*movw.
    // 4 cycles (add+3*adc):
    time = now+dist;
    // 4 cycles (2*sts):
    OCR1A = time;
    // At this point the race ends. When time moves on, the interrupt flag
    // will fire and as soon as we sei(), so will the interrupt vector.
  }
}


/*
uint32_t get_time(void)
{
  uint16_t l,h;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    l = Timer_Value(1);
    h = event_time_high;
    if ((l & 0x8000) && (TIFR1 & (1 << TOV1))) {
      h++; // the timer just overflowed, but the interrupt has not yet fired.
    }
  }
//  uint32_t res;
//  res = ((uint32_t)h) << 16;
//  res |= (uint32_t)l;
  return ((uint32_t)h) << 16 | l;
}
*/
bool enqueue_event(const event_t* ev)
{
  return enqueue_event_abs(ev->time,ev->handler,ev->param);
/*  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (event_free_ix == event_first_ix)
      return false;
    if (event_first_ix == EVENT_INDEX_EMPTY) {
      event_first_ix = 0;
      event_free_ix = 1;
      event_queue[0] = *ev;
    } else {
      if (ev->time >= event_queue[event_free_ix-1].time) {
        event_queue[event_free_ix++] = *ev;
      } else {
        uint8_t k = event_free_ix-2;
        while (ev->time < event_queue[k].time && k != event_first_ix) {
          if (k != 0) k--;
          else k = EVENT_QUEUE_SIZE-1;
        }
        k = (k!=EVENT_QUEUE_SIZE-1)?k+1:0;
        for (uint8_t i = event_free_ix; i != k;
                i = (i!=0)?i-1:EVENT_QUEUE_SIZE-1) {
          event_queue[i] = event_queue[(i!=0)?i-1:EVENT_QUEUE_SIZE-1];
        }
        event_queue[k] = *ev;
        event_free_ix++;
        if (event_free_ix == EVENT_QUEUE_SIZE) event_free_ix = 0;
      }
    }
  }
  return true;*/
}

bool enqueue_event_abs(uint32_t time, event_handler_fun_t h, void* param)
/*{
  event_t ev = { .time = time, .handler = h, .param = param };
  return enqueue_event(&ev);
}*/
{
  uint8_t ix;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    // need to copy the volatiles for the optimizer:
//    uint32_t cur_time = (uint32_t)event_time_high << 16 | Timer_Value(1);
    uint8_t first_ix = event_first_ix;
    uint8_t free_ix = event_free_ix;
    if (free_ix == first_ix)
      return false;
    if (first_ix == EVENT_INDEX_EMPTY) {
      first_ix = 0;
      free_ix = 1;
      ix = 0;
      event_first_ix = first_ix;
    } else {
      //uint8_t k = free_ix!=0 ? free_ix-1 : EVENT_QUEUE_SIZE-1;
      uint8_t k = predmod(free_ix,EVENT_QUEUE_SIZE);
      uint8_t low = predmod(first_ix,EVENT_QUEUE_SIZE);
      while (time < event_queue[k].time && k != low) {
        events_decmod(k,EVENT_QUEUE_SIZE);
//        if (k != 0) k--;
//        else k = EVENT_QUEUE_SIZE-1;
      }
      events_incmod(k,EVENT_QUEUE_SIZE);
//      k = (k!=EVENT_QUEUE_SIZE-1) ? k+1 : 0;
      for (uint8_t i = free_ix; i != k;
                   i = (i!=0)?i-1:EVENT_QUEUE_SIZE-1) {
//        event_queue[i] = event_queue[(i!=0)?i-1:EVENT_QUEUE_SIZE-1];
        event_queue[i] = event_queue[predmod(i,EVENT_QUEUE_SIZE)];
      }
      ix = k;
      events_incmod(free_ix,EVENT_QUEUE_SIZE);
//      free_ix++;
//      if (free_ix == EVENT_QUEUE_SIZE) free_ix = 0;
    }
    if (ix == first_ix) {
      events_set_hw_timer(time);
/*
      OCR1A = time & 0xffff;
      // This is a race. We need to make sure time > get_time_sync() at the
      // time we set OCR1A. Otherwise we might be a whole cycle late.
      // altogether: 71/72/73 cycles (37 + 17 + (5/7/8) + 12).
      // Below dist is chosen so that dist >= ceil(cycles/DIV)+1.
      // 37 cycles (call-ret):
      uint32_t now = get_time_sync();
      // 4 cycles for 2*movw.
      // another 4 cycles for 2*movw.
      // 4 cycles for sub+3*sbc,
      // 3/2 cycles ((1/2/3)+(2/0) sbrc+rjmp):
      if (gteq_mod32(now,time)) { // The event is in the past. Reschedule.
        // 3 cycles (lds+andi):
        uint8_t scale = (TCCR1B >> CS10) & 7;
        uint8_t dist;
        // whole if-clause: 5/7/8 cycles.
        // 3/2 cycles (1+(1/2) cpi+brne):
        if (scale == TIMER_SCALE_DIV_8)
          // 3 cycles (ldi+rjmp)
          dist = 10;
        // 2/3 cycles (1+(1/2) cpi+breq):
        else if (scale == TIMER_SCALE_1)
          // 1 cycle (ldi)
          dist = 90; //73, but just to make sure.
        // 3 cycles (ldi+rjmp)
        else dist = 3;
        // 4 cycles for pointless 2*movw.
        // 4 cycles (add+3*adc):
        time = now+dist;
        // 4 cycles (2*sts):
        OCR1A = time;
        // At this point the race ends. When time moves on, the interrupt flag
        // will fire and as soon as we sei(), so will the interrupt vector.
      }
//      events_checknclear_ovf();
*/
    }
    event_free_ix = free_ix;
    event_queue[ix].time = time;
    event_queue[ix].handler = h;
    event_queue[ix].param = param;
/*    if (ix == first_ix) {
      OCR1A = time & 0xffff;
      events_checknclear_ovf();
    }*/
  }
  return true;
}

/*
void events_clear(void)
{
  event_first_ix = EVENT_INDEX_EMPTY; // don't even need atomic here.
}
*/

bool enqueue_event_rel(uint32_t time, event_handler_fun_t h, void* param)
{
  return enqueue_event_abs(get_time()+time,h,param);
//  event_t ev = { .time = get_time()+time, .handler = h, .param = param };
//  return enqueue_event(&ev);
}

// un-queue all events with handler h
// returns true iff such events were present.
bool dequeue_events(event_handler_fun_t h)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    // need to copy the volatiles for the optimizer:
    uint8_t first_ix = event_first_ix;
    uint8_t free_ix = event_free_ix;
    if (first_ix == EVENT_INDEX_EMPTY) {
      return false;
    } else {
      bool deleted_first = false;
      uint8_t deleted = 0;
      for (uint8_t k = first_ix; k != free_ix;
                   k = k!=EVENT_QUEUE_SIZE-1?k+1:0) {
        if (event_queue[k].handler == h) {
          if (k == first_ix) {
            first_ix = succmod(k,EVENT_QUEUE_SIZE);
            deleted_first = true;
          } else {
            deleted++;
          }
        } else if (deleted != 0) {
          event_queue[(k+EVENT_QUEUE_SIZE-deleted)%EVENT_QUEUE_SIZE] = event_queue[k];
        }
      }
      if (deleted != 0) {
        free_ix = (free_ix+EVENT_QUEUE_SIZE-deleted)%EVENT_QUEUE_SIZE;
        event_free_ix = free_ix;
      }
      if (deleted_first) {
        if (first_ix == free_ix) {
          first_ix = EVENT_INDEX_EMPTY;
          // we don't need to update OCR1A. The old value will do.
        } else {
          uint32_t time = event_queue[first_ix].time;
          events_set_hw_timer(time);
        }
        event_first_ix = first_ix;
      }
      bool res = deleted_first || (deleted != 0);
      return res;
    }
  }
  return false; // never reached, but compiler disagrees.
}


void events_start(uint8_t scale)
{
  Timer_Init(1);
  if (event_first_ix != EVENT_INDEX_EMPTY)
    OCR1A = event_queue[event_first_ix].time & 0xffff;
   else OCR1A = 0;
  Timer_Interrupts(1) = TIMER_INTERRUPT_OUTPUT_COMPARE_A;
  Timer_SetScale(1,scale);
}

/*
void events_stop(void)
{
  Timer_SetScale(1,TIMER_SCALE_STOPPED);
}
*/

