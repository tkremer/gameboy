#ifdef APP_ENTRY
  APP_ENTRY("1-Wire",onewire_start,onewire_stop,onewire_keypressed)
#else

#include <util/crc16.h>

// 1-wire specs (from datasheet)
// all times in µs

// we mostly use the minimum timings because the pull-up time adds to it.
#define onewire_reset_duration_min 480
#define onewire_reset_duration_max 960
#define onewire_reset_duration_use 480
#define onewire_presence_time_min 15
#define onewire_presence_time_max 60
#define onewire_presence_duration_min 60
#define onewire_presence_duration_max 240
#define onewire_presence_sample_time 40
// time between end-of-reset and first bit, covering the presence pulse
#define onewire_presence_window 480
#define onewire_bit_1_duration_min 1
#define onewire_bit_1_duration_max 15
#define onewire_bit_1_duration_use 2
#define onewire_bit_0_duration_min 60
#define onewire_bit_0_duration_max 120
#define onewire_bit_0_duration_use 60
#define onewire_bit_sample_time_min 2 // 1
#define onewire_bit_sample_time_max 15
#define onewire_bit_sample_time_use 13
#define onewire_bit_duration_min 61
#define onewire_bit_duration_use 61 //(4*61)

#define onewire_cmd_readrom 0x33
#define onewire_cmd_match 0x55
#define onewire_cmd_skip 0xcc
#define onewire_cmd_search 0xf0
#define onewire_cmd_alarmsearch 0xec

#define PRE_PASTE(a,b) a##b
#define CONCAT(a,b) PRE_PASTE(a,b)

#define onewire_port C
#define onewire_port_PORT CONCAT(PORT,onewire_port)
#define onewire_port_PIN CONCAT(PIN,onewire_port)
#define onewire_port_DDR CONCAT(DDR,onewire_port)
#define onewire_pin 4

#define onewire_pulse(t) do {\
  onewire_port_PORT &= ~(1<<onewire_pin);\
  onewire_port_DDR |= (1<<onewire_pin);\
  _delay_us(t);\
  onewire_port_DDR &= ~(1<<onewire_pin);\
  onewire_port_PORT |= (1<<onewire_pin);\
} while (0)

// TODO: substitute all the last delays by a next-bit-ok-time.

// true if there is a peer.
bool onewire_reset() {
  //uint32_t time = get_time();
  onewire_pulse(onewire_reset_duration_use);
  _delay_us(onewire_presence_time_max+onewire_presence_sample_time);
  uint8_t res = onewire_port_PIN & (1<<onewire_pin);
  _delay_us(onewire_presence_window-onewire_presence_time_max-onewire_presence_sample_time);
  return res == 0;
}

void onewire_write_bit(uint8_t bit) {
  if (bit) {
    onewire_pulse(onewire_bit_1_duration_use);
    _delay_us(onewire_bit_duration_use-onewire_bit_1_duration_use);
  } else {
    onewire_pulse(onewire_bit_0_duration_use);
    _delay_us(onewire_bit_duration_use-onewire_bit_0_duration_use);
  }
}

uint8_t onewire_read_bit() {
  onewire_pulse(onewire_bit_sample_time_min);
  _delay_us(onewire_bit_sample_time_use-onewire_bit_sample_time_min);
  uint8_t res = onewire_port_PIN & (1<<onewire_pin);
  _delay_us(onewire_bit_duration_use-onewire_bit_sample_time_use);
  return res ? 1 : 0;
}

// writing/reading goes LSB first

void onewire_write_byte(uint8_t val) {
  for (int8_t i = 0; i < 8; i++) {
    onewire_write_bit(val & 1);
    val >>= 1;
  }
}

uint8_t onewire_read_byte() {
  uint8_t res = 0;
  for (int8_t i = 0; i < 8; i++) {
    res >>= 1;
    res |= onewire_read_bit() ? 1<<7 : 0;
  }
  return res;
}

void onewire_write_buf(const void* buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    onewire_write_byte(((const uint8_t*)buf)[i]);
}

void onewire_read_buf(void* buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    ((uint8_t*)buf)[i] = onewire_read_byte();
}

typedef uint64_t onewire_addr_t;

// A device can often be uniquely identified by just one
// byte containing the list of decisions made during search arbitration.
// at least 8 devices are addressable this way, up to 255 if the decision tree
// is well balanced.
// by using 2/4 bytes you could increase that range to 16/32 -- 2^16-1/2^32-1
// addressable devices, though at that point you might as well use the full
// address.
typedef uint8_t onewire_id_t;
#define onewire_id_bits (sizeof(onewire_id_t)*8)
#define onewire_id_invalid ((onewire_id_t)-1LL)

// addr may be NULL to address all devices
void onewire_select(const onewire_addr_t* addr) {
  if (addr != NULL) {
    onewire_write_byte(onewire_cmd_match);
    onewire_write_buf(addr,8);
  } else {
    onewire_write_byte(onewire_cmd_skip);
  }
}

// short_id is valid as long as the device list does not change.
// at least 8 devices can be addressed using short_id,
// up to 255 if you're lucky. The short_id 0xff is the invalid id.
// short_id represents the path in the decision tree that leads to the device.
typedef struct {
  onewire_addr_t addr;
  int8_t last_choice;
  uint8_t type;
  onewire_id_t short_id;
} onewire_search_t;

void onewire_search_init(onewire_search_t* ctx, bool alarm) {
  ctx->addr = 0;
  ctx->last_choice = -1;
  ctx->type = alarm ? onewire_cmd_alarmsearch : onewire_cmd_search;
}

// returns false if no more devices were found, otherwise returns true and
// fills ctx->addr with another device's address.
// Also fills ctx->short_id, but it is valid only in non-alarm searches.
bool onewire_search_next(onewire_search_t* ctx) {
  onewire_addr_t addr = ctx->addr;
  int8_t last_choice = ctx->last_choice;
  int8_t next_choice = -2;
  onewire_id_t short_id = 0;
  uint8_t short_id_bits = 0;
  if (last_choice == -2 || !onewire_reset()) {
    return false;
  }
  onewire_write_byte(ctx->type);
  for (int8_t i = 0; i < 64; i++) {
    uint8_t bit = onewire_read_bit();
    uint8_t notbit = onewire_read_bit();
    if (bit == notbit) {
      if (bit == 0) {
        // both bits are present
        if (i < last_choice) {
          bit = addr & 1;
        } else if (i == last_choice) {
          bit = 1;
        } else {
          bit = 0;
        }
        short_id >>= 1;
        short_id_bits++;
        if (bit == 0)
          next_choice = i;
        else short_id |= 1LL<<(onewire_id_bits-1);
      } else {
        // no devices match
        return false;
      }
    } else {
      // sanity check?
      /*if (bit != (addr & 1)) {
        return false;
      }*/
    }
    onewire_write_bit(bit);
    addr >>= 1;
    if (bit) addr |= 1LL << 63;
  }
  if (short_id_bits < onewire_id_bits) {
    if (short_id_bits != 0)
      short_id >>= (onewire_id_bits-short_id_bits);
  } else if (short_id_bits > onewire_id_bits) {
    short_id = onewire_id_invalid;
  }
  ctx->addr = addr;
  ctx->last_choice = next_choice;
  ctx->short_id = short_id;
  return true;
}

// select by short_id.
// addr_out may be NULL, otherwise is filled with the actual address
bool onewire_select_short(onewire_id_t short_id, onewire_addr_t *addr_out) {
  // we don't have to initialize *addr_out, as it is shifted out anyway.
  uint8_t short_id_bits = 0;
  if (short_id == onewire_id_invalid || !onewire_reset()) {
    return false;
  }
  onewire_write_byte(onewire_cmd_search);
  for (int8_t i = 0; i < 64; i++) {
    uint8_t bit = onewire_read_bit();
    uint8_t notbit = onewire_read_bit();
    if (bit == notbit) {
      if (bit == 0 && short_id_bits < onewire_id_bits) {
        // both bits are present
        bit = short_id & 1;
        short_id >>= 1;
        short_id_bits++;
      } else {
        // no devices match or we've run out of addressing bits.
        return false;
      }
    }
    onewire_write_bit(bit);
    if (addr_out) {
      *addr_out >>= 1;
      if (bit) *addr_out |= 1LL << 63;
    }
  }
  return short_id == 0LL;
}

// do we actually need the crc8?
// well, it's already implemented in util/crc16.h, so we might just use it.
// crc8(first 7 bytes of address) = 8th byte of address
// crc8(address) == 0
uint8_t onewire_crc8(const void* buf,size_t size) {
  uint8_t crc = 0;
  for (size_t i = 0; i < size; i++)
    crc = _crc_ibutton_update(crc,((const uint8_t*)buf)[i]);
  return crc;
}

bool onewire_address_crc_valid(const onewire_addr_t* addr) {
  return onewire_crc8(addr,sizeof(addr)) == 0;
}

// DS18B20

#define ds18x20_cmd_write_scratchpad 0x4e
#define ds18x20_cmd_read_scratchpad 0xbe
#define ds18x20_cmd_copy_scratchpad 0x48
#define ds18x20_cmd_convert_T 0x44
#define ds18x20_cmd_recall_e2 0xb8
#define ds18x20_cmd_read_power 0xb4
#define ds18b20_familycode 0x28
// one degree celsius is this many returned units.
#define ds18b20_one_degree 16
#define ds18b20_temperature_fractional_bits 4
#define ds18b20_conversion_time(bits) (750000>>(12-(bits))) // µs

// whenever possible, the functions operate on the currently selected device.
// That way, enumerations with onewire_search can be used directly.

void ds18b20_start_conversion() {
  onewire_write_byte(ds18x20_cmd_convert_T);
}

void ds18b20_save_config() {
  onewire_write_byte(ds18x20_cmd_copy_scratchpad);
}

void ds18b20_reload_config() {
  onewire_write_byte(ds18x20_cmd_recall_e2);
}

bool ds18b20_read_power() {
  onewire_write_byte(ds18x20_cmd_read_power);
  return onewire_read_bit();
}

// scratch must be 3 bytes long
void ds18b20_write_scratchpad(const void* scratch) {
  onewire_write_byte(ds18x20_cmd_write_scratchpad);
  onewire_write_buf(scratch,3);
}

// up to 9 bytes are read.
void ds18b20_read_scratchpad(void* scratch, uint8_t size) {
  if (size > 9)
    size = 9;
  onewire_write_byte(ds18x20_cmd_read_scratchpad);
  onewire_read_buf(scratch,size);
}

// can only be used on a complete scratchpad (9 bytes)
bool ds18b20_scratchpad_crc_valid(const void* scratch) {
  return onewire_crc8(scratch,9) == 0;
}

// addr may be NULL to set all. Don't do that if you have other types of
// devices on that bus.
// note, that T_low and T_high are in degrees celsius (I think).
void ds18b20_configure(int8_t T_low, int8_t T_high, uint8_t bits) {
  if (bits < 9) bits = 9;
  else if (bits > 12) bits = 12;
  uint8_t config = (bits-9) << 5;
  uint8_t scratch[3] = { T_high, T_low, config };
  ds18b20_write_scratchpad(&scratch);
  onewire_reset(); // just to be safe, that the write really happens.
}

// read-modify-write semantics require a device address to be used.
void ds18b20_set_device_precision(const onewire_addr_t* addr, uint8_t bits) {
  if (bits < 9) bits = 9;
  else if (bits > 12) bits = 12;
  uint8_t config = (bits-9) << 5;
  if (!onewire_reset())
    return;
  onewire_select(addr);
  uint8_t scratch[5];
  ds18b20_read_scratchpad(scratch, 5);
  scratch[4] = config;
  onewire_reset();
  onewire_select(addr);
  ds18b20_write_scratchpad(&scratch[2]);
  onewire_reset(); // just to be safe, that the write really happens.
}

// only uses the first 2 bytes.
// result is in 1/16 degrees celsius
int16_t ds18b20_temperature_from_scratchpad(void* scratch) {
  int16_t res = *(int16_t*)scratch; // hard-coded little-endianness
  return res;
}

// result is in 1/16 degrees celsius
int16_t ds18b20_read_temperature() {
  uint8_t scratch[2];
  ds18b20_read_scratchpad(scratch,2);
  return ds18b20_temperature_from_scratchpad(scratch);
}

// may be issued after save, reload and start_conversion arbitrarily often.
bool ds18b20_read_busy() {
  return !onewire_read_bit();
}

bool is_ds18b20(const onewire_addr_t* addr) {
  uint8_t type = *addr & 0xff;
  return type == ds18b20_familycode;
}

// -------- actual application

uint8_t onewire_pos;

// converts value/2^decimalbits according to "%<intdigits>.<decimaldigits>f"
// dest must fit intdigits+decimaldigits+2 chars
void fixpoint_to_str(int32_t value, uint8_t decimalbits, uint8_t intdigits, uint8_t decimaldigits, char* dest) {
  for (int8_t i = 0; i < decimaldigits; i++)
    value *= 10;
  value >>= decimalbits;
  snprintl(dest,intdigits+decimaldigits,value);
  // we're using the right-justification if snprintl here:
  // however, we need to drag any minus sign to the front.
  bool sign = false;
  for (int8_t i = intdigits+decimaldigits; i >= intdigits; i--) {
    char c = dest[i];
    if (c == ' ') c = '0';
    if (c == '-') {
      c = '0';
      sign = true;
    }
    dest[i+1] = c;
  }
  if (intdigits > 0) {
    if (dest[intdigits-1] == ' ')
      dest[intdigits-1] = '0';
    if (dest[intdigits-1] == '-') {
      dest[intdigits-1] = '0';
      sign = true;
    }
  }
  dest[intdigits] = '.';
  if (sign) {
    if (intdigits > 1)
      dest[intdigits-2] = '-';
    else dest[0] = '-';
  }
  dest[intdigits+decimaldigits+1] = 0;
}

#define onewire_listing_interval msec2ticks(5000,TIMER_DIV)

void onewire_update_display(bool do_configure)
{
  onewire_search_t ctx;
  char line[17];
  onewire_search_init(&ctx,false);
  int16_t i = 0;
  display_clear();
  if (onewire_reset()) {
    while (onewire_search_next(&ctx)) {
      int16_t y = i-(onewire_pos>>1);
      if (y >= 0 && y < LCD_HEIGHT/8) {
        buftohex((uint8_t*)&ctx.addr,&line[0],8);
        line[16] = 0;
        display_text(LCD_WIDTH/2-8*5,y*8,&testfont,line);
      }
      if (onewire_crc8(&ctx.addr,sizeof(ctx.addr)) != 0) {
        display_rect(1,y*8+3,1,2,COLOR_BLACK);
      }
      if (is_ds18b20(&ctx.addr)) {
        if (do_configure) {
          ds18b20_configure(-100,100,12);
        } else if (onewire_pos & 1) {
          int16_t temp = ds18b20_read_temperature();
          inttohex(ctx.short_id,&line[0],2);
          //line[0] = ' ';
          fixpoint_to_str(temp,ds18b20_temperature_fractional_bits,4,2,&line[2]);
          //inttohex(temp,&line[2],4);
          // temp == 1, x=temp, temp*=100, temp>>4 == 7
          // x=100, x>>4 == 6
          //int32_t x = temp;
          //volatile int32_t x = -100;
          //x >>= 4;
          //snprintl(&line[2],6,x);
          //inttohex(x,&line[0],8);
          //line[8] = 0;
          display_rect(LCD_WIDTH-9*5-2,y*8,2,8,COLOR_WHITE);
          display_text(LCD_WIDTH-9*5,y*8,&testfont,line);
        }
      }
      get_time(); // we don't want to block the timer overflow logic.
      i++;
    }
    // we can fit 6 lines on the display
    if (i > 6) {
      int16_t h = LCD_HEIGHT*6/i;
      int16_t y = (LCD_HEIGHT-h)*(onewire_pos>>1)/(i-6);
      if (y < LCD_HEIGHT)
        display_rect(0,y,1,h,COLOR_BLACK);
      /*snprintl(&line[0],4,i);
      line[4] = 0;
      display_text(LCD_WIDTH-4*5,0,&testfont,line);*/
    }
    if (i == 0) {
      onewire_reset();
      onewire_write_byte(onewire_cmd_readrom);
      onewire_read_buf(&ctx.addr,8);
      buftohex((uint8_t*)&ctx.addr,&line[0],8);
      line[16] = 0;
      display_text(LCD_WIDTH/2-8*5,LCD_HEIGHT/2-8/2,&testfont,line);
    }
  } else {
    display_text(LCD_WIDTH-2*5,LCD_HEIGHT-8,&testfont,"R");
  }
}

void onewire_ontimer(void* param) {
  uint16_t val = (uint16_t)param;
  onewire_update_display(val == 0);
  onewire_reset();
  onewire_select(NULL);
  ds18b20_start_conversion();
  val++;
  if (val >= 20) val = 0;
  enqueue_event_rel(onewire_listing_interval,&onewire_ontimer,(void*)val);
}

/*
void onewire_update_display()
{
  display_clear();
  char msg[8];
  display_rect(0,0,11,8,COLOR_WHITE);
  snprintl(&msg[0],4,servo_pos);
  msg[4] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2-10-4,&testfont,msg);
  snprintl(&msg[0],4,servo_step);
  msg[4] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2-4,&testfont,msg);
  int32_t deg = ((int32_t)servo_pos)*servo_total_angle/255;
  snprintl(&msg[0],4,deg);
  msg[4] = 'd';
  msg[5] = 'e';
  msg[6] = 'g';
  msg[7] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2+10-4,&testfont,msg);
}
*/

void onewire_start() {
  onewire_pos = 0;
  onewire_ontimer((void*)0);
}

void onewire_stop() {
  dequeue_events(&onewire_ontimer);
  onewire_port_DDR &= ~(1<<onewire_pin);
  onewire_port_PORT |= 1<<onewire_pin;
}

void onewire_keypressed(uint8_t keys, uint8_t old_keys) {
  // TODO: let the user select a device and show its details.
  if ((keys & KEY_LEFT) && (keys & KEY_RIGHT)) {
    application_stop();
  }
  keys = keys & ~old_keys;
  if (keys & KEY_LEFT) {
    application_stop();
  } else if (keys & KEY_UP) {
    if (onewire_pos > 0) {
      onewire_pos--;
      onewire_update_display(false);
    }
  } else if (keys & KEY_DOWN) {
    onewire_pos++;
    onewire_update_display(false);
  } else if (keys & KEY_RIGHT) {
  }
}


#endif
