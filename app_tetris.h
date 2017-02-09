#ifdef APP_ENTRY
  APP_ENTRY("Tetris",tetris_start,tetris_stop,tetris_keypressed)
#else

#define TETRIS_WIDTH 10
#define TETRIS_BYTEWIDTH ((TETRIS_WIDTH+7)/8)
#define TETRIS_HEIGHT ((LCD_HEIGHT-2)/2)
#define TETRIS_PIECES_COUNT 7
struct {
  int8_t x, y;
  uint8_t rot, piece, level, lastkey;
  uint16_t score;
  uint8_t filled[TETRIS_HEIGHT*TETRIS_BYTEWIDTH];
} tetris_ctx;

// 4x4 bitmaps = 2 bytes/piece/orientation.

// 7 pieces:
// x x   x  x x  x  xx
// x x   x xx xx xx xx
// x xx xx x   x x
// x
const uint16_t tetris_pieces_data[] PROGMEM = {
  0x2222, 0x00f0,                  // |
  0x0622, 0x0740, 0x0446, 0x0170,  // L
  0x0644, 0x0470, 0x0226, 0x0710,  // _|
  0x0264, 0x0c60,                  // '-,
  0x0462, 0x0360,                  // ,-'
  0x0262, 0x0270, 0x0232, 0x0072,  // |-
  0x0660                           // []
};

const struct {
  uint8_t orientations;
  uint8_t index;
} tetris_pieces[TETRIS_PIECES_COUNT] PROGMEM = {
  { 2,0 },
  { 4,2 },
  { 4,6 },
  { 2,10 },
  { 2,12 },
  { 4,14 },
  { 1,18 }
};

static inline bool tetris_get_px(uint8_t x, uint8_t y) {
  return tetris_ctx.filled[y*TETRIS_BYTEWIDTH + (x>>3)] & (1<<(x&7));
}

static inline uint8_t tetris_get_4px(int8_t x, int8_t y) {
  if (x <= -4 || x >= TETRIS_WIDTH) return 0xf;
  if (y >= TETRIS_HEIGHT) return 0xf;
  if (y < 0) {
    return x < 0 ? 0xf >> (4+x) :
           x > TETRIS_HEIGHT-4 ? (0xf << (TETRIS_HEIGHT-x))&0xf
                 : 0;
  }
  if ((x&7) <= 4) {
    return (
            (tetris_ctx.filled[y*TETRIS_BYTEWIDTH + (x>>3)] |
                    (((x>>3)+1 != TETRIS_BYTEWIDTH || TETRIS_WIDTH%8 == 0) ?
                         0 
                       : 0x0f << (TETRIS_WIDTH%8))) >> (x&7)
           ) & 0xf;
  } else {
      uint8_t left = (x >= 0 ? tetris_ctx.filled[y*TETRIS_BYTEWIDTH + (x>>3)]
                             : 0xff) >> (x&7);
      uint8_t right = (
                ((x>>3)+1 < TETRIS_BYTEWIDTH) ?
                    tetris_ctx.filled[y*TETRIS_BYTEWIDTH + (x>>3)+1] | 
                    (((x>>3)+2 != TETRIS_BYTEWIDTH || TETRIS_WIDTH%8 == 0) ?
                         0 
                       : 0x0f << (TETRIS_WIDTH%8))
                : 0xf
              ) << (8-(x&7));
      return (left | right) & 0xf;
  }
/*
  if (x < 0) return ((0xf >> (x&7)) |
          (filled[y*TETRIS_BYTEWIDTH + (x>>3)+1] << (8-x&7))) & 0xf;
    if (x < TETRIS_WIDTH-4)
      return ((filled[y*TETRIS_BYTEWIDTH + (x>>3)] >> (x&7))
            | (filled[y*TETRIS_BYTEWIDTH + (x>>3)+1] << (8-x&7))) & 0xf;
    return ((filled[y*TETRIS_BYTEWIDTH + (x>>3)] >> (x&7))
          | (0xf << (8-x&7))) & 0xf;
  }
*/
}

static inline uint16_t tetris_get_4x4px(int8_t x, int8_t y) {
  return tetris_get_4px(x,y) | (tetris_get_4px(x,y+1) << 4) |
    ((uint16_t)(tetris_get_4px(x,y+2) | (tetris_get_4px(x,y+3) << 4)) << 8);
}

// out-of-bounds write is accepted iff it's all zeros.
static inline bool tetris_put_4px(int8_t x, int8_t y, uint8_t bitmap) {
  if (y < 0 || y >= TETRIS_HEIGHT)
    return bitmap == 0;
  if (x <= -4 || x >= TETRIS_WIDTH) return false;

  bitmap &= 0xf;
  uint8_t bit_left = bitmap << (x&7);
  uint8_t bit_right = bitmap >> (8-(x&7));
  if (x >= 0)
    tetris_ctx.filled[y*TETRIS_BYTEWIDTH + (x>>3)] |= bit_left;
  else if (bit_left != 0) return false;
  if ((x&7) > 4) {
    if ((x>>3)+1 < TETRIS_BYTEWIDTH)
      tetris_ctx.filled[y*TETRIS_BYTEWIDTH + (x>>3)+1] |= bit_right;
    else if (bit_right != 0) return false;
    // actually out-of-bounds fail is only relevant for y < 0, anything else is
    // already handled by collision detection.
  }
  return true;
}

static inline bool tetris_put_4x4px(int8_t x, int8_t y, uint16_t bitmap) {
  return tetris_put_4px(x,y,   bitmap     &0xf) &&
         tetris_put_4px(x,y+1,(bitmap>> 4)&0xf) &&
         tetris_put_4px(x,y+2,(bitmap>> 8)&0xf) &&
         tetris_put_4px(x,y+3,(bitmap>>12)&0xf);
}

static inline uint16_t tetris_get_piece_data(uint8_t rot, uint8_t piece) {
  return pgm_read_word(&tetris_pieces_data[pgm_read_byte(&tetris_pieces[piece].index)+rot]);
}

static inline int32_t tetris_interval() {
  return sec2ticks(1.0,TIMER_DIV)/(3+tetris_ctx.level);
  // we start at level 1, so we may divide...
}

static inline bool tetris_dead() {
  return tetris_ctx.piece == 0xff;
}

static inline bool tetris_death_locked() {
  return tetris_ctx.level == 2;
}

void tetris_death_unlock(void* param) {
  tetris_ctx.level = 1;
}

void tetris_die() {
  //application_stop();
  tetris_ctx.piece = 0xff;
  /*
  char num[4];
  uint8_t l = tetris_ctx.level;
  num[3] = 0;
  num[2] = l%10+'0'; l /= 10;
  num[1] = l%10+'0'; l /= 10;
  num[0] = l%10+'0';
  */
  display_text_P(10,LCD_HEIGHT-8,&testfont,PSTR("Game over"));
  //display_text(10,24,&testfont,num);

  tetris_ctx.level = 2;
  enqueue_event_rel(sec2ticks(1.0/2,TIMER_DIV),&tetris_death_unlock,NULL);
}

static inline uint8_t tetris_get_max_rot(uint8_t piece) {
  return pgm_read_byte(&tetris_pieces[piece].orientations);
}

static bool tetris_test_collision(int8_t x, int8_t y, uint8_t rot, uint8_t piece) {
  uint16_t piece_data = tetris_get_piece_data(rot,piece);
  uint16_t fill_data = tetris_get_4x4px(x,y);
  return (piece_data & fill_data) ? 1 : 0;
}

// FIXME: the long one still glitches to the right (x == 9)

// TODO: this could be a better graphic. (well, if we had grey levels...)
const uint8_t tetris_sprite_data[] PROGMEM = { 0x03,0x03, 0xfc,0xfc };

void tetris_drawstate() {
  sprite_t sprite = {
    .w = 2,
    .h = 2,
    .black = pgm_addr(&tetris_sprite_data[0]),
    .white = pgm_addr(&tetris_sprite_data[2])
  };
  uint8_t px = 8, py = 1;
  display_rect(px-1,py-1,TETRIS_WIDTH*2+2,TETRIS_HEIGHT*2+2,COLOR_BLACK);
  display_rect(px,py,TETRIS_WIDTH*2,TETRIS_HEIGHT*2,COLOR_WHITE);
  for (int y = 0; y < TETRIS_HEIGHT; y++)
    for (int x = 0; x < TETRIS_WIDTH; x++)
      if (tetris_get_px(x,y))
        display_sprite(px+x*2,py+y*2,&sprite);
     // display_putpixel(px+x,py+y,COLOR_BLACK);

  int x = px+tetris_ctx.x*2, y = py+tetris_ctx.y*2;
  uint16_t piece_data = tetris_get_piece_data(tetris_ctx.rot,tetris_ctx.piece);
  for (int dy = 0; dy < 4; dy++)
    for (int dx = 0; dx < 4; dx++)
      if (piece_data & (1 << (dy*4+dx)))
        display_sprite(x+dx*2,y+dy*2,&sprite);
        //display_putpixel(x+dx,y+dy, COLOR_BLACK);
  char msg[] = "Score:\n 0000\nLevel:\n  000";
  snprintl(&msg[8],4,tetris_ctx.score);
  snprintl(&msg[22],3,tetris_ctx.level);
  display_text(px+TETRIS_WIDTH*2+3,py,&testfont,msg);
}

bool tetris_newpiece() {
  //static uint8_t nextpiece = 0;
  tetris_ctx.rot = 0;
  tetris_ctx.x = TETRIS_WIDTH/2-2;
  tetris_ctx.y = 0;
  tetris_ctx.piece = prng_read_byte() % TETRIS_PIECES_COUNT;
  //tetris_ctx.piece = nextpiece++;
  //if (nextpiece == TETRIS_PIECES_COUNT)
  //  nextpiece = 0;
  tetris_drawstate();
  if (tetris_test_collision(tetris_ctx.x,tetris_ctx.y,tetris_ctx.rot,tetris_ctx.piece)) {
    tetris_die();
    return 0;
  }
  return 1;
}

// checks if a line should be deleted, deletes it and returns 1 if deleted.
uint8_t tetris_check_deletion(int8_t y) {
  uint8_t last_mask = (0xff >> (8 - TETRIS_WIDTH%8));
  for (int i = 0; i < TETRIS_BYTEWIDTH; i++) {
    uint8_t val = tetris_ctx.filled[y*TETRIS_BYTEWIDTH + i];
    if (val != 0xff) {
      if (!(i == TETRIS_BYTEWIDTH-1 && (val & last_mask) == last_mask)) {
        return 0; // not full.
      }
    }
  }
  // line full. delete it.
  for (int y2 = y; y2 > 0; y2--) {
    for (int i = 0; i < TETRIS_BYTEWIDTH; i++) {
      tetris_ctx.filled[y2*TETRIS_BYTEWIDTH + i] = tetris_ctx.filled[(y2-1)*TETRIS_BYTEWIDTH + i];
    }
  }
  for (int i = 0; i < TETRIS_BYTEWIDTH; i++) {
    tetris_ctx.filled[i] = 0;
  }
  return 1;
}

uint8_t tetris_embed_piece(int8_t x, int8_t y, uint8_t rot, uint8_t piece) {
  uint16_t piece_data = tetris_get_piece_data(rot,piece);
  uint16_t fill_data = tetris_get_4x4px(x,y);
  if (!tetris_put_4x4px(x,y,piece_data)) return 0xff;

  uint8_t res = 0;

  fill_data |= piece_data;
  if (fill_data & 0x000f) res += tetris_check_deletion(y);
  if (fill_data & 0x00f0) res += tetris_check_deletion(y+1);
  if (fill_data & 0x0f00) res += tetris_check_deletion(y+2);
  if (fill_data & 0xf000) res += tetris_check_deletion(y+3);
  return res;
}

//#define debug(x) display_text_P(0,0,&testfont,PSTR(x));

void tetris_ontimer(void* param) {
  int8_t x = tetris_ctx.x, y = tetris_ctx.y;
  uint8_t rot = tetris_ctx.rot, piece = tetris_ctx.piece;
  if (tetris_test_collision(x,y+1,rot,piece)) {
    // put down that piece and take a new one.
    uint8_t lines = tetris_embed_piece(x,y,rot,piece);
    if (lines == 0xff) {
      tetris_die();
      return;
    } else {
      uint8_t score = 1<<lines;
      if (tetris_ctx.score < 9999-score) tetris_ctx.score += score;
      else tetris_ctx.score = 9999;
      //if (lines != 0 && tetris_ctx.level < 0xff) tetris_ctx.level++;
      tetris_ctx.level = tetris_ctx.score/10 + 1;
    }
    if (!tetris_newpiece()) {
      return;
    }
  } else {
    tetris_ctx.y = y+1;
    tetris_drawstate();
  }
  enqueue_event_rel(tetris_interval(),&tetris_ontimer,NULL);
}

void tetris_clear() {
  for (int i = 0; i < sizeof(tetris_ctx.filled); i++)
    tetris_ctx.filled[i] = 0;
}

void tetris_start() {
  tetris_ctx.level = 1;
  tetris_ctx.score = 0;
  display_clear();
  tetris_clear();
  if (tetris_newpiece())
    enqueue_event_rel(tetris_interval(),&tetris_ontimer,NULL);
}

void tetris_dokey(uint8_t keys) {
  if (tetris_dead()) {
    // avoid unintended actions due to post-game-over-movement attempts.
    if (tetris_death_locked())
      return;
    if (keys & KEY_LEFT) {
      application_stop();
    } else if (keys & (KEY_RIGHT)) {
      tetris_start();
    }
  } else {
    int8_t x = tetris_ctx.x, y = tetris_ctx.y;
    uint8_t rot = tetris_ctx.rot, piece = tetris_ctx.piece;
    bool repaint = false;
    if (keys & KEY_LEFT) {
      x--;
      if (!tetris_test_collision(x,y,rot,piece)) {
        tetris_ctx.x = x;
        repaint = true;
      }
    } else if (keys & KEY_UP) {
      // turn piece.
      rot++;
      if (rot == tetris_get_max_rot(piece)) rot = 0;
      if (!tetris_test_collision(x,y,rot,piece)) {
        tetris_ctx.rot = rot;
        repaint = true;
      }
    } else if (keys & KEY_DOWN) {
      // let it fall down one unit.
      y++;
      if (!tetris_test_collision(x,y,rot,piece)) {
        tetris_ctx.y = y;
        repaint = true;
      }
    } else if (keys & KEY_RIGHT) {
      x++;
      if (!tetris_test_collision(x,y,rot,piece)) {
        tetris_ctx.x = x;
        repaint = true;
      }
    }
    if (repaint) {
      tetris_drawstate();
    }
  }
}

void tetris_on_key_repeat(void* param) {
  enqueue_event_rel(msec2ticks(125,TIMER_DIV),&tetris_on_key_repeat,NULL);
  tetris_dokey(tetris_ctx.lastkey);
}

void tetris_keypressed(uint8_t keys, uint8_t old_keys) {
  if ((keys & (KEY_LEFT | KEY_RIGHT)) == (KEY_LEFT | KEY_RIGHT)
      || (keys & (KEY_UP | KEY_DOWN)) == (KEY_UP | KEY_DOWN)) {
    // escape sequence to end game.
    application_stop();
    return;
  }
  keys = keys & ~old_keys;
  dequeue_events(&tetris_on_key_repeat);
  if (keys != 0) {
    tetris_ctx.lastkey = keys;
    enqueue_event_rel(msec2ticks(500,TIMER_DIV),&tetris_on_key_repeat,NULL);
    tetris_dokey(keys);
  }
}

void tetris_stop() {
  dequeue_events(&tetris_ontimer);
  dequeue_events(&tetris_on_key_repeat);
}

#endif
