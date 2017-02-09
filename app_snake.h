#ifdef APP_ENTRY
  APP_ENTRY("Snake",snake_start,snake_stop,snake_keypressed)
#else

#define SNAKE_MAX_TAIL 24
struct {
  uint8_t x, y, dir, level, cherry_x, cherry_y;
  // tail saves the 2-bit directions in a dense array:
  uint8_t tail[(SNAKE_MAX_TAIL+3)/4];
} snake_ctx;

static inline uint8_t snake_length() {
  return (snake_ctx.level*2+2 > SNAKE_MAX_TAIL ?
                                SNAKE_MAX_TAIL : snake_ctx.level*2+2);
}

static inline int32_t snake_interval() {
  return sec2ticks(1.0,TIMER_DIV)/(3+snake_ctx.level);
  //return sec2ticks(1.0/4,TIMER_DIV)/snake_ctx.level;
  // we start at level 1, so we may divide...
}

static inline bool snake_dead() {
  return snake_ctx.dir == 0xff;
}

static inline bool snake_death_locked() {
  return snake_ctx.level == 2;
}

void snake_death_unlock(void* param) {
  snake_ctx.level = 1;
}

void snake_die() {
  //application_stop();
  snake_ctx.dir = 0xff;
  char lvl[] = "Level: 000";
  uint8_t l = snake_ctx.level;
  snprintl(&lvl[7],3,l);
/*
  num[3] = 0;
  num[2] = l%10+'0'; l /= 10;
  num[1] = l%10+'0'; l /= 10;
  num[0] = l%10+'0';
*/
  display_text_P(12,16,&testfont,PSTR("Game over"));
  display_text(10,24,&testfont,lvl);

  snake_ctx.level = 2;
  enqueue_event_rel(sec2ticks(1.0/2,TIMER_DIV),&snake_death_unlock,NULL);
}

void snake_newcherry() {
  uint8_t new_x = prng_read_byte(), new_y = prng_read_byte();
  new_x %= LCD_WIDTH;
  new_y %= LCD_HEIGHT;
  snake_ctx.cherry_x = new_x;
  snake_ctx.cherry_y = new_y;
  // FIXME: need to make sure, cherry is not inside snake...
  display_putpixel(new_x,new_y,COLOR_BLACK);
}

void snake_ontimer(void* param) {
  for (int i = (snake_length()+3)/4-1; i > 0; i--) {
    snake_ctx.tail[i] = (snake_ctx.tail[i]<<2) | (snake_ctx.tail[i-1] >> 6);
  }
  uint8_t dir = snake_ctx.dir;
  snake_ctx.tail[0] = (snake_ctx.tail[0]<<2) | dir;
  switch (dir) {
    case 0: snake_ctx.x++; break;
    case 1: snake_ctx.y++; break;
    case 2: snake_ctx.x--; break;
    case 3: snake_ctx.y--; break;
  }
  uint8_t x = snake_ctx.x, y = snake_ctx.y;
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
    snake_die();
    return;
  } else {
    if (x == snake_ctx.cherry_x && y == snake_ctx.cherry_y) {
      if (snake_ctx.level != 0xff)
        snake_ctx.level++;
        // here we actually grow the snake... This is a minor bug, as it may
        // collide the head with the new part of tail. However, we made sure,
        // the new part would go left-right in the initialization of tail.
      snake_newcherry();
    }
    uint8_t x2 = x, y2 = y;
    bool dead = false;
    for (int i = 0; i < snake_length(); i++) {
      uint8_t dir2 = (snake_ctx.tail[i/4]>>((i%4)*2)) & 3;
      switch (dir2) {
        case 0: x2--; break;
        case 1: y2--; break;
        case 2: x2++; break;
        case 3: y2++; break;
      }
      if (i == snake_length()-1) {
        break;
      }
      if (x == x2 && y == y2) {
        dead = true;
        break;
      }
    }
    if (dead) {
      snake_die();
      return;
    } else {
      display_putpixel(x2,y2,COLOR_WHITE);
      display_putpixel(x,y,COLOR_BLACK);
    }
  }
  enqueue_event_rel(snake_interval(),&snake_ontimer,NULL);
}

void snake_start() {
  snake_ctx.level = 1;
  snake_ctx.dir = 0;
  snake_ctx.x = LCD_WIDTH/2;
  snake_ctx.y = LCD_HEIGHT/2;
  display_clear();
  snake_newcherry();
  for (int i = 0; i < (SNAKE_MAX_TAIL+3)/4; i++) {
    snake_ctx.tail[i] = 0x88; // left-right-left-right.
  }
  enqueue_event_rel(snake_interval(),&snake_ontimer,NULL);
}

void snake_stop() {
  dequeue_events(&snake_ontimer);
}

void snake_keypressed(uint8_t keys, uint8_t old_keys) {
  if ((keys & (KEY_LEFT | KEY_RIGHT)) == (KEY_LEFT | KEY_RIGHT)
      || (keys & (KEY_UP | KEY_DOWN)) == (KEY_UP | KEY_DOWN)) {
    // escape sequence to end game.
    application_stop();
  }
  keys = keys & ~old_keys;
  if (snake_dead()) {
    // avoid unintended actions due to post-game-over-movement attempts.
    if (snake_death_locked())
      return;
    if (keys & KEY_LEFT) {
      application_stop();
    } else if (keys & (KEY_RIGHT)) {
      snake_start();
    }
  } else {
    if (keys & KEY_LEFT) {
      snake_ctx.dir = 2;
    } else if (keys & KEY_UP) {
      snake_ctx.dir = 3;
    } else if (keys & KEY_DOWN) {
      snake_ctx.dir = 1;
    } else if (keys & KEY_RIGHT) {
      snake_ctx.dir = 0;
    }
  }
}

#endif
