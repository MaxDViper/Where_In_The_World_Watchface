#include <pebble.h>
#include "config.h"

#define STR_SIZE 20
#define REDRAW_INTERVAL 30
#define TIME_OFFSET_PERSIST 1
#define LAT_PERSIST 2
#define LON_PERSIST 3

#if PBL_DISPLAY_WIDTH > 144
  #define WIDTH 200
  #define HEIGHT 139
  #define MAP_RESOURCE RESOURCE_ID_THREE_WORLDS_200
  #define TIME_FONT FONT_KEY_LECO_60_NUMBERS_AM_PM
  #define DATE_FONT FONT_KEY_GOTHIC_28_BOLD
  #define TEXT_OFFSET 10
  #define ODOT_SIZE 5
  #define IDOT_SIZE 2
  #define CROSS_WIDTH 2
  #define ODOT_WIDTH 2
#else
  #define WIDTH 144
  #define HEIGHT 100
  #define MAP_RESOURCE RESOURCE_ID_THREE_WORLDS_144
  #define TIME_FONT FONT_KEY_LECO_42_NUMBERS
  #define DATE_FONT FONT_KEY_GOTHIC_18_BOLD
  #define TEXT_OFFSET 3
  #define ODOT_SIZE 3
  #define IDOT_SIZE 1
  #define CROSS_WIDTH 1
  #define ODOT_WIDTH 1
#endif

int LOCAL_X = -1;
int LOCAL_Y = -1;

static Window *window;
static TextLayer *time_text_layer;
static TextLayer *date_text_layer;
static TextLayer *bottom_text_layer;
static GBitmap *world_bitmap;
static Layer *canvas;
static GBitmap *image;
static int redraw_counter;
// s is set to memory of size STR_SIZE, and temporarily stores strings
char *s;

int time_offset = 0;

static void flip_color(int sw){
  // swith=0 means day, other means night
  if(sw == 0){
    text_layer_set_background_color(time_text_layer, GColorWhite);
    text_layer_set_text_color(time_text_layer, GColorBlack);
    text_layer_set_background_color(date_text_layer, GColorWhite);
    text_layer_set_text_color(date_text_layer, GColorBlack);
    text_layer_set_background_color(bottom_text_layer, GColorWhite);
    text_layer_set_text_color(bottom_text_layer, GColorBlack);
  } else {
    text_layer_set_background_color(time_text_layer, GColorBlack);
    text_layer_set_text_color(time_text_layer, GColorWhite);
    text_layer_set_background_color(date_text_layer, GColorBlack);
    text_layer_set_text_color(date_text_layer, GColorWhite);
    text_layer_set_background_color(bottom_text_layer, GColorBlack);
    text_layer_set_text_color(bottom_text_layer, GColorWhite);
  }
}

static void draw_earth() {
  // ##### calculate the time
  int now = (int)time(NULL) + time_offset;
  float day_of_year; // value from 0 to 1 of progress through a year
  float time_of_day; // value from 0 to 1 of progress through a day
  // approx number of leap years since epoch
  // = now / SECONDS_IN_YEAR * .24; (.24 = average rate of leap years)
  int leap_years = (int)((float)now / 131487192.0);
  // day_of_year is an estimate, but should be correct to within one day
  day_of_year = now - (((int)((float)now / 31556926.0) * 365 + leap_years) * 86400);
  day_of_year = day_of_year / 86400.0;
  time_of_day = day_of_year - (int)day_of_year;
  day_of_year = day_of_year / 365.0;
  // ##### calculate the position of the sun
  // left to right of world goes from 0 to 65536
  int sun_x = (int)((float)TRIG_MAX_ANGLE * (1.0 - time_of_day));
  // bottom to top of world goes from -32768 to 32768
  // 0.2164 is march 20, the 79th day of the year, the march equinox
  // Earth's inclination is 23.4 degrees, so sun should vary 23.4/90=.26 up and down
  int sun_y = -sin_lookup((day_of_year - 0.2164) * TRIG_MAX_ANGLE) * .26 * .25;
  // ##### draw the bitmap
  int x, y;
  for(x = 0; x < WIDTH; x++) {
    int x_angle = (int)((float)TRIG_MAX_ANGLE * (float)x / (float)(WIDTH));
    for(y = 0; y < HEIGHT; y++) {
      int y_angle = (int)((float)TRIG_MAX_ANGLE * (float)y / (float)(HEIGHT * 2)) - TRIG_MAX_ANGLE/4;
      // spherical law of cosines
      float angle = ((float)sin_lookup(sun_y)/(float)TRIG_MAX_RATIO) * ((float)sin_lookup(y_angle)/(float)TRIG_MAX_RATIO);
      angle = angle + ((float)cos_lookup(sun_y)/(float)TRIG_MAX_RATIO) * ((float)cos_lookup(y_angle)/(float)TRIG_MAX_RATIO) * ((float)cos_lookup(sun_x - x_angle)/(float)TRIG_MAX_RATIO);
#ifdef PBL_BW
      int byte = y * gbitmap_get_bytes_per_row(world_bitmap) + (int)(x / 8);
      if ((angle < 0) ^ (0x1 & (((char *)gbitmap_get_data(world_bitmap))[byte] >> (7 - x % 8)))) {
        // white pixel
        ((char *)gbitmap_get_data(image))[byte] = ((char *)gbitmap_get_data(image))[byte] | (0x1 << (7 - x % 8));
      } else {
        // black pixel
        ((char *)gbitmap_get_data(image))[byte] = ((char *)gbitmap_get_data(image))[byte] & ~(0x1 << (7 - x % 8));
      }
#else
      int byte = y * gbitmap_get_bytes_per_row(world_bitmap) + (int)(x / 2);
      if (angle < 0) { // dark pixel
        ((char *)gbitmap_get_data(world_bitmap))[byte] = ((char *)gbitmap_get_data(world_bitmap))[(int)(WIDTH*HEIGHT / 2) + byte];
      } else { // light pixel
        ((char *)gbitmap_get_data(world_bitmap))[byte] = ((char *)gbitmap_get_data(world_bitmap))[WIDTH*HEIGHT + byte];
      }
#endif
      // Day/Night UI Check
      if(x == LOCAL_X && y == LOCAL_Y){
        if (angle < 0) {
          flip_color(1); // Night time
        } else {
          flip_color(0); // Day time
        }
      }
    }
  }
  layer_mark_dirty(canvas);
}

static void draw_watch(struct Layer *layer, GContext *ctx) {
  // Get the dynamic bounds of our canvas layer
  GRect bounds = layer_get_unobstructed_bounds(layer);
  
  // Calculate dynamic offsets to center the 144x100 map horizontally
  int map_offset_x = (bounds.size.w - WIDTH) / 2;
  int map_offset_y = 0; // Keeping it at the top
  
  // 1. Draw the earth bitmap at the centered offset
  graphics_draw_bitmap_in_rect(ctx, image, GRect(map_offset_x, map_offset_y, WIDTH, HEIGHT));
  
  // Only draw the crosshair if we have a valid location
  if (LOCAL_X >= 0 && LOCAL_Y >= 0) {
    // Shift the actual location by the dynamic map offset
    int cross_x = LOCAL_X + map_offset_x;
    int cross_y = LOCAL_Y + map_offset_y;
    int gap = ODOT_SIZE;

    #if defined(PBL_COLOR)
      graphics_context_set_stroke_width(ctx, CROSS_WIDTH);
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      
      graphics_draw_line(ctx, GPoint(map_offset_x, cross_y), GPoint(cross_x - gap, cross_y));
      graphics_draw_line(ctx, GPoint(cross_x + gap, cross_y), GPoint(map_offset_x + WIDTH, cross_y));
      graphics_draw_line(ctx, GPoint(cross_x, map_offset_y), GPoint(cross_x, cross_y - gap));
      graphics_draw_line(ctx, GPoint(cross_x, cross_y + gap), GPoint(cross_x, map_offset_y + HEIGHT));

      graphics_context_set_stroke_width(ctx, ODOT_WIDTH);
      graphics_draw_circle(ctx, GPoint(cross_x, cross_y), ODOT_SIZE);
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_circle(ctx, GPoint(cross_x, cross_y), IDOT_SIZE);
    #else 
      // Step A: Draw the White Line (3 pixels wide)
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 3);
      
      graphics_draw_line(ctx, GPoint(map_offset_x, cross_y), GPoint(cross_x - gap, cross_y));
      graphics_draw_line(ctx, GPoint(cross_x + gap, cross_y), GPoint(map_offset_x + WIDTH, cross_y));
      graphics_draw_line(ctx, GPoint(cross_x, map_offset_y), GPoint(cross_x, cross_y - gap));
      graphics_draw_line(ctx, GPoint(cross_x, cross_y + gap), GPoint(cross_x, map_offset_y + HEIGHT));
      
      // Draw a solid white background for the target circle
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(cross_x, cross_y), 4); 

      // Step B: Draw the Black Line (1 pixel wide) right down the middle
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 1);
      
      graphics_draw_line(ctx, GPoint(map_offset_x, cross_y), GPoint(cross_x - gap, cross_y));
      graphics_draw_line(ctx, GPoint(cross_x + gap, cross_y), GPoint(map_offset_x + WIDTH, cross_y));
      graphics_draw_line(ctx, GPoint(cross_x, map_offset_y), GPoint(cross_x, cross_y - gap));
      graphics_draw_line(ctx, GPoint(cross_x, cross_y + gap), GPoint(cross_x, map_offset_y + HEIGHT));

      // Draw the delicate black target ring and center dot
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_draw_circle(ctx, GPoint(cross_x, cross_y), 3);
      graphics_fill_circle(ctx, GPoint(cross_x, cross_y), 1);
    #endif
  }
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  static char time_text[] = "00:00";
  static char date_text[] = "00 Mth | 00 Day";

  strftime(date_text, sizeof(date_text), "%m %b | %d %a", tick_time);
  text_layer_set_text(date_text_layer, date_text);

  strftime(time_text, sizeof(time_text), "%I:%M", tick_time);
  text_layer_set_text(time_text_layer, time_text);
  
  // Redraw the earth at defined interval
  if (tick_time->tm_min % REDRAW_INTERVAL == 0) {
    draw_earth();
  }
}

// Get the time from the phone, which is probably UTC
// Calculate and store the offset when compared to the local clock
static void app_message_inbox_received(DictionaryIterator *iterator, void *context) {
  // 1. Time Sync Logic
  Tuple *t = dict_find(iterator, 0);
  if (t) {
    int unixtime = t->value->int32;
    int now = (int)time(NULL);
    time_offset = unixtime - now;
    persist_write_int(TIME_OFFSET_PERSIST, time_offset); 
  }

  // 2. Location Logic
  Tuple *lat_tuple = dict_find(iterator, 1);
  Tuple *lon_tuple = dict_find(iterator, 2);
  
  if (lat_tuple && lon_tuple) {
    int32_t lat_val = lat_tuple->value->int32;
    int32_t lon_val = lon_tuple->value->int32;

    // Save to persistent storage so it's there next time we start
    persist_write_int(LAT_PERSIST, lat_val);
    persist_write_int(LON_PERSIST, lon_val);

    // We divide by 10000 because JS sends floats as scaled integers 
    float lat = (float)lat_tuple->value->int32 / 10000.0;
    float lon = (float)lon_tuple->value->int32 / 10000.0;

    // Convert real-world coordinates to map pixels
    LOCAL_X = (int)(((lon + 180.0) / 360.0) * WIDTH);
    LOCAL_Y = (int)(((90.0 - lat) / 180.0) * HEIGHT);
    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "New Location Received: X:%d, Y:%d", LOCAL_X, LOCAL_Y);
  }

  // Redraw the map with the new data
  draw_earth();
}

static void window_load(Window *window) {
#ifdef BLACK_ON_WHITE
  GColor background_color = GColorBlack;
  GColor foreground_color = GColorWhite;
#else
  GColor background_color = GColorWhite;
  GColor foreground_color = GColorBlack;
#endif
  window_set_background_color(window, background_color);
  
  // Get the dynamic bounds of the watch face
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int text_area_start = HEIGHT - TEXT_OFFSET;
  int remaining_height = bounds.size.h - text_area_start;
  
  int time_height = (remaining_height * 60) / 100;
  int date_height = remaining_height - time_height;

  time_text_layer = text_layer_create(GRect(0, text_area_start, bounds.size.w, time_height));
  text_layer_set_background_color(time_text_layer, background_color);
  text_layer_set_text_color(time_text_layer, foreground_color);
  text_layer_set_font(time_text_layer, fonts_get_system_font(TIME_FONT));
  text_layer_set_text(time_text_layer, "");
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));

  // The bottom text layer acts as a background block for the date
  bottom_text_layer = text_layer_create(GRect(0, text_area_start + time_height, bounds.size.w, date_height));
  text_layer_set_background_color(bottom_text_layer, background_color);
  text_layer_set_text_color(bottom_text_layer, foreground_color);
  layer_add_child(window_layer, text_layer_get_layer(bottom_text_layer));

  date_text_layer = text_layer_create(GRect(0, text_area_start + time_height, bounds.size.w, date_height));
  text_layer_set_background_color(date_text_layer, background_color);
  text_layer_set_text_color(date_text_layer, foreground_color);
  text_layer_set_font(date_text_layer, fonts_get_system_font(DATE_FONT));
  text_layer_set_text(date_text_layer, "");
  text_layer_set_text_alignment(date_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(date_text_layer));

  // Canvas covers the whole screen to allow flexible drawing
  canvas = layer_create(bounds);
  layer_set_update_proc(canvas, draw_watch);
  layer_add_child(window_layer, canvas);
#ifdef PBL_BW
  image = gbitmap_create_with_resource(RESOURCE_ID_WORLD);
#else
  image = gbitmap_create_as_sub_bitmap(world_bitmap, GRect(0, 0, WIDTH, HEIGHT));
#endif
  draw_earth();
}

static void window_unload(Window *window) {
  text_layer_destroy(time_text_layer);
  text_layer_destroy(date_text_layer);
  text_layer_destroy(bottom_text_layer);
  layer_destroy(canvas);
  gbitmap_destroy(image);
}

static void init(void) {
  redraw_counter = 0;

  // Load the UTC offset, if it exists
  time_offset = 0;
  if (persist_exists(TIME_OFFSET_PERSIST)) {
    time_offset = persist_read_int(TIME_OFFSET_PERSIST);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "loaded offset %d", time_offset);
  }
  
  // Load the Last Known Location
  if (persist_exists(LAT_PERSIST) && persist_exists(LON_PERSIST)) {
    int32_t lat_val = persist_read_int(LAT_PERSIST);
    int32_t lon_val = persist_read_int(LON_PERSIST);

    float lat = (float)lat_val / 10000.0;
    float lon = (float)lon_val / 10000.0;

    LOCAL_X = (int)(((lon + 180.0) / 360.0) * WIDTH);
    LOCAL_Y = (int)(((90.0 - lat) / 180.0) * HEIGHT);
    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded saved location: X:%d, Y:%d", LOCAL_X, LOCAL_Y);
  }
  
  world_bitmap = gbitmap_create_with_resource(RESOURCE_ID_WORLD);
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  
  const bool animated = true;
  window_stack_push(window, animated);

  s = malloc(STR_SIZE);
  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  handle_minute_tick(tick_time, MINUTE_UNIT);

  app_message_register_inbox_received(app_message_inbox_received);
  app_message_open(128, 128);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  free(s);
  window_destroy(window);
  gbitmap_destroy(world_bitmap);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}