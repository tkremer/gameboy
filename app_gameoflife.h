#ifdef APP_ENTRY

  APP_ENTRY("Game of Life",gol_start, gol_stop, gol_keypressed)

#else

int32_t gol_interval = 0;
//uint16_t seed = 0;

// Notice: this is quite a suboptimal implementation. - But it'll do...
void gol_ontimer(void* param) {
  uint8_t lines[2][LCD_WIDTH+2];
  for (int i = 0; i < LCD_WIDTH+2; i++)
    lines[1][i] = 0;
  lines[0][0] = 0;
  lines[0][LCD_WIDTH+1] = 0;

  uint8_t line = 0;
  for (int y = 0; y < LCD_HEIGHT; y++) {
    for (int x = 0; x < LCD_WIDTH; x++) {
      uint8_t oldval = display_getpixel(x,y);
      lines[line][x+1] = oldval;
      int count = lines[1-line][x]+lines[1-line][x+1]+lines[1-line][x+2]
                 +lines[line][x]           +display_getpixel(x+1,y)
                 +display_getpixel(x-1,y+1)+display_getpixel(x,y+1)
                                               +display_getpixel(x+1,y+1);
      // uint8_t newval = count == 2 ? oldval : count == 3 ? 1 : 0;
      //   oldval ? (count >= 2 && count <= 3) : (count == 3);
      uint8_t newval = count == 3 ? 1 : 0;
      if (count != 2) display_putpixel(x,y,newval);
    }
    line = 1-line;
  }
  enqueue_event_rel(gol_interval,&gol_ontimer,NULL);
}

/*
uint8_t prng_mangle(uint32_t *state, uint8_t input) {
  state[0] ^= input ^ (state[1]>>15) ^ (state[5]<<3) ^ state[7] ^ (state[13] << 13) ^ state[15]; // TODO: use something more sane.
  for (int i = 15; i > 0; i--) {
    state[i] = state[i-1];
  }
  return state[15] & 0xff;
}
*/
void gol_fill() { //uint16_t seed) {
/*  uint32_t state[16];
  for (int i = 0; i < 16; i++) {
    state[i] = seed;
  }
  for (int16_t i = 0; i < 4096; i++) {
    uint8_t byte = pgm_read_byte((void*)i);
    prng_mangle(state,byte+seed);
  }*/
  for (uint16_t i = 0; i < LCD_WIDTH*LCD_HEIGHT/8; i++) {
    display_ctx.image[i] = prng_read_byte(); //prng_mangle(state,0);
  }
  display_invalidate_all();
}

void gol_start() {
  if (gol_interval == 0) {
    gol_interval = sec2ticks(1.0/25,TIMER_DIV);
//    seed = 0;
  }
  gol_fill();
//  gol_fill(seed);
//  seed++;
  enqueue_event_rel(gol_interval,&gol_ontimer,NULL);
}

void gol_stop() {
  dequeue_events(&gol_ontimer);
}

void gol_keypressed(uint8_t keys, uint8_t old_keys) {
  keys = keys & ~old_keys;
  if (keys & KEY_LEFT) {
    application_stop();
  } else if (keys & KEY_UP) {
    if (gol_interval > 1)
      gol_interval /= 2;
  } else if (keys & KEY_DOWN) {
    if (gol_interval < (1<<14))
      gol_interval *= 2;
  } else if (keys & KEY_RIGHT) {
    // maybe pause/resume? maybe randomness?
    gol_fill();
//    gol_fill(seed);
//    seed++;
  }
}

#endif
