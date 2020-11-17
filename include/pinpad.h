/*

  Custom pinpad driver. Consumes 1 pin on Port C.

  Copyright (c) 2019 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef __PINPAD_H__
#define __PINPAD_H__

/*
  The pinpad is a numerical+"*"+"#" 12-button keyboard wired so that it
  can be read by one analog pin, used as wakeup-interrupt at least for
  the "*" key and provide a limited attack surface for anyone with access
  to all external parts.
  
  required: a pin with both ADC and interrupt capability (C0..C5).

  The pinpad hardware:
    12+1 pins, one for each button plus mass.

    Pinpad connection:
      leftmost pin (1) ----> rightmost pin (13)
      *, 7, 1, 4, 0, 8, 5, 2, #, 9, 6, 3, mass

      Yes, that's column-major order *except* for the swapping of 1 and 4.


  complete wiring:
    VCC-Ru-----+--+----~~----------+
          +-Rm-+  |                | 
    MC----+       Dz --~~------*===*-------------------+
          +-C--+  | /          |                       |
    GND-Rd-----+--+-           *=R2'=*=R3'=*=R4'===Rn'=* (connector)

  external wiring:
    *===*
    |   | 
    |   +-------------------+
    |                       | 
    *=R2'=*=R3'=*=R4'===Rn'=* (connector)

    Ri' = (0, 1k, 1.2k, 1.5k, 1.8k, 4.7k/2, 3.3k, 4.7k, 6.8k, 10k, 22k, 68k)

    (ideally Ri' = (0, 1k, 1.2k, 1.5k, 1.8k, 2.4k, 3.1k, 4.4k, 6.6k, 11k, 22k, 66k), but the above were available as SMD)

    Ri' = Ri - R_{i-1}:
    The result is a 2-wire interface that gives resistance Ri
    if button i is pressed (lower buttons mask higher buttons).
    Ri are chosen so that roughly equal voltage drops appear
    between adjacent keys.


  internal wiring:
    We used C = 2*220nF, Dz = 12V Zener diode, soldered everything directly
    between two pin headers (everything in SMD parts except for Dz).

    VCC-*-Ru-----+--+---+
            +-Rm-+  |   |
    MC--*---+      ^Dz  * 
            +-C--+  |
    GND-*-Rd-----+--+---*

    Values:
      Ru = 10k, Rd=1k => Rud=11k
      Rud := Ru+Rd
      ideally:
        V_i = VCC*(Rd/Rud+(i-1)/n*(Ru/Rud)):
        Ri = Ru/(1-V_i/VCC)-Rud
           = Rud*((i-1)/(n+1-i))
        Ri = (0, 1000, 2200, 3666.66666666667, 5500, 7857.14285714286, 11000, 15400, 22000, 33000, 55000, 121000)
        ($ perl -e 'print 93+int((1024-93)/12*$_),", " for 0..12; print "\n";')
        (93 = 1024/11)
        V_i = VCC/1024*(93, 170, 248, 325, 403, 480, 558, 636, 713, 791, 868, 946)
      real by design:
        Ri' = (0, 1k, 1.2k, 1.5k, 1.8k, 4.7k/2, 3.3k, 4.7k, 6.8k, 10k, 22k, 68k)
        Ri = (0, 1k, 2.2k, 3.7k, 5.5k, 7.85k, 11.15k, 15.85k, 22.65k, 32.65k, 54.65k, 122.65k)
        V_i = VCC*(1-Ru/(Rud+Ri))
            = (0.45V, 0.83V, 1.21V, 1.60V, 1.97V, 2.35V, 2.74V, 3.14V, 3.51V, 3.85V, 4.24V, 4.63V)
            = VCC/1024*(93, 171, 248, 327, 403, 481, 562, 643, 720, 789, 868, 947)
      real by measurement:
        TODO
*/

#include <avr/pgmspace.h>
#include <adc_watch.h>

/* This needs to be done elsewhere:
void EVENT_adc_watch(uint8_t channel, int16_t value) {
  if (channel == PINPAD_PIN)
    pinpad_on_adc_read(value);
}
void init() {
  PCICR |= 1<<PCIE1;
}
*/


// Port is always C
#ifndef PINPAD_PIN
#define PINPAD_PIN 0 // C0, ADC0
#endif

// FIXME: replace ideal values by proper ranges.
//    OR: replace values by linear approximation, saving memory.
const int16_t pinpad_adc_values[12] PROGMEM =
  {93, 171, 248, 327, 403, 481, 562, 643, 720, 789, 868, 947};
const char pinpad_chars[12] PROGMEM =
  {'*', '7', '1', '4', '0', '8', '5', '2', '#', '9', '6', '3'};
//const int16_t pinpad_fuzz = 93/3;
// idea: voltage space between k and k+1 is 1/3 for k, 1/3 invalid
// and 1/3 for k+1. However it may as well be [x,1-2x,x] for any x<1/2.
#define pinpad_fuzz (93/3) // more correctly (1024-93)/12/3 = 26
#define pinpad_max_valid (947+pinpad_fuzz)
#define pinpad_min_idle (1023-pinpad_fuzz)

typedef struct {
  int16_t minval;
} pinpad_ctx_t;
pinpad_ctx_t pinpad_ctx;

void EVENT_pinpad_keypressed(char c);

// pressing a key i means going down to a voltage that is within
// pinpad_adc_values[i] +- pinpad_fuzz, then going up again.
// if we reach somewhere between those values, we register the keypress
// as a faulty keypress, returning character '\0'.
void pinpad_on_adc_read(int16_t value) {
  int16_t minval = pinpad_ctx.minval;
  if (minval == 1024) {
    adc_watch_set_range(PINPAD_PIN,value-5,value+5);
  }
  if (value < minval) {
    minval = value;
    pinpad_ctx.minval = minval;
  }
  if (value > pinpad_min_idle) {
    if (minval < pinpad_min_idle) {
      // V_i ~= 93+(1024-93)/12*i
      // i ~= (V_i-93)*12/(1024-93)
      int16_t i = ((minval-93)*24/(1024-93)+1)/2;
      if (i < 0) i = 0;
      if (i > 11) i = 11;
      //while (i > 0 && pinpad_adc_values[i-1]

      int16_t diff = abs(minval-(int16_t)pgm_read_word(&pinpad_adc_values[i]));
      if (diff < pinpad_fuzz) {
        EVENT_pinpad_keypressed((char)pgm_read_byte(&pinpad_chars[i]));
      } else {
        EVENT_pinpad_keypressed(0);
      }
    }
    pinpad_ctx.minval = 1024;
    adc_watch_set_range(PINPAD_PIN,pinpad_max_valid,1023);
  }
}

void pinpad_sleep() {
  // need to activate digital input, disable reading
  uint8_t mask = (1<<PINPAD_PIN);
  PCMSK1 |= mask;
  DIDR0 &= ~mask;
  adc_watch_set_mask(adcw_state.mask & ~mask);
  //adc_watch_set_range(PINPAD_PIN,0,1023);
}

void pinpad_unsleep() {
  // need to disable digital input, activate reading
  uint8_t mask = (1<<PINPAD_PIN);
  PCMSK1 &= ~mask;
  DIDR0 |= mask;

  adc_watch_set_range(PINPAD_PIN,pinpad_max_valid,1023);
  adc_watch_set_mask(adcw_state.mask | mask);
}

void pinpad_init() {
  pinpad_ctx.minval = 1024;
  DDRC &= ~(1<<PINPAD_PIN);
  PORTC &= ~(1<<PINPAD_PIN);
  //PORTC |= (1<<PINPAD_PIN);
  pinpad_unsleep();
}

void pinpad_stop() {
  pinpad_sleep();
  pinpad_ctx.minval = 1024;
  PORTC |= (1<<PINPAD_PIN); // enable internal pull-up, just in case.
}

#endif
