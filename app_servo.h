#ifdef APP_ENTRY
  APP_ENTRY("Servo",servo_start,servo_stop,servo_keypressed)
#else

// data sheet:
//#define servo_pulse_low msec2ticks(1,TIMER_DIV)
//#define servo_pulse_high msec2ticks(2,TIMER_DIV)

// To test actual limits:
//#define servo_pulse_low msec2ticks(0.1,TIMER_DIV)
//#define servo_pulse_high msec2ticks(12.85,TIMER_DIV)

// tested results:
// correct range: 0.6 - 2.35 0.6..2.25 = 180°
//#define servo_pulse_low msec2ticks(0.6,TIMER_DIV)
//#define servo_pulse_high msec2ticks(2.35,TIMER_DIV)
//#define servo_total_angle (180L*(235-60)/(225-60))

// new servos (tested): 450pwm-2500pwm; 180deg == 2435-450 +-8
#define servo_pulse_low usec2ticks(450,TIMER_DIV)
#define servo_pulse_high usec2ticks(2500,TIMER_DIV)
#define servo_total_angle (180L*(2500-450)/(2435-450))

#define servo_pwm_period msec2ticks(20,TIMER_DIV)
#define servo_pin 5


uint8_t servo_pos = 128;
uint8_t servo_step = 1;
int32_t servo_pulse = (servo_pulse_low+servo_pulse_high)/2;


void servo_ontimer(void* param) {
  uint16_t val = (uint16_t)param;
  int32_t interv = 0;
  if (val == 0) {
    PORTC |= 1<<servo_pin;
    interv = servo_pulse;
    val = 1;
  } else {
    PORTC &= ~(1<<servo_pin);
    interv = servo_pwm_period - servo_pulse;
    val = 0;
  }
  enqueue_event_rel(interv,&servo_ontimer,(void*)val);
}

void servo_update_display()
{
  display_clear();
  char msg[8];
  display_rect(0,0,11,8,COLOR_WHITE);
  // hw_pos (0-255)
  snprintl(&msg[0],4,servo_pos);
  msg[4] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2-15-4,&testfont,msg);
  // steps per keypress
  snprintl(&msg[0],4,servo_step);
  msg[4] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2-5-4,&testfont,msg);
  // degrees rotation
  int32_t deg = ((int32_t)servo_pos)*servo_total_angle/255;
  snprintl(&msg[0],4,deg);
  msg[4] = 'd';
  msg[5] = 'e';
  msg[6] = 'g';
  msg[7] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2+5-4,&testfont,msg);
  // µs pwm
  int32_t pwm = servo_pulse*256/usec2ticks(256,TIMER_DIV);
  snprintl(&msg[0],4,pwm);
  msg[4] = 'p';
  msg[5] = 'w';
  msg[6] = 'm';
  msg[7] = 0;
  display_text(LCD_WIDTH/2-10,LCD_HEIGHT/2+15-4,&testfont,msg);
}

void servo_update_pulse()
{
  servo_pulse = servo_pulse_low + (((int32_t)servo_pos)*(servo_pulse_high-servo_pulse_low))/255; // going from -1 to 2 instead of 0 to 1. // /255;
  //servo_pulse = servo_pulse_low + (((int32_t)servo_pos - 64)*2*(servo_pulse_high-servo_pulse_low))/255; // going from -1 to 2 instead of 0 to 1. // /255;
}

void servo_start() {
  PORTC &= ~(1<<servo_pin);
  DDRC |= 1<<servo_pin;
  servo_update_display();
  servo_ontimer(NULL);
}

void servo_stop() {
  dequeue_events(&servo_ontimer);
  DDRC &= ~(1<<servo_pin);
  PORTC |= 1<<servo_pin;
}

void servo_keypressed(uint8_t keys, uint8_t old_keys) {
  if ((keys & KEY_LEFT) && (keys & KEY_RIGHT)) {
    application_stop();
  }
  keys = keys & ~old_keys;
  if (keys & KEY_LEFT) {
    if (servo_step == 1)
      application_stop();
    else {
      servo_step--;
    }
    servo_update_display();
  } else if (keys & KEY_UP) {
    if (servo_pos < 255-servo_step)
      servo_pos += servo_step;
    else
      servo_pos = 255;
    servo_update_pulse();
    servo_update_display();
  } else if (keys & KEY_DOWN) {
    if (servo_pos >= servo_step)
      servo_pos -= servo_step;
    else
      servo_pos = 0;
    servo_update_pulse();
    servo_update_display();
  } else if (keys & KEY_RIGHT) {
    servo_step++;
    servo_update_display();
  }
}


#endif
