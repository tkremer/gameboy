// for spi.h
#define NO_LUFA

#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <timers.h>
#define EVENT_QUEUE_SIZE 8
#include <events.c.h>
  
#define TIMER_DIV 1
#define TIMER_SCALE TIMER_SCALE_1
//#define TIMER_DIV 1024
//#define TIMER_SCALE TIMER_SCALE_DIV_1024
#define shortdelay msec2ticks(200.0,TIMER_DIV)
#define longdelay msec2ticks(800.0,TIMER_DIV)


#define EEPROMFS_VARS 2
#include <eeprom.h>
eepromfs_index_t eep_index;
//#define eep_segment_low 0
//#define eep_segment_high 1

#define ledpin 5
#define baudrate 9600
//#define baudrate 57600
//#define baudrate 19200
// 9600 19200 38400 57600 115200

// can have baudrates of up to 2000000 (usable!)

// some defines used by the last LUFA project, parts of which were included.
#define LEDS_LED1 (1<<ledpin)
#define LEDs_ToggleLEDs(mask) PINB = mask
#define LEDs_GetLEDs() (PORTB & LEDS_LED1)
#define LEDs_TurnOnLEDs(mask) PORTB |= mask
#define LEDs_TurnOffLEDs(mask) PORTB &= ~mask
#define ATTR_NO_INIT __attribute__((section(".noinit")))
#define ATTR_ALWAYS_INLINE __attribute__((always_inline))

#define outbuf_size 80
#include <usart.h>

#include <adc.h>
#include <adc_watch.h>
#include <spi.h>
#include "prng.h"
#include <pcd8544_display.h>
#include "testfont.h"

// those should better be used in menu and applications...
#define KEY_UP (1<<6)
#define KEY_RIGHT (1<<7)
#define KEY_LEFT (1<<0)
#define KEY_DOWN (1<<1)

#include "menu.h"

void process_char(char c);

static char hexchar(uint8_t i) {
  return i < 10 ? '0'+i : i < 16 ? 'A'-10+i : 'X';
}

void buftohex(const uint8_t* src, char* dest, int count) {
  for (int i = 0; i < count; i++) {
    uint8_t val = src[i];
    dest[2*i] = hexchar(val >> 4);
    dest[2*i+1] = hexchar(val & 15);
  }
  dest[2*count] = 0;
}

void inttohex(uint32_t value, char* dest, int count) {
  for (int i = 0; i < count; i++) {
    uint8_t val = (value & 0xf);
    value >>= 4;
    dest[count-1-i] = hexchar(val);
  }
  dest[count] = 0;
}

uint32_t hex2int(const char* s) {
  uint32_t res = 0;
  for (int i = 0; s[i] != 0 && i < 20; i++) {
    char c = s[i];
    char v = c-'0';
    if (v > 9) v = c-'A'+10;
    if (v > 15) v = c-'a'+10;
    if (v > 15 || v < 0) v = 0;
    res <<= 4;
    res |= v;
  }
  return res;
}

int16_t atan2_int16(int16_t y, int16_t x) {
  return (int16_t)(atan2(y,x)*(65536.0/2/M_PI));
  // we could use a more coarse spproximation here to get rid of floating
  // point operations.

/*
  // f1(x) = x-x^3/3 ~ arctan(x), |x| < 1
  // f2(x) = pi/4+x/2-x^2/2+x^3/2 ~ arctan(1+x), |x| < ?
  int16_t y1 = abs(y);
  int16_t x1 = abs(x);
  if (x1 > y1) {
    int16_t y2 = y1;
    y1 = x1;
    x1 = y2;
  }
  // x <= y.
  int16_t res;
  if (2*x < y) { // y/x < 1/2
    int32_t m = ((int32_t)y1*65536)/x1; // < 32768
    res = (int16_t)((m-((((m*m)>>16)*m)>>16)/3)/(65536*M_PI)); // f1(m)
  } else { // y/x > 1/2
    int32_t m = ((int32_t)(y1-x1)*65536)/x1; // <= 32768
    res = (int16_t)(16384+(m/2-((m*m)>>16)/2+((((m*m)>>16)*m)>>16)/2)/(65536*M_PI)); // f2(m)

  }

*/

/*
  // atan(y/x) = ...(y/(x^2+y^2)) = ...(x/(x^2+y^2))
  int16_t res = 0;
  bool flip = 0;
  if (y < 0) {
    y = -y;
    res -= 16384;
    flip = !flip;
  }
  if (x < 0) {
    x = -x;
    flip = !flip;
    res += (flip?-1:1)*16384;
  }
  if (x < y) {
    flip = !flip;
    int16_t x2 = x;
    x = y;
    y = x2;
  }
*/
}

bool snprintl(char* s, int len, int32_t value) {
  bool positive = value >= 0;
  bool ok = 1;
  uint32_t val;
/*  if (!positive) {
    val = -value;
  }*/
  if (positive) {
    val = value;
  } else {
    val = (uint32_t)(-value);
  }
  int i = 0;
  while (i < len && val != 0) {
    char v0 = val % 10;
    val /= 10;
    s[len-1-i] = '0'+v0;
    i++;
  }
  if (i == 0) {
    s[len-1] = '0';
    i++;
  }
  if (!positive) {
    if (i == len-1) {
      i--;
      ok = 0;
    }
    s[len-1-i] = '-';
    i++;
  }
  if (val != 0) {
    ok = 0;
  }
  while (i < len) {
    s[len-1-i] = ' ';
    i++;
  }
  return ok;
}

// --- power management ---

void poweroff() {
  cli();
  events_clear();
  PORTB &= ~(1<<ledpin);
  while(true) {
    // disable interrupts
    cli();
    // disable watchdog
    wdt_reset();
    MCUSR &= ~(1 << WDRF);
    wdt_disable();
    // disable Analog Comparator (to make sure that internal AREF is not used)
    ACSR = 1 << ACD;
    // disable ADC (ref. says "The ADC must be disabled before shutdown")
    // also disable all digital input buffers on ADC pins. Should happen
    // automatically anyway, but better to be explicit.
    adc_conf(false,0xff,0);
    // shut down all peripherals
    power_all_disable();
    // go to sleep
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_bod_disable();
    sleep_cpu();
    // should not be reached. But who knows for sure?
    PORTB |= 1<<ledpin; // If something wrong, at least notify the user...
  }
}

// --- application ---


// The device loads a saved state at bootup and maybe later again.
// It saves a state when requested by the user.

void save_state() {
/*  uint64_t syms = segment_display_symbols;
  uint32_t seg_lo,seg_hi;
  seg_lo = syms & 0xffffffff;
  seg_hi = syms >> 32;
  eepromfs_put(&eep_index,eep_segment_low,&seg_lo);
  eepromfs_put(&eep_index,eep_segment_high,&seg_hi);
*/
}

void load_state() {
/*
  uint32_t seg_lo,seg_hi;
  if (eepromfs_get(&eep_index,eep_segment_low,&seg_lo) &&
      eepromfs_get(&eep_index,eep_segment_high,&seg_hi)) {
    uint64_t seg_syms = ((uint64_t)seg_hi << 32) | seg_lo;
    segment_display(seg_syms);
  }
*/
}

// FIXED: make baud parameter for usart_init().
void set_baud(uint32_t baud) {
  usart_init_baud(baud);
}

uint16_t adc_val;

/*
// 600 bytes for sin, cos and M_PI. That's overkill for our purpose.
static int saw_cos(int max, int16_t t) {
  return max - abs((int32_t)t*max/(1<<14));
}
*/

/*
static const char main_menu_entry0_str[] = "Game of Life";
menuentry_t main_menu_entries[8] = {
  {main_menu_entry0_str,1},{"Bar",2},{"Baz",3},{"Tetris",4},{"Snake",5},{"Foobar",6},{"Blocks",7},{"Voltmeter",8}
};

menu_t main_menu = {
  .parent = 0,
  .count = 8,
  .entries = main_menu_entries
};
*/
#include "applications.h"

//menu_ctx_t menu_ctx;
//bool menu_is_open = false;

const uint8_t black2[] PROGMEM = { 0x06,0x0b,0x09,0x06 },
              white2[] PROGMEM = { 0xff,0xfb,0xf9,0xff };
void display_test(uint32_t arg) {
  //uint8_t count = display_try;
  uint8_t x = arg & 0xff, y = (arg >> 8) & 0xff;
  switch (arg >> 24) {
    case 0:
      for (int i = 0; i < ((arg>>16)&0xff); i++)
        display_putpixel(i*i/2+x,i+y,1);
      break;
    case 1:
      {
        uint8_t black[] = { 0x06,0x0b,0x09,0x06 },
                white[] = { 0xff,0xfb,0xf9,0xff };
        sprite_t sprite = { 4,4, black, white };
        display_sprite(x,y,&sprite);
      }
      break;
    case 2:
      {
        sprite_t sprite = { 4,4, pgm_addr(&black2[0]), pgm_addr(&white2[0]) };
        display_sprite(x,y,&sprite);
      }
      break;
    case 3:
      {
        char msg[6] = "Hello";
        uint8_t i = (arg >> 16) & 0xff;
        if (i < sizeof(msg)) msg[i] = 0;
        display_text(x,y,&testfont,&msg[0]);
      }
      break;
    case 4:
      {
        display_text_P(x,y,&testfont,PSTR("Hello World!"));
      }
      break;
    case 5:
      display_clear();
      break;
    case 6:
      // test menus.
/*
      if (x == 0) {
        menu_open(&menu_ctx,&main_menu,&testfont);
      } else {
        menu_keypressed(&menu_ctx,x);
      }
*/
      break;
    case 7:
      // invalidate all to reveal what would be shown if the invalidation code was used correctly...
      display_invalidate_all();
      //display_invalidate_rect(0,0,LCD_WIDTH,LCD_HEIGHT);
      break;
  }
}

void led_blink_event(void* param) {
  LEDs_ToggleLEDs(LEDS_LED1);

  int16_t i = (int16_t)param;

  if (i > 0) {
    i--;
    enqueue_event_rel(msec2ticks(500,TIMER_DIV),&led_blink_event,(void*)i);
  }
}

void status_report_event(void* param) {
  usart_msg("stat: ");
/*  char msg[10];
  buftohex((uint8_t*)&rfm12_status,&msg[0],2);
  msg[4] = ' ';
  buftohex((uint8_t*)&rfm12b_status,&msg[5],2);
  usart_write(msg,9);*/
  usart_msg(".\n");
  enqueue_event_rel(msec2ticks(1000,TIMER_DIV),&status_report_event,param);
}

uint8_t recent_pins[3] = {0,0,0};
int32_t last_keypress_time = 0;

void EVENT_Interrupt(uint8_t port, uint8_t pins) {
  // fill the entropy into the random buffer:
  int32_t time = get_time();
  prng_write_byte(time & 0xff);

  char msg[10] = "r0 ......\n";
  msg[1] = '0'+port;
  inttohex(pins,&msg[3],2);
  msg[5] = '\n';
  msg[6] = 0;
  uint8_t changedpins = pins ^ recent_pins[port];

  //  D6,D7,B0,B1: button ports, input (up,right,left,down)
  uint8_t mask = port == 0 ? 0x03 : port == 2 ? 0xc0 : 0;
  changedpins &= mask;

/*  if (changedpins) {
    // can have keydown and keyup here    
  }*/
  // only interested in keydown for now:
/*
  uint8_t newpins = (~pins) & changedpins;
  if (newpins && ((time - last_keypress_time) > msec2ticks(100,TIMER_DIV)) ) {
    if (!menu_is_open) {
      menu_open(&menu_ctx,&main_menu,&testfont);
      menu_is_open = true;
    }
    menu_keypressed(&menu_ctx,newpins);
    last_keypress_time = time;
    if (usart_writable_space() > 11) {
      usart_write((char*)&time,4);
      usart_write((char*)&last_keypress_time,4);
      usart_writechar(changedpins);
      usart_writechar(newpins);
      usart_writechar('\n');
    }
  }
*/
  /*
  inttohex(PINB,&msg[3],2);
  inttohex(PINC,&msg[5],2);
  inttohex(PIND,&msg[7],2);
  msg[9] = '\n';
  msg[10] = 0;*/
  recent_pins[port] = pins;
  if (changedpins && ((time - last_keypress_time) > msec2ticks(20,TIMER_DIV)) ) {
    last_keypress_time = time;
    if (current_application == 0xffff) {
      application_start(0); // starts default application (menu).
    }
    application_keypressed((~recent_pins[0] & 0x03) | (~recent_pins[2] & 0xc0));
  }
  if (usart_writable_space() > 7) {
    usart_write(msg,6);
  }
}

ISR (PCINT0_vect, ISR_BLOCK)
{
  EVENT_Interrupt(0,PINB);
}

ISR (PCINT1_vect, ISR_BLOCK)
{
  EVENT_Interrupt(1,PINC);
}

ISR (PCINT2_vect, ISR_BLOCK)
{
  EVENT_Interrupt(2,PIND);
}

void setup_Interrupts(uint8_t maskB, uint8_t maskC, uint8_t maskD) {
  PCICR = ((maskB?1:0) << PCIE0)
        | ((maskC?1:0) << PCIE1)
        | ((maskD?1:0) << PCIE2);
  // PCIFR & (1<< PCIF0)
  PCMSK0 = maskB;
  PCMSK1 = maskC;
  PCMSK2 = maskD;
}
 
void buttons_init() {
  // enable pull-ups for B0,B1,D6,D7
  PORTB |= 0x03;
  PORTD |= 0xc0;
  
  setup_Interrupts(0x03,0,0xc0); // B0,B1,D6,D7

  recent_pins[0] = PINB;
  recent_pins[1] = PINC;
  recent_pins[2] = PIND;
}


#define inbuf_size 80
char inbuf[inbuf_size+1];
int inbuf_len = 0;

int aread_count = 0;

void process_line() {
  char *s = inbuf;
  if (inbuf_len == 0)
    return;
  if (s[0] != '!') {
    int len = inbuf_len;
/*    uint64_t syms = segment_str2syms(s);
    if (len < 8) {
      uint64_t old_syms = segment_display_symbols;
      syms = (syms << (8*(8-len))) | (old_syms >> (8*len));
    }
    segment_display(syms);*/
    usart_write(inbuf,len); // echo input
    usart_writechar('\n');
  } else if (inbuf_len >= 2) {
    char cmd = s[1];
    char *param = &s[2];
    switch (cmd) {
      case '0': {
          usart_msg("VERSION 3\n");
        }
        break;
      case 'a': {
          int value = hex2int(param);
          aread_count = value;
        }
        break;
      case 'b': {
          uint32_t baud = hex2int(param);
//          uint64_t syms = segment_int2syms(baud);
          set_baud(baud);
//          segment_display(syms);
          usart_msg("OK.\n");
        }
        break;
      case 'd': {
          //display_try = hex2int(param);
          display_test(hex2int(param));
          usart_msg("OK.\n");
        }
        break;
      case 'D': {
          //display_try = 0;
          display_init();
        }
        break;
      case 'f': {
          uint16_t count = hex2int(param);
          enqueue_event_rel(1,&led_blink_event,(void*)count);
        }
        break;
      case 'F': {
          dequeue_events(&led_blink_event);
        }
        break;
      case 'G': {
          uint8_t num = hex2int(param);
          usart_write(inbuf,inbuf_len);
          uint32_t value;
          value = adcw_state.values[num % 8];
          char msg[10];
          inttohex(value,msg,9);
          usart_writechar(' ');
          usart_write(msg,9);
          usart_writechar('\n');
        }
        break;
      case 'l':
        load_state();
        usart_msg("OK.\n");
        break;
      case 'P': {
          uint8_t port = PINB;
          usart_writechar(port);
          usart_writechar('\n');
        }
        break;
      case 's':
        save_state();
        usart_msg("OK.\n");
        break;
      case 'x': {
          dequeue_events(&led_blink_event);
          LEDs_TurnOffLEDs(LEDS_LED1);
        }
        break;
      case 'y': {
          dequeue_events(&status_report_event);
          enqueue_event_rel(1,&status_report_event,(void*)0);
        }
        break;
      case 'Y': {
          dequeue_events(&status_report_event);
        }
        break;
      case 'z':
        poweroff();
        break;
    }
  }
}

void process_char(char c) {
  if (c == 10 || c == 13) {
    if (inbuf_len < inbuf_size) {
      inbuf[inbuf_len] = 0;
      process_line();
    }
    inbuf_len = 0;
  } else if (inbuf_len < inbuf_size) {
    if (c > 0x80 || c < 0x20) {
      inbuf_len = inbuf_size;
    } else {
      inbuf[inbuf_len] = c;
      inbuf_len++;
    }
  } // else ignore more characters and in the end the whole line.
}

void EVENT_USART_Read(char c) {
  // fill the entropy into the random buffer:
  prng_write_byte(get_time() & 0xff);

  process_char(c);
}

// let's take C3 as force-input and ADC6/ADC7 as two-phase rotary position
// input (magnet sensors).

void EVENT_adc_watch(uint8_t channel, int16_t value) {
#if 0
//  if (aread_count > 0) {
  if (usart_writable_space() >= 20) {
    // we don't want incomplete messages to saturate the connection.
    char msg[11] = {'a','d','c','0','=','0','0','0','0','\n'}; // TODO: can/should we PSTR here?
    msg[0] = (string_state.pos >> 16) & 0xff;
    msg[1] = (string_state.pos >> 8) & 0xff;
    snprintl(&msg[5],4,value);
/*    msg[3] = '0'+channel;
    msg[8] = '0'+value%10;
    msg[7] = '0'+(value/10)%10;
    msg[6] = '0'+(value/100)%10;
    msg[5] = '0'+(value/1000)%10;*/
    usart_write(&msg[0],10);
//    aread_count--;
//    bool ok = ((value > adcw_state.max[channel]) || (value < adcw_state.min[channel]));

/*    usart_write((char*)&adcw_state.min[channel],2);
    usart_write((char*)&adcw_state.max[channel],2);
    usart_write((char*)&value,2);
    usart_writechar('0'+ok);
    usart_writechar('\n');*/
  }
#endif
  //adc_watch_set_range(channel,value-20,value+20);
  adc_watch_set_range(channel,value-2,value+2);
  //value -= 512;
}


/*
  project pins:
    D3,D4,D5: display ports, output (TODO: which ones?)
      There are: SS, D/C and reset...
    D6,D7,B0,B1: button ports, input (up,right,left,down)
                 (TODO: That's PCINT2 and PCINT0.)
    B3,B5: SDO,SCK for display
    3V3,GND for display, GND for buttons.
*/

void startup() {
  // disable watchdog if enabled
  wdt_reset();
  MCUSR &= ~(1 << WDRF);
  wdt_disable ();

  // disable any output, just in case
  DDRB = 0;
  DDRC = 0;
  DDRD = 0;
  // enable pullups
  PORTB = 0xff;
  PORTC = 0xff;
  PORTD = 0xff;

  // configure pins
  PORTB &= ~(1<<ledpin);
  DDRB |= 1<<ledpin;

  //adc_conf(true,0,7); // enable, no adc pins, slow prescaler
  // spec says 50..200kHz for accuracy, we have 16M for cpu, need factor 2^7.

  // setup event processing
  events_start(TIMER_SCALE);

  usart_init();
  buttons_init();

  adc_conf(true,0xc8,ADC_DIV_128); // pins C3, ADC6 and ADC7
  adc_watch_init(0xc8);
  //adc_watch_start();

//  segment_display(0x0202020202020202);
  NONATOMIC_BLOCK(NONATOMIC_FORCEOFF) {
    eepromfs_index(&eep_index,0,0);
  }

  display_init();

  load_state();
}

int main() {

  startup();
  prng_seed(0);
  application_start(0); // starts default application (menu).

  sei();


  // prepare for sleeping
  set_sleep_mode(SLEEP_MODE_IDLE);
   // _ADC, _PWR_DOWN, _PWR_SAVE, _STANDBY, _EXT_STANDBY
   // PWR_SAVE leaves Timer2 active, PWR_DOWN is rather off.
  while(true) {
    // let the ISRs handle the events.
    sleep_mode();
      /* or:
           sleep_enable(); sei(); sleep_cpu(); sleep_disable();
         for avoiding race conditions we don't have.
      */
  }
  return 0;
}
