/*

  USART driver with configurable output buffer and character-input event.

  Copyright (c) 2018 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */


#ifndef __USART_H__
#define __USART_H__

#include <avr/pgmspace.h>

// baudrate may be predefined, defaults to 9600 baud
#ifndef baudrate
#define baudrate 9600
#endif

#ifndef outbuf_size
#define outbuf_size 20
#endif

char outbuf[outbuf_size];
int outbuf_len = 0;
int outbuf_start = 0;

//#define ATTR_CONST __attribute__((const))
//#define ATTR_ALIAS(func) __attribute__((alias(#func)))
//#define ATTR_WEAK __attribute__((weak))
//void EVENT_Stub(void) ATTR_CONST;
void EVENT_USART_Read(char c);// ATTR_WEAK ATTR_ALIAS(EVENT_Stub);

// double speed (U2X0) is always on.
// The interface is brought down first, to make sure the new settings are applied.
static void usart_init_ubrr(uint8_t ubrr) {
  // maybe: DDRD &= 0xfc; but then we assume RX/TX is always D0/D1...
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    UCSR0B = 0;
    // In theory we have to wait for any outgoing transmissions to complete...
    UBRR0 = ubrr;
    UCSR0A = 1<<U2X0; // set U2X0 (double speed), clear MPCM0 (address filter)
    UCSR0C = (0<<USBS0)|(3<<UCSZ00); // 8 data bits, 1 stop bit, no parity
    UCSR0B = (1<<RXEN0)|(1<<TXEN0); // enable TX and RX
    UCSR0B = (1<<RXCIE0)|(1<<RXEN0)|(1<<TXEN0); // enable TX and RX and RX-ISR
  }

  //UCSR0B |= (1<<RXCIE0);

  /*
  UBRR0 = F_CPU*1.0/baudrate/16-1;
     // 16M/9600/16-1 = 1M/9600-1 = 103.166 -> 103;
  //UBRR0 = 103; // select speed 9600 baud (taken from Table 20-6, ATmega spec.)
  UCSR0A = 0; // clear U2X0 (double speed) and MPCM0 (address filter)
  UCSR0B = (1<<RXEN0)|(1<<TXEN0); // enable TX and RX
  UCSR0C = (0<<UPM00)|(1<<USBS0)|(3<<UCSZ00); // 8 data bits, 2 stop bits, no parity
  */
  // USB-Serial-controller doesn't understand parity.
  // two stop bits are as good as one (of course...)
  // advantage of double speed? Maybe 8 samples per bit uses less power?
  // apparently my monitor program's baud rate is fixed to 9600 somehow...

  // even parity: (2<<UPM00)|
}

// baud = F_CPU/(UBRR0+1)/16

// bootloader-defined default:
//   10201806 = hex(UBRR0,UCSR0{A,B,C})
// --> 58823.5 baud; single speed, no MPCM0; TX&RX; no parity, 1 stop, 8bits
// .ino-selected:
//   CF 62 98 06
// --> 9615.4 baud; double speed, no MPCM0; TX&RX,RX-ISR; no parity, 1 stop, 8bits

// initialize to static baudrate. Avoids floating point division.
void usart_init() {
  uint8_t ubrr = F_CPU*1.0/8/baudrate-1;
  usart_init_ubrr(ubrr);
}

// initialize to given baudrate. Will use floating point division.
void usart_init_baud(uint32_t baud) {
  uint8_t ubrr = F_CPU*1.0/8/baud-1;
  usart_init_ubrr(ubrr);
}

// after initialization, the transmitter is enabled.
// to disable it again (to attach other transmitting hardware), use these:
// You need to manage Pin D1 configuration first, though
// (reasonable default is input with pull-up).
void usart_disable_transmitter() {
  UCSR0B &= ~(1<<TXEN0); // disable TX
  // This will only take effect once all pending output has been flushed.
  // Any output in the output buffer might be discarded.
}

void usart_enable_transmitter() {
  UCSR0B |= (1<<TXEN0); // enable TX
}

bool usart_can_read() {
  return UCSR0A & (1<<RXC0);
}

bool usart_pollchar(char* res) {
  bool ret = usart_can_read();
  if (ret) {
    *res = UDR0;
  }
  return ret;
}

char usart_readchar() {
  while (!usart_can_read());
  char res = UDR0;
  return res;
}

bool usart_can_write() {
  return UCSR0A & (1<<UDRE0);
}

// doesn't need an atomic block because we only use it in the ISR directly.
bool usart_pollwrite() {
  if (outbuf_len != 0 && usart_can_write()) {
    UDR0 = outbuf[outbuf_start];
    outbuf_len--;
    if (outbuf_len == 0) {
      outbuf_start = 0;
    } else {
      outbuf_start = (outbuf_start+1)%outbuf_size;
    }
    return 1;
  }
  return 0;
}

bool usart_writechar(char c) {
  /*
  if (outbuf_len > 0 && usart_can_write()) {
    usart_pollwrite();
  }
  */
  // we need an atomic block because we calculate start+len and each summand
  // may change in the ISR.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (outbuf_len < outbuf_size) {
      outbuf[(outbuf_start+outbuf_len)%outbuf_size] = c;
      outbuf_len++;
    } else {
      return 0;
    }
  }
  if (outbuf_len == 1) {
    UCSR0B |= (1<<UDRIE0);
    // debug: usart_writechar('M');
  }
  //usart_pollwrite();
  return 1;
}

/*
void usart_writechar(char c) {
  //usart_queuechar(c);

  while (!usart_can_write()); // wait till ready.
  // clear TXC0
  UDR0 = c;
}
*/

void usart_write(const char* s, int len) {
/*  if (len == -1) {
    len = strlen(s);
  }*/
  for (int i = 0; i < len; i++) {
    usart_writechar(s[i]);
  }
}

// s is a pointer to PROGMEM.
void usart_write_P(const char* s, int len) {
/*  if (len == -1) {
    len = strlen(s);
  }*/
  for (int i = 0; i < len; i++) {
    char c = pgm_read_byte(s+i);
    usart_writechar(c);
  }
}

// Note: if you use this macro multiple times for the same string, it will consume its size in progmem multiple times. Better to wrap it in an inline function then.
#define usart_msg(msg) usart_write_P(PSTR(msg),sizeof(msg)-1)

// gets the free space in the write buffer.
// TODO: Do we need an atomic block or not? Do we need volatile?
int usart_writable_space() {
  int len;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    len = (volatile int)outbuf_len;
  }
  return outbuf_size-len;
}

ISR(USART_RX_vect, ISR_BLOCK)
{
  char c = UDR0;
  //sei();
  EVENT_USART_Read(c);
}

ISR(USART_UDRE_vect, ISR_BLOCK)
{
  usart_pollwrite();
  if (outbuf_len == 0) {
    UCSR0B &= ~ (1<<UDRIE0);
  }
}

// Not needed:
//ISR(USART_TX_vect, ISR_BLOCK)

//#define usart_write(s) usart_write((s),strlen((s)))
/*void usart_write(char* s) {
  usart_write(s,strlen(s));
}*/

#endif
