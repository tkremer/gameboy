#ifndef __ADC_WATCH_H__
#define __ADC_WATCH_H__

#include <adc.h>

#define ADCW_STATE_STOPPED 0
#define ADCW_STATE_INIT 1
#define ADCW_STATE_READING 2
#define ADCW_STATE_SWITCHING 3

#define ADCW_READ_COUNT 10

void EVENT_adc_watch(uint8_t channel, int16_t value);

struct {
  uint8_t channel, next_channel, state, mask, count, max_count;
  // prescaler;
  uint16_t val;
  int16_t values[8],min[8],max[8];
} adcw_state;// = {0,0,ADCW_STATE_STOPPED,0,0,0,{0,0,0,0,0,0,0,0}};

void adc_watch_start() {
  uint8_t mask = adcw_state.mask;
  //uint8_t prescaler = adcw_state.prescaler;
  uint8_t c = 0;
  if (mask == 0)
    return;
  while (c < 8 && (mask & (1<<c)) == 0)
    c++;
  adcw_state.channel = c;
  adcw_state.state = ADCW_STATE_INIT;
  //adc_conf(true,mask,prescaler);
  adc_interrupt_enable(true);
  adc_start_continuous(c,ADC_REF_VCC);
}

void adc_watch_stop() {
  adc_stop_continuous();
  adc_interrupt_enable(false);
  adcw_state.state = ADCW_STATE_STOPPED;
}

void adc_watch_init(uint8_t channel_mask) {
  //, uint8_t prescaler) {
  memset(&adcw_state,0,sizeof(adcw_state));
  for (int i = 0; i < 8; i++)
    adcw_state.min[i] = 32767;
  for (int i = 0; i < 8; i++)
    adcw_state.max[i] = -1;
  // -> The default is "alert everything".
  adcw_state.mask = channel_mask;
  adcw_state.max_count = ADCW_READ_COUNT-1;
  //adcw_state.prescaler = prescaler;
  adcw_state.state = ADCW_STATE_STOPPED;
}

void adc_watch_set_mask(uint8_t channel_mask) {
  if (channel_mask == 0) {
    adc_watch_stop();
    adcw_state.mask = 0;
  } else {
    adcw_state.mask = channel_mask;
  }
}

void adc_watch_set_range(uint8_t channel, int16_t min, int16_t max) {
  adcw_state.min[channel] = min;
  adcw_state.max[channel] = max;
}

void adc_watch_set_read_count(uint8_t count) {
  if (count >= 1)
    adcw_state.max_count = count-1;
}

On_ADC_read {
  // do something
  switch(adcw_state.state) {
    case ADCW_STATE_STOPPED:
      break; // result returned ignored after stopping.
    case ADCW_STATE_INIT: {
      // first read on the channel complete, but garbage.
      adcw_state.state = ADCW_STATE_READING;
      adcw_state.count = 0;
      adcw_state.val = 0;
      uint8_t c = adcw_state.channel;
      do {
        c++;
        if (c >= 8) c -= 8;
      } while (!(adcw_state.mask & (1<<c)));
      adcw_state.next_channel = c;
      break;
    }
    case ADCW_STATE_READING:
      adcw_state.val += adc_value();
      adcw_state.count++;
      if (adcw_state.count >= adcw_state.max_count) {
        //ADCW_READ_COUNT-1) {
        adcw_state.state = ADCW_STATE_SWITCHING;
        adc_set_channel(adcw_state.next_channel,ADC_REF_VCC);
        // next read result is still on current channel.
      }
      break;
    case ADCW_STATE_SWITCHING: {
      uint16_t uval = adcw_state.val + adc_value();
      uint8_t count = adcw_state.count+1; //ADCW_READ_COUNT;
      //uint8_t count = adcw_state.count+1;
      uval /= count;
      uint8_t chan = adcw_state.channel;
      int16_t val = (adcw_state.values[chan]+(int16_t)uval)/2;
      adcw_state.values[chan] = val;
      adcw_state.channel = adcw_state.next_channel;
      adcw_state.state = ADCW_STATE_INIT;
      if ((val > adcw_state.max[chan]) || (val < adcw_state.min[chan])) {
        EVENT_adc_watch(chan,val);
      }
      break;
    }
    default:
      // invalid state. Reset.
      //adcw_state.state = ADCW_STATE_INIT;
      adc_watch_stop();
  }
}

#endif
