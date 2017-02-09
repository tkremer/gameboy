#ifdef APP_ENTRY
  APP_ENTRY("Voltmeter",vmeter_start,vmeter_stop,vmeter_keypressed)
#else

int32_t vmeter_interval = sec2ticks(1.0/4,TIMER_DIV);
uint8_t vmeter_channel = 0;
uint8_t vmeter_x = 0;

void vmeter_ontimer(void* param) {
  int16_t val = adcw_state.values[vmeter_channel];

  int y = LCD_HEIGHT-1-(int32_t)val*(LCD_HEIGHT-1)/1023;

  display_rect(vmeter_x,0,1,LCD_HEIGHT,COLOR_WHITE);
  display_rect(vmeter_x,y,1,LCD_HEIGHT-y,COLOR_BLACK);
  //display_putpixel(vmeter_x,y,COLOR_BLACK);

  char chan[] = { '0'+vmeter_channel,0 };
  display_text(0,0,&testfont,chan);

  vmeter_x++;
  if (vmeter_x >= LCD_WIDTH) vmeter_x = 0;

  enqueue_event_rel(vmeter_interval,&vmeter_ontimer,NULL);
}

void vmeter_start() {
  display_clear();
  adc_conf(true,1 << vmeter_channel,ADC_DIV_128); // pins C3, ADC6 and ADC7
  adc_watch_set_mask(1<<vmeter_channel);
  adc_watch_start();

  vmeter_ontimer(NULL);
  //enqueue_event_rel(vmeter_interval,&vmeter_ontimer,NULL);
}

void vmeter_stop() {
  adc_watch_stop();
  adc_conf(false,0,ADC_DIV_128); // pins C3, ADC6 and ADC7
  dequeue_events(&vmeter_ontimer);
}

void vmeter_keypressed(uint8_t keys, uint8_t old_keys) {
  keys = keys & ~old_keys;
  if (keys & KEY_LEFT) {
    application_stop();
  } else if (keys & KEY_UP) {
    if (vmeter_interval > 1)
      vmeter_interval /= 2;
  } else if (keys & KEY_DOWN) {
    if (vmeter_interval < (1<<14))
      vmeter_interval *= 2;
  } else if (keys & KEY_RIGHT) {
    vmeter_channel++;
    if (vmeter_channel == 8) vmeter_channel = 0;
    adc_watch_set_mask(1<<vmeter_channel);
    // maybe pause/resume? maybe menu?
  }
}


#endif
