
struct menu_t;

typedef struct {
  const char *text;
  uint16_t function;
} menuentry_t;

typedef struct menu_t {
  const struct menu_t* parent;
  uint8_t count;
  uint16_t first_ix;
  const menuentry_t* entries;
} menu_t;

typedef struct {
  const menu_t* menu;
  const font_t* font;
  uint8_t pos;
} menu_ctx_t;

void menu_draw(menu_ctx_t* ctx) {
  uint8_t h = pgm_read_byte(&ctx->font->height);
  int8_t lines = LCD_HEIGHT/h;
  int8_t firstline = ctx->pos-lines/2;
  uint8_t count = pgm_read_byte(&ctx->menu->count);
  //if (lines > ctx->menu->count) {
  if (lines > count) {
    firstline = 0;
    //lines = ctx->menu->count;
    lines = count;
  //} else if (firstline+lines > ctx->menu->count)
  //  firstline = (int16_t)ctx->menu->count-lines;
  } else if (firstline+lines > count)
    firstline = (int16_t)count-lines;
  else if (firstline < 0) firstline = 0;

  const menuentry_t* entries = (const menuentry_t*)pgm_read_ptr(&ctx->menu->entries);

  display_clear();
  for (int i = 0; i < lines; i++) {
    int l = i+firstline;
    //const char* text = (const char*)pgm_read_ptr(&ctx->menu->entries[l].text);
    const char* text = (const char*)pgm_read_ptr(&entries[l].text);
    display_text_P(0,i*h,ctx->font,text);
    if (l == ctx->pos) {
      display_rect(0,i*h,LCD_WIDTH,h,COLOR_INVERT);
    }
  }
}

void menu_open(menu_ctx_t* ctx, const menu_t* menu, const font_t* font) {
  ctx->menu = menu;
  ctx->font = font;
  ctx->pos = 0;
  menu_draw(ctx);
}

// returns selected value or 0 for still-in-menu or 0xffff for try-to-get-out.
uint16_t menu_keypressed(menu_ctx_t* ctx, uint8_t key) {
  // 6,7,0,1 -> up,right,left,down
  switch (key) {
    // I use u,l,r,d for now.
    case 1<<6:
      if (ctx->pos > 0) ctx->pos--;
      break;
    case 1<<0:
      {
        const menu_t* parent = (const menu_t*)pgm_read_ptr(&ctx->menu->parent);
        if (parent) {
          ctx->menu = parent;
//        if (ctx->menu->parent) {
//          ctx->menu = ctx->menu->parent;
          ctx->pos = 0;
        } else {
          return 0xffff;
        }
      }
      break;
    case 1<<7:
      {
        const menuentry_t* entries = (const menuentry_t*)pgm_read_ptr(&ctx->menu->entries);
        uint16_t f = pgm_read_word(&entries[ctx->pos].function);
        //uint16_t f = pgm_read_word(&ctx->menu->entries[ctx->pos].function);
        if (f & 0x8000) {
          menu_t* submenu = (menu_t*)(f & 0x7fff);
          ctx->menu = submenu;
          ctx->pos = 0;
        } else if (f == 0) {
          // auto-counting. 0 is not a valid function anyway.
          return ctx->pos + pgm_read_word(&ctx->menu->first_ix);
        } else {
          return f;
        }
      }
      break;
    case 1<<1:
      if (ctx->pos < pgm_read_byte(&ctx->menu->count)-1) ctx->pos++;
      break;
    default:
      return 0;
  }
  menu_draw(ctx);
  return 0;
}

