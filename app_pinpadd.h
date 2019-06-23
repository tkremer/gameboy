#ifdef APP_ENTRY
  APP_ENTRY("Pinpad-Test",pinpadd_start,pinpadd_stop,pinpadd_keypressed)
#else

//#define pinpadd_adc_div ADC_DIV_32
#define pinpadd_adc_div ADC_DIV_128

int32_t pinpadd_interval = sec2ticks(1.0/100,TIMER_DIV);
uint8_t pinpadd_channel = 4;
#define PINPAD_PIN pinpadd_channel
#include "pinpad.h"

char pinpadd_text[10];
int pinpadd_i;
uint32_t pinpadd_last_event;

void pinpadd_repaint() {
  //display_rect(0,0,11,8,COLOR_WHITE);
  char chan[] = { '0'+pinpadd_channel,0 };
  display_text(0,0,&testfont,chan);

  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2-10-4,&testfont,pinpadd_text);
}

void EVENT_pinpad_keypressed(char c) {
  pinpadd_last_event = get_time();
  pinpadd_text[pinpadd_i] = (c == 0 ? 'X' : c);
  pinpadd_i++;
  if (pinpadd_i >= 10) {
    pinpadd_i = 0;
  }
  pinpadd_repaint();
}

void pinpadd_ontimer(void* param) {
  int16_t val = adcw_state.values[pinpadd_channel];

  char msg[8];
  snprintl(&msg[0],4,val);
  msg[4] = 0;
  display_text(0,8,&testfont,msg);

  snprintl(&msg[0],4,pinpad_ctx.minval);
  msg[4] = 0;
  display_text(0,16,&testfont,msg);


  pinpad_on_adc_read(val);

  int32_t interv = pinpadd_interval;

  enqueue_event_rel(interv,&pinpadd_ontimer,NULL);
}

void pinpadd_start() {
  display_clear();
  pinpadd_text[0] = 0;
  pinpadd_i = 0;
  pinpad_init();
  adc_watch_start();

  pinpadd_repaint();

  pinpadd_ontimer(NULL);
  //enqueue_event_rel(pinpadd_interval,&pinpadd_ontimer,NULL);
}

void pinpadd_stop() {
  pinpad_stop();
  adc_watch_stop();
  dequeue_events(&pinpadd_ontimer);
}

void pinpadd_keypressed(uint8_t keys, uint8_t old_keys) {
  keys = keys & ~old_keys;
  if (keys & KEY_LEFT) {
    application_stop();
  } else if (keys & KEY_UP) {
    if (pinpadd_interval > 1)
      pinpadd_interval /= 2;
  } else if (keys & KEY_DOWN) {
    if (pinpadd_interval < (1<<14))
      pinpadd_interval *= 2;
  } else if (keys & KEY_RIGHT) {
    pinpad_stop();
    pinpadd_channel++;
    if (pinpadd_channel == 6) {
      pinpadd_channel = 0;
    }
    pinpad_init();
    pinpadd_repaint();
  }
}


#endif
