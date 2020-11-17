/*

  8x7-segment display driver with charlyplexing.

  Copyright (c) 2016 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H
/*

  2x 7-segment display

    Cathodes:                   Wiring:
         --    B0    --             B1 B0 D7 D6      D6 D7 B0 B1
     D7 |  | D6  D7 |  | D6         1  0  7  6       6  7  0  1
         --    B1    --            +----------------------------+
     D4 |  | D3  D4 |  | D2        |     8               8      |
         --    D5    --            +----------------------------+
     Anode: D2    Anode: D3         23 4  5  A0      A1 5  4  23
                                    D3 D4 D5 R       R  D5 D4 D2
                                              \D2 D3/  
           
         --  0
      7 |  | 6
         --  1
      4 |  | 2,3
         --  5

  8x 7-segment display
    (2x display is digits 2 and 3)

  B0,B1,D2,D3,D4,D5,D6,D7 -> p0..p7
  segments s*0..s*7:
         --  0
      7 |  | 6
         --  1
      4 |  | 2    3 unconnected (actually legacy)
         --  5
    Anode: s*A
  siA <=> pi,
  sij <=> pj, j!=i
  sii <=> p3

  - enable DDR[3] iff bit sym[i] set, PORT[3] = 0 (cathode on)
  - enable DDR[i] for symbol i, PORT[i] = 1 (anode on)
  - enable DDR[j] for all other set bits sym[j], PORT[j] = 0 (cathode on)
  - disable DDR[j] for unset bits of sym, PORT doesn't matter there.
  DDR(sym,i) = sym | 1<<i | ((sym >> i)& 1) << 3
  PORT(sym,i) = 1<<i   // ~(sym | 1<<3) | 1<<i




   Wiring:
    10766701
    |------|
    245AA542
    
    13766703 10766701 10766701 10736301
    |------| |------| |------| |------|
    24501542 34523542 23545342 24567542

       B1 B0 D7 D6      D6 D7 B0 B1
       1  0  7  6       6  7  0  1
      +----------------------------+
      |     8               8      |
      +----------------------------+
       23 4  5  A0      A1 5  4  23
       D3 D4 D5 R       R  D5 D4 D2
                 \D2 D3/  

*/

#include <avr/pgmspace.h>
#include <timers.h>
#include <events.h>

#define segment_display_period msec2ticks(1.0,TIMER_DIV)
//#define segment_display_period sec2ticks(1.0,TIMER_DIV)
uint64_t segment_display_symbols = 0;

const uint8_t segment_digits[] PROGMEM = {
  0b11111101, // 0
  0b01001100, // 1
  0b01110011, // 2
  0b01101111, // 3
  0b11001110, // 4
  0b10101111, // 5
  0b10111111, // 6
  0b01001101, // 7
  0b11111111, // 8
  0b11101111, // 9
};

const struct {
  char c;
  uint8_t sym;
} segment_chars[] PROGMEM = {
  { 'A', 0b11011111 },
//  { 'a', 0b01111111 },
  { 'b', 0b10111110 },
  { 'C', 0b10110001 },
  { 'c', 0b00110010 },
  { 'd', 0b01111110 },
  { 'E', 0b10110011 },
  { 'F', 0b10010011 },
  { 'G', 0b10111101 },
  { 'H', 0b11011110 },
  { 'h', 0b10011110 },
  { 'I', 0b01001100 },
  { 'i', 0b00001100 },
  { 'J', 0b01101100 },
  { 'j', 0b00101100 },
  { 'L', 0b10110000 },
  { 'l', 0b10010000 },
  { 'N', 0b11011101 },
  { 'n', 0b00011110 },
  { 'O', 0b11111101 },
  { 'o', 0b00111110 },
  { 'P', 0b11010011 },
  { 'r', 0b00010010 },
  { 'S', 0b10101111 },
  { 't', 0b10110010 },
  { 'U', 0b11111100 },
  { 'u', 0b00111100 },
  { 'Y', 0b11101110 },
  { 'Z', 0b01110011 },
  { ' ', 0b00000000 },
  { '_', 0b00100000 },
  { '-', 0b00000010 },
  { '\'',0b10000000 },
  { '`', 0b01000000 },
  { '"', 0b11000000 },
  { '(', 0b10110001 },
  { ')', 0b01101101 },
  { '[', 0b10110001 },
  { ']', 0b01101101 },
  { '=', 0b00100010 }
};

#define segment_chars_count (sizeof(segment_chars)/sizeof(segment_chars[0]))

uint8_t segment_digit2sym(uint8_t digit)
{
  return pgm_read_byte(&segment_digits[digit]);
}

uint64_t segment_int2syms(int32_t num)
{
  uint64_t res = 0;
  int i = 7;
  if (num == 0) {
    return (uint64_t)segment_digit2sym(0) << (7*8);
  }
  bool neg = (num < 0);
  if (neg) {
    num = -num; // the corner case -(MAX_INT+1) is not displayable anyway.
  }
  while (num > 0 && i >= 0) {
    uint8_t mod = num % 10;
    num = num/10;
    res |= (uint64_t)segment_digit2sym(mod) << (i*8);
    i--;
  }
  if (neg && i >= 0) {
    res |= 0x02 << (i*8);
  }
  return res;
}

uint8_t segment_char2sym(char c)
{
  // digits first:
  if (c >= '0' && c <= '9')
    return segment_digit2sym(c-'0');
  // now chars, case sensitive:
  for (uint8_t i = 0; i < segment_chars_count; i++) {
    if (pgm_read_byte(&segment_chars[i].c) == c)
      return pgm_read_byte(&segment_chars[i].sym);
  }
  // as a fallback, accept case conversion for single-cased letters:
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
    // c &= 0xdf; // (~0x20), lowercase bit.
    for (uint8_t i = 0; i < segment_chars_count; i++) {
      if (((pgm_read_byte(&segment_chars[i].c)^c) & 0x1f) == 0)
        return pgm_read_byte(&segment_chars[i].sym);
    }
  }
  // no symbol found. Choose a default:
  return 0x02; // symbol "minus" for no symbol.
}

uint16_t segment_chars2syms(const char s[2])
{
  return segment_char2sym(s[0]) | (segment_char2sym(s[1]) << 8);
}

uint64_t segment_str2syms(const char* s)
{
  uint64_t res = 0;
  for (int i = 0; i < 8; i++) {
    char c = s[i];
    if (c == 0) break;
    res |= ((uint64_t)segment_char2sym(c) << (8*i));
  }
  return res;
}

void segment_init()
{
  PORTD = 0xfc;
  PORTB |= 3;
  DDRD = 0;
  DDRB &= 0xfc;
}

// a state is essentially directly the pin configuration needed for showing it.
// sym = B0,B1,D2,D3,D4,D5,D6,D7; ddr = 0,0,DDRD2,DDRD3,0,0,0,0
// DDRD2 = 0 -> left symbol off, right D2 off.
// DDRD2 = 1, D2 = 1 -> left symbol on (still right D2 off).
// DDRD2 = 1, D2 = 0 -> left symbol off, right D2 on.
// similar for DDRD3/D3.
// Note: all led contacts are cathodes, so 0 = on, 1 = off.
void segment_setstate(uint8_t sym, uint8_t ddr)
{
  PORTD = sym & 0xfc;
  PORTB = (PORTB & ~3) | (sym & 3);
  DDRD = ddr & 0xfc;
  DDRB = (DDRB & 0xfc) | (ddr & 3);
  //DDRD = 0xf0 | (ddr & 0xc);
}

// a symbol is one 7-segment symbol using bits B0, B1, D2/3, D2/3, D4, D5, D6, D7
// The 2nd and 3rd bit are expected to be equal.
// All bits are 1 for on, 0 for off.
/*
void segment_show(uint8_t symbol, bool right)
{
  uint8_t ddr = 0;
  uint8_t sym = ~symbol;
  if (right) {
    // D2 = 0, DDRD2 = right D2?1:0, D3 = 1, DDRD3 = 1
    sym = (sym & ~4) | 8;
    ddr = 8 | (symbol & 4);
  } else {
    // D3 = 0, DDRD3 = right D3?1:0, D2 = 1, DDRD2 = 1
    sym = (sym & ~8) | 4;
    ddr = 4 | (symbol & 8);
  }
  segment_setstate(sym,ddr);
}
*/
void segment_show(uint8_t symbol, int ix)
{
  uint8_t ddr = (symbol & ~(1<<3)) | (1<<ix) | (((symbol >> ix)&1) << 3);
  uint8_t sym = 1<<ix;
  segment_setstate(sym,ddr);
}

// show_both trades one segment for the possibility of showing two digits. We could only scale this trade to 8 segments for 8 digits.
/*
// a symbol is one 7-segment symbol using bits B0, B1, D2/3, D2/3, D4, D5, D6, D7
// The 2nd and 3rd bit are forced to zero, as that led is shared with an anode.
// All bits are 1 for on, 0 for off.
void segment_show_both(uint8_t symbol)
{
  uint8_t ddr = 0xc;
  uint8_t sym = ~symbol | 0xc;
  segment_setstate(sym,ddr);
}
*/


void segment_ontimer(void* param)
{
  uint16_t ix = (uint16_t)param;
  uint64_t symbols = segment_display_symbols;
  if (symbols != 0) {
    uint8_t sym = (symbols >> (8*ix)) & 0xff;
    // right ? (symbols >> 8) : (symbols & 0xff);
    segment_show(sym,ix);
    ix++;
    if (ix >= 8) ix = 0;
    param = (void*)(ix);
    enqueue_event_rel(segment_display_period,&segment_ontimer,param);
  } else {
    segment_setstate(0xff,0); // shutdown anodes.
  }
}

// shows all 8 symbols, alternating fast.
// bits 0..7 are the left symbol, bits 8..15 are the right symbol.
void segment_display(uint64_t symbols)
{
  void* param = (void*)0;
  bool stopped = segment_display_symbols == 0;
  segment_display_symbols = symbols;
  if (stopped)
    enqueue_event_rel(1,&segment_ontimer,param);
}

void segment_undisplay()
{
  dequeue_events(&segment_ontimer);
  segment_display_symbols = 0;
  segment_setstate(0xff,0); // shutdown anodes.
}

#endif
