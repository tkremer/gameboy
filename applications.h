
typedef void (*callback_t)();
typedef void (*keypress_callback_t)(uint8_t keys, uint8_t old_keys);

typedef struct {
  callback_t start,stop;
  keypress_callback_t keypressed;
} application_t;

void application_start(uint16_t app);
void application_stop();
void application_keypressed(uint8_t keys);

void dummy() {};


// menu application

// DONE: find a way to autogenerate this menu.
// FIXED: menus should really be in PROGMEM.
//static const char main_menu_entry0_str[] /*PROGMEM*/ = "Game of Life";
#define APP_ENTRY(name,astart,astop,akeyp) static const char main_menu_entry_str_##astart[] PROGMEM = name;
  #include "app_list.h"
#undef APP_ENTRY

const menuentry_t main_menu_entries[] PROGMEM = {
  //#define APP_ENTRY(name,astart,astop,akeyp) { name, 0 },
  #define APP_ENTRY(name,astart,astop,akeyp) { main_menu_entry_str_##astart, 0 },
    #include "app_list.h"
  #undef APP_ENTRY
//  {main_menu_entry0_str,0},{"Snake",0},{"Baz",0},{"Tetris",0},{"Bar",0},{"Foobar",0},{"Blocks",0},{"Voltmeter",0}
//  {main_menu_entry0_str,1},{"Snake",2},{"Baz",3},{"Tetris",4},{"Bar",5},{"Foobar",6},{"Blocks",7},{"Voltmeter",8}
};

const menu_t main_menu PROGMEM = {
  .parent = 0,
  .count = sizeof(main_menu_entries)/sizeof(main_menu_entries[0]),
  .first_ix = 1,
  .entries = main_menu_entries
};

menu_ctx_t menu_ctx = {
  .menu = &main_menu,
  .font = &testfont,
  .pos = 0
};

void menu_app_start() {
  //menu_open(&menu_ctx,&main_menu,&testfont);
  menu_draw(&menu_ctx);
}

void menu_app_keypressed(uint8_t keys, uint8_t old_keys) {
  uint8_t new_keys = keys & ~old_keys;
/*
  bool do_write = usart_writable_space() >= 5;
  if (do_write)
    usart_writechar(new_keys);
*/
  uint16_t result = menu_keypressed(&menu_ctx,new_keys);
/*  if (do_write) {
    usart_writechar(result & 0xff);
    usart_writechar(result >> 8);
    usart_writechar('\n');
  }*/
  if (result != 0) {
    if (result == 0xffff) {
      // reset, in case we got stuck in a submenu.
      menu_open(&menu_ctx,&main_menu,&testfont);
    } else {
      application_start(result);
    }
  }
}

#include "app_list.h"

#define APP_ENTRY(name,astart,astop,akeyp) ,{ .start = &astart, .stop = &astop, .keypressed = &akeyp }
uint16_t current_application = 0xffff; // 0 = menu.
const application_t applications[] PROGMEM = {
  { .start = &menu_app_start, .stop = &dummy, .keypressed = &menu_app_keypressed }
  // adds a comma: APP_ENTRY(menu_app_start, dummy, menu_app_keypressed)
/*  { .start = &menu_app_start, .stop = &dummy,
    .keypressed = &menu_app_keypressed },*/
  #include "app_list.h"
};
#undef APP_ENTRY

#define APP_CALLBACK(name) (*(typeof(applications[0].name))pgm_read_ptr(&applications[current_application].name))
void application_stop() {
  if (current_application != 0xffff) {
    APP_CALLBACK(stop)();
    //(*applications[current_application].stop)();
    current_application = 0xffff;
  }
}

void application_start(uint16_t app) {
  if (app < sizeof(applications)/sizeof(applications[0])) {
    application_stop();
    current_application = app;
    APP_CALLBACK(start)();
//    (*applications[app].start)();
  }
}

uint8_t old_keys = 0;
void application_keypressed(uint8_t keys) {
  if (current_application != 0xffff)
    APP_CALLBACK(keypressed)(keys,old_keys);
//    (*applications[current_application].keypressed)(keys,old_keys);
  old_keys = keys;
}
#undef APP_CALLBACK

