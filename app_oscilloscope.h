#ifdef APP_ENTRY
  APP_ENTRY("Oszilloskop",osci_start,osci_stop,osci_keypressed)
#else

#define osci_adc_div ADC_DIV_32
//#define osci_adc_div ADC_DIV_128

int32_t osci_interval = sec2ticks(1.0/100,TIMER_DIV);
int32_t osci_wait_interval = sec2ticks(1.0/4,TIMER_DIV);
uint8_t osci_channel1 = 6;
uint8_t osci_channel2 = 7;
uint8_t osci_x = 0;

void osci_ontimer(void* param) {
  int16_t val1 = adcw_state.values[osci_channel1];
  int16_t val2 = adcw_state.values[osci_channel2];

  int32_t interv = osci_interval;

  // trigger on channel2 > 0 flank
  if (osci_x == 0xfe && val2 < 512) osci_x = 0xff;
  else if (osci_x == 0xff && val2 >= 512) osci_x = 0;
  
  if (osci_x == 0) display_clear();


  if (osci_x < 11 || osci_x >= 0xfe) {
    char chan[] = { '0'+osci_channel1,'0'+osci_channel2,0 };
    display_rect(0,0,11,8,COLOR_WHITE);
    display_text(0,0,&testfont,chan);
  }

  if (osci_x < 0xfe) {

    int y1 = LCD_HEIGHT-1-(int32_t)val1*(LCD_HEIGHT-1)/1023;
    int y2 = LCD_HEIGHT-1-(int32_t)val2*(LCD_HEIGHT-1)/1023;

//    display_rect(osci_x,0,1,LCD_HEIGHT,COLOR_WHITE);
    display_putpixel(osci_x,y1,COLOR_BLACK);
    display_putpixel(osci_x,y2,COLOR_BLACK);
//    display_rect(osci_x,y1,1,1,COLOR_BLACK);
//    display_rect(osci_x,y2,1,1,COLOR_BLACK);
    //display_putpixel(osci_x,y,COLOR_BLACK);
    osci_x++;
    if (osci_x >= LCD_WIDTH) {
      osci_x = 0xfe;
      interv = osci_wait_interval;
    }
  }

  enqueue_event_rel(interv,&osci_ontimer,NULL);
}

void osci_start() {
  display_clear();
  uint8_t mask = (1 << osci_channel1) | (1 << osci_channel2);
  adc_conf(true,mask,osci_adc_div);
   // pins C3, ADC6 and ADC7
  adc_watch_set_mask(mask);
  adc_watch_start();

  osci_x = 0;
  osci_ontimer(NULL);
  //enqueue_event_rel(osci_interval,&osci_ontimer,NULL);
}

void osci_stop() {
  adc_watch_stop();
  adc_conf(false,0,ADC_DIV_128); // pins C3, ADC6 and ADC7
  dequeue_events(&osci_ontimer);
}

void osci_keypressed(uint8_t keys, uint8_t old_keys) {
  keys = keys & ~old_keys;
  if (keys & KEY_LEFT) {
    application_stop();
  } else if (keys & KEY_UP) {
    if (osci_interval > 1)
      osci_interval /= 2;
  } else if (keys & KEY_DOWN) {
    if (osci_interval < (1<<14))
      osci_interval *= 2;
  } else if (keys & KEY_RIGHT) {
    osci_channel1++;
    if (osci_channel1 == 8) {
      osci_channel1 = 0;
      osci_channel2++;
      if (osci_channel2 == 8) {
        osci_channel2 = 0;
      }
    }
    uint8_t mask = (1 << osci_channel1) | (1 << osci_channel2);
    // TODO: somehow this is still not working right...
    adc_conf(true,mask,osci_adc_div);
    adc_watch_set_mask(mask);
    // maybe pause/resume? maybe menu?
  }
}


#endif
