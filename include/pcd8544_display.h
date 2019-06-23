/*

  Display driver for PCD8544 display.
  Uses an internal display buffer and an invalidation mask to sync to the
   display. Supports pixels, rectangles, sprites and text/fonts.
  Consumes the SPI subsystem.

  Copyright (c) 2019 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef __PCD8544_DISPLAY_H__
#define __PCD8544_DISPLAY_H__

#include "spi.h"
#include <avr/pgmspace.h>

#define LCD_WIDTH 84
#define LCD_HEIGHT 48

#define PCD8544_POWERDOWN 0x04
#define PCD8544_ENTRYMODE 0x02
#define PCD8544_EXTENDEDINSTRUCTION 0x01

#define PCD8544_DISPLAYBLANK 0x0
#define PCD8544_DISPLAYNORMAL 0x4
#define PCD8544_DISPLAYALLON 0x1
#define PCD8544_DISPLAYINVERTED 0x5

// H = 0
#define PCD8544_FUNCTIONSET 0x20
#define PCD8544_DISPLAYCONTROL 0x08
#define PCD8544_SETYADDR 0x40
#define PCD8544_SETXADDR 0x80

// H = 1
#define PCD8544_SETTEMP 0x04
#define PCD8544_SETBIAS 0x10
#define PCD8544_SETVOP 0x80

#define PCD8544_SPI_DIV SPI_DIV_4

#define display_bias 4
#define display_contrast 0x3f

#define display_reset (1<<3)
#define display_ssel (1<<4)
#define display_dc (1<<5)

#define display_patch_size 4 // divisor of LCD_WIDTH; good to use a power of 2.
#define display_mask_size (LCD_WIDTH/display_patch_size*LCD_HEIGHT/8) // bits
#define display_mask_bytesize ((display_mask_size+7)>>3)
//  (int)ceil(display_mask_size*1.0/8)]; -> compiler didn't like this.
struct {
  uint8_t image[LCD_WIDTH*LCD_HEIGHT/8]; //504 bytes
  uint8_t modified_mask[display_mask_bytesize]; //126+eps bits = 16 bytes
  uint8_t pos, mod;
} display_ctx;

const uint8_t display_init_seq[] PROGMEM = {
    // extended mode
    PCD8544_FUNCTIONSET | PCD8544_EXTENDEDINSTRUCTION,
    // set bias
    PCD8544_SETBIAS | display_bias,
    // set contrast
    PCD8544_SETVOP | display_contrast,
    // normal mode
    PCD8544_FUNCTIONSET,
    // normal display mode
    PCD8544_DISPLAYCONTROL | PCD8544_DISPLAYNORMAL,
    // row 0
    PCD8544_SETYADDR | 0,
    // column 0
    PCD8544_SETXADDR | 0
  };


void display_hwgoto(uint8_t x, uint8_t y) {
  SPI_Disable_Interrupt();
  
  PORTD |= display_ssel; // re-sync communication channel
  // commands follow
  PORTD &= ~display_dc;
  // keep ssel low again.
  PORTD &= ~display_ssel;

  SPI_TransferByte(PCD8544_SETYADDR | y);
  SPI_TransferByte(PCD8544_SETXADDR | x);

  // data follows
  PORTD |= display_dc;

  SPI_Enable_Interrupt();
}

void display_sync() {
  SPI_Disable_Interrupt();
  
  PORTD |= display_ssel; // re-sync communication channel
  // commands follow
  PORTD &= ~display_dc;
  // actually, we keep ssel low for the rest of the time.
  PORTD &= ~display_ssel;
  for (int i = sizeof(display_init_seq)-2; i < sizeof(display_init_seq); i++)
    SPI_TransferByte(pgm_read_byte(&display_init_seq[i]));

  // data follows
  PORTD |= display_dc;

  display_ctx.pos = 0;
  display_ctx.mod = 0;

  SPI_Enable_Interrupt();
}

void display_hwclear() {
  // data follows
  PORTD |= display_dc;
  
  // send data
//  PORTD &= ~(display_ssel);
  for (int i = 0; i < LCD_WIDTH*LCD_HEIGHT; i++) {
    SPI_TransferByte(0);
  }
//  PORTD |= display_ssel;
}

bool display_isr() {
  uint8_t i = display_ctx.pos;
  uint8_t j = display_ctx.mod;
  // j==0 is special (after update suspension),
  // j==display_patch_size is after finishing one patch. Go find next one.
  if (j == display_patch_size || j == 0) {
    if (j == 0) {
      i = -1;
    }
/*
    i++;
    if (i >= display_mask_size) {
      i = 0;
      display_sync();
      return 0;
    }
*/
    // DONE: enable skipping.
    uint8_t count = 0;
    bool check = 0;
    do {
      i++;
      count++;
      if (i >= display_mask_size) {
        i = 0;
      }
      check = display_ctx.modified_mask[i>>3] & (1<<(i & 7));
    } while (!check && count <= display_mask_size);
      // case count==display_patch_size: check last pos once again, as we
      // may have invalidated it, while it was transmitting.
    if (!check) {
      // nothing to do. suspend update, mark this with mod == 0.
      display_ctx.mod = 0;
      return 0;
    }
    if (count != 1 || j == 0) {
      uint8_t x = (i%(LCD_WIDTH/display_patch_size))*display_patch_size;
      uint8_t y = i/(LCD_WIDTH/display_patch_size);
      display_hwgoto(x,y);
    }
    display_ctx.pos = i;
    display_ctx.modified_mask[i>>3] &= ~(1<<(i&7)); // being updated now.
    j = 0;
  }
  SPI_AsyncTransferByte(display_ctx.image[((uint16_t)i)*display_patch_size+j]);
  j++;
  display_ctx.mod = j; // in 1..display_patch_size
  return 1;
}

void display_poll() {
  if (display_ctx.mod == 0) {
    display_isr();
  }
}

/*
bool timer_queued = 0;
void display_ontimer(void* param) {
  timer_queued = 0;
  display_isr();
}
*/

On_SPI_Complete {
  //SPI_AsyncWaitResult();
/*
  if (!timer_queued) {
    timer_queued = 1;
    enqueue_event_rel(msec2ticks(1000,TIMER_DIV),&display_ontimer,NULL);
  }*/
  display_isr();
}

void display_invalidate_all() {
  for (int i = 0; i < display_mask_bytesize; i++) {
    display_ctx.modified_mask[i] = 0xff;
  }
  display_poll();
}

void display_clear() {
  for (int i = 0; i < LCD_WIDTH*LCD_HEIGHT/8; i++) {
    display_ctx.image[i] = 0;
  }
  display_invalidate_all();
}

void display_init() {
  display_ctx.pos = 0;
  display_ctx.mod = 0;


  // initialize
  SPI_Init(SPI_MODE_MASTER | SPI_MODE_MSB | SPI_MODE_SAMPLE_LEADING | SPI_MODE_IDLE_LOW,PCD8544_SPI_DIV); // no interrupt yet.
  PORTD |= display_ssel | display_dc;
  DDRD |= display_ssel | display_reset | display_dc;

  // reset display
  PORTD &= ~display_reset;
  _delay_ms(500);
  PORTD |= display_reset;
 
  // commands follow
  PORTD &= ~display_dc;
  // actually, we keep ssel low for the rest of the time.
  PORTD &= ~display_ssel;
  for (int i = 0; i < sizeof(display_init_seq); i++)
    SPI_TransferByte(pgm_read_byte(&display_init_seq[i]));

  // data follows
  PORTD |= display_dc;

//  display_hwclear();
  SPI_Enable_Interrupt();

  display_clear();

/*  display_ctx.image[0] = 0xaa;
  display_ctx.image[1] = 0x55;
  display_ctx.image[2] = 0xaa;
  display_ctx.image[3] = 0x55;
  display_ctx.image[4] = 0x88;
  display_ctx.image[5] = 0x44;
  display_ctx.image[6] = 0x22;
  display_ctx.image[7] = 0x11;
*/
}

#define COLOR_WHITE 0
#define COLOR_BLACK 1
#define COLOR_INVERT 2

void display_putpixel(uint8_t x, uint8_t y, uint8_t color) {
  if (!(x >= 0 && y >= 0 && x < LCD_WIDTH && y < LCD_HEIGHT))
    return;
  uint16_t pos = x+LCD_WIDTH*(uint16_t)(y>>3);
  uint8_t mpos = pos/display_patch_size;
  switch (color) {
    case COLOR_WHITE:
      display_ctx.image[pos] &= ~(1<<(y&7));
      break;
    case COLOR_BLACK:
      display_ctx.image[pos] |= (1<<(y&7));
      break;
    case COLOR_INVERT:
      display_ctx.image[pos] ^= (1<<(y&7));
      break;
  }
  display_ctx.modified_mask[mpos>>3] |= (1<<(mpos&7));
  display_poll();
}

uint8_t display_getpixel(uint8_t x, uint8_t y) {
  if (!(x >= 0 && y >= 0 && x < LCD_WIDTH && y < LCD_HEIGHT))
    return 0;
  uint16_t pos = x+LCD_WIDTH*(uint16_t)(y>>3);
  return display_ctx.image[pos] & (1<<(y&7)) ? COLOR_BLACK : COLOR_WHITE;
}

// TODO: improve performance (we don't have to loop over every *bit*
// of modified_mask.
void display_invalidate_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
//  uint8_t mx = x0&7;
  uint8_t y0 = y>>3;
  uint8_t y1 = (y+h-1)>>3;
  uint8_t x0 = x / display_patch_size;
  uint8_t x1 = (x+w-1) / display_patch_size;

  // range check
  if (y1 >= LCD_HEIGHT>>3) y1 = (LCD_HEIGHT>>3)-1;
  if (x1 >= LCD_WIDTH/display_patch_size) x1 = LCD_WIDTH/display_patch_size-1;

  uint16_t mpos0 = x0+y0*(LCD_WIDTH/display_patch_size);
  for (uint8_t dy = 0; dy <= (y1-y0); dy++) {
    for (uint8_t dx = 0; dx <= (x1-x0); dx++) {
      uint8_t mpos = mpos0+dy*(LCD_WIDTH/display_patch_size)+dx;
      display_ctx.modified_mask[mpos>>3] |= (1<<(mpos&7));
    }
  }
  display_poll();
}

// FIXME: range check does bad clipping.
void display_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color) {
  uint8_t y0 = y>>3;
  uint8_t y1 = (y+h-1)>>3;
  uint8_t x0 = x;
  uint8_t x1 = (x+w-1);

  // range check
  if (y1 >= LCD_HEIGHT>>3) y1 = (LCD_HEIGHT>>3)-1;
  if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH-1;

  uint8_t my0 = y & 7;
  uint8_t my1 = (y+h-1) & 7;

  uint8_t y0mask = 0xff << my0;
  uint8_t y1mask = 0xff >> (7-my1);
  if (y0 == y1) y0mask &= y1mask;

  uint16_t pos0 = x0+((uint16_t)y0)*LCD_WIDTH;
  for (uint8_t dy = 0; dy <= (y1-y0); dy++) {
    uint8_t mask = dy == 0 ? y0mask : dy==(y1-y0) ? y1mask : 0xff;
    uint16_t pos1 = pos0+((uint16_t)dy)*LCD_WIDTH;
    for (uint8_t dx = 0; dx <= (x1-x0); dx++) {
      uint16_t pos = pos1+dx;
      switch (color) {
        case COLOR_WHITE:
          display_ctx.image[pos] &= ~(mask);
          break;
        case COLOR_BLACK:
          display_ctx.image[pos] |= (mask);
          break;
        case COLOR_INVERT:
          display_ctx.image[pos] ^= (mask);
          break;
      }
    }
  }

  display_invalidate_rect(x,y,w,h);
}

// w*h pixels, sizeof(*black) == sizeof(*white) == w*ceil(h*1.0/8)
// black contains 1s where blackness shall be.
// white contains 0s where whiteness shall be.
//  anything else is transparent. Anything below y=h must be transparent.
//  They may point to the same location to get opaque sprites, but then h%8==0.
typedef struct {
  uint8_t w,h;
  const uint8_t *black, *white; // ram or pgm_addr(pgmspace)
} sprite_t;

// starts with codepoint 0x20.
typedef struct {
  uint8_t height, symbols;
  const uint16_t *offsets;                  // in pgmspace!
  const uint8_t *chars_black, *chars_white; // in pgmspace!
} font_t;

#define __PGM_FLAG 0x8000
#define pgm_addr(x) ((typeof(x)) ((uint16_t)(x) | __PGM_FLAG))
//#define is_pgm_addr(x) ((uint16_t)(x) & __PGM_FLAG)
#define load_byte(x) ((uint16_t)&(x) & __PGM_FLAG ? pgm_read_byte((uint8_t*)((uint16_t)(&(x)) & ~__PGM_FLAG)) : (x))

// TODO: signed coordinates
void display_sprite(uint8_t x, uint8_t y, const sprite_t* sprite) {
  uint8_t w = sprite->w;
  uint8_t w2 = w;
  uint8_t h = sprite->h;
  if (w == 0 || h == 0) return;

  uint8_t h0 = (h+7)>>3;
  // [y0..y1] = [y>>3 .. (y+dy)>>3] (inclusive)
  uint8_t my = y&7;
  uint8_t y0 = y>>3;
  uint8_t y1 = (y+h-1)>>3;

  // range check
  if (y1 >= LCD_HEIGHT>>3) y1 = (LCD_HEIGHT>>3)-1;
  if (x+w2 > LCD_WIDTH) {
    if (LCD_WIDTH <= x)
      return;
    w2 = LCD_WIDTH-x;
  }

  uint16_t pos0 = x+((uint16_t)y0)*LCD_WIDTH;
  for (uint8_t dx = 0; dx < w2; dx++) {
    for (uint8_t dy = 0; dy <= (y1-y0); dy++) {
      uint16_t pos = pos0+((uint16_t)dy)*LCD_WIDTH+dx;
      uint8_t or_mask, and_mask;
      if (dy < h0) {
        or_mask = load_byte(sprite->black[dx+(uint16_t)(w)*dy]);
        and_mask = load_byte(sprite->white[dx+(uint16_t)(w)*dy]);
      } else {
        or_mask = 0;
        and_mask = 0xff;
      }
      if (my != 0) {
        or_mask <<= my;
        and_mask <<= my;
        if (dy != 0) {
          or_mask |= load_byte(sprite->black[dx+(uint16_t)(w)*(dy-1)]) >> (8-my);
          and_mask |= load_byte(sprite->white[dx+(uint16_t)(w)*(dy-1)]) >> (8-my);
        } else {
          and_mask |= 0xff >> (8-my);
        }
      }
      uint8_t img = display_ctx.image[pos];
      display_ctx.image[pos] = (img & and_mask) | or_mask;
    }
  }
  display_invalidate_rect(x,y,x+w2,y+h);
}

// user-friendly variant, no need to use pgm_addr() (but overhead).
void display_sprite_P(uint8_t x, uint8_t y, const sprite_t* sprite) {
  sprite_t sp = *sprite;
  sp.black = pgm_addr(sp.black);
  sp.white = pgm_addr(sp.white);
  display_sprite(x,y,&sp);
}

// font must be in pgmspace and contain pointers to pgmspace!
void display_text_(uint8_t x, uint8_t y, const font_t* font, const char* text, bool text_pgm) {
  uint8_t px = x, py = y,
          h = pgm_read_byte(&font->height),
          syms = pgm_read_byte(&font->symbols);
  uint8_t *black = (uint8_t*)pgm_read_ptr(&font->chars_black),
          *white = (uint8_t*)pgm_read_ptr(&font->chars_white);
  uint16_t *offsets = (uint16_t*)pgm_read_ptr(&font->offsets);
  uint8_t bppx = (h+7)>>3; // bytes per x-pixel
  black = pgm_addr(black),
  white = pgm_addr(white);
  unsigned char c;
  // "warning: suggest parentheses around assignment used as truth value" (!)
  while ((c = text_pgm ? pgm_read_byte(text) : *(unsigned char*)text)) {
    if (c < 0x20) {
      if (c == 10) { // enter.
        px = x;
        py += h;
      }
      // otherwise just skip the symbol.
    } else if (c-0x20 >= syms) {
      // do not print this symbol.
    } else {
      c -= 0x20;
      uint16_t offs = ((c == 0) ? 0 : pgm_read_word(&offsets[c-1]));
      uint16_t offs2 = pgm_read_word(&offsets[c]);
      uint8_t w = offs2-offs;
      if (w != 0) {
        if (bppx != 1) w = w/bppx;
          // will the optimizer accept this special case as likely?

/*        if (w != 5) {
          display_ctx.image[0] = 0xff;
          display_ctx.image[1] = w;
          display_ctx.image[2] = offs & 0xff;
          display_ctx.image[3] = offs >> 8;
          display_ctx.image[4] = offs2 & 0xff;
          display_ctx.image[5] = offs2 >> 8;
          display_ctx.modified_mask[0] |= 3;
        }
*/
        sprite_t sprite;
        sprite.w = w;
        sprite.h = h;
        sprite.black = black+offs;
        sprite.white = white+offs;
        display_sprite(px,py,&sprite);
        px += w;
      }
    }
    text++;
  }
}

void display_text(uint8_t x, uint8_t y, const font_t* font, const char* text) {
  display_text_(x,y,font,text,0);
}
void display_text_P(uint8_t x, uint8_t y, const font_t* font, const char* text) {
  display_text_(x,y,font,text,1);
}
#undef load_byte

#endif
