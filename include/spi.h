/*

  SPI functions, both synchronous and asynchronous.
  Detects, if LUFA is used and uses its SPI.h then.

  Copyright (c) 2016 Thomas Kremer

*/

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 */

#ifndef __MY_SPI_H__
#define __MY_SPI_H__

//#ifndef NO_LUFA
// defined in LUFA/Version.h. Include before this file if you use LUFA.
#ifdef LUFA_VERSION_INTEGER
#include <LUFA/Drivers/Peripheral/SPI.h>
#endif

/*

  SPI.h is nice in the first place, but it really lacks an asynchronous view
  to the operations. We do not want to just busy-wait for an answer. We rather
  send our byte, do stuff and when we have the time, we check the received
  byte (e.g. right before we send the next one).

*/

static inline void SPI_AsyncTransferByte(const uint8_t Byte) ATTR_ALWAYS_INLINE;
static inline uint8_t SPI_AsyncWaitResult(void) ATTR_ALWAYS_INLINE;
static inline bool SPI_AsyncResultReady(void) ATTR_ALWAYS_INLINE;

static inline void SPI_AsyncTransferByte(const uint8_t Byte)
{
  SPDR = Byte;
}

static inline bool SPI_AsyncResultReady(void)
{
  return (SPSR & (1 << SPIF));
}

static inline uint8_t SPI_AsyncWaitResult(void)
{
  while (!SPI_AsyncResultReady());
  return SPDR;
}

#define On_SPI_Complete ISR(SPI_STC_vect, ISR_BLOCK)
// usage: On_SPI_Complete { do_something(); ... }
//#ifdef NO_LUFA
#ifndef LUFA_VERSION_INTEGER

static inline uint8_t SPI_TransferByte(const uint8_t byte) ATTR_ALWAYS_INLINE;
static inline void SPI_Enable() ATTR_ALWAYS_INLINE;
static inline void SPI_Disable() ATTR_ALWAYS_INLINE;

static inline uint8_t SPI_TransferByte(const uint8_t byte) {
  SPI_AsyncTransferByte(byte);
  return SPI_AsyncWaitResult();
}

// SPR1 << 2 | SPR0 << 1 | SPI2X << 0
#define SPI_DIV_2 1
#define SPI_DIV_4 0
#define SPI_DIV_8 3
#define SPI_DIV_16 2
#define SPI_DIV_32 5
#define SPI_DIV_64 4
#define SPI_DIV_128 7
#define SPI_DIV_64b 6

#define SPI_MODE_SAMPLE_TRAILING (1<<CPHA) //2
#define SPI_MODE_SAMPLE_LEADING (0<<CPHA)
#define SPI_MODE_IDLE_HIGH (1<<CPOL) //3
#define SPI_MODE_IDLE_LOW (0<<CPOL)
#define SPI_MODE_MASTER (1<<MSTR) // 4
#define SPI_MODE_SLAVE (0<<MSTR)
#define SPI_MODE_LSB (1<<DORD) // 5
#define SPI_MODE_MSB (0<<DORD)

#define SPI_MODE_INTERRUPT (1<<SPIE)

static inline void _SPI_presetup(uint8_t mode) {
  if (mode & SPI_MODE_MASTER) {
    // set SS to output high (unselected slave)
    // if set to input, it would enable shared-master mode.
    PORTB |= 1<<2;
    DDRB |= 1<<2;
  }
}
static inline void _SPI_postsetup(uint8_t mode) {
  volatile uint8_t dummy __attribute__(( unused )) = SPDR;
     // To clear the interrupt flag and the collision flag.
  // Set output for MISO, MOSI and SCK;
  //   master/slave mode will override the neccessary input pins.
  DDRB |= (1<<3) | (1<<4) | (1<<5);
/*  if (mode & SPI_MODE_MASTER) {
    // enable MOSI and SCK
    DDRB |= (1<<3) | (1<<5);
  } else {
    // enable MISO
    DDRB |= 1<<4;
  }*/
}

// pins are: SS=B2, MOSI=B3, MISO=B4, SCK=B5 on ATmega328P
//           SS=B0, SCLK=B1, MOSI=B2, MISO=B3 on AT90USB162 (not supported for now, use LUFA for that one)
// bit order, data mode
static inline void SPI_Init(uint8_t mode, uint8_t div) {
  _SPI_presetup(mode);
  SPSR = (1<<SPIF) | ((div & 1) << SPI2X);
  SPCR = mode | (1<<SPE) | (div >> 1);
  _SPI_postsetup(mode);
}

static inline void SPI_Enable_Interrupt() {
  SPCR |= SPI_MODE_INTERRUPT;
}

static inline void SPI_Disable_Interrupt() {
  SPCR &= ~SPI_MODE_INTERRUPT;
}

static inline void SPI_Enable() {
  uint8_t mode = SPCR;
  if (!(mode & (1<<SPE))) {
    _SPI_presetup(mode);
    SPCR = mode | (1 << SPE);
    _SPI_postsetup(mode);
  }
}

static inline void SPI_Disable() {
  // disable all output pins.
  DDRB &= 0xc3; // ~(1 << [2,3,4,5])
  // activate pull-ups, just in case...
  PORTB |= 0x3c;
  SPCR &= ~(1 << SPE);
}

#endif // NO_LUFA

#endif // __MY_SPI_H__
