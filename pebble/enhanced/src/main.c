/*
 * Enhanced Loop CGM Monitor - Pebble Watch App
 * 
 * Improved UX based on modern Pebble design principles:
 * - Better information hierarchy
 * - Icon-based navigation
 * - Enhanced data visualization
 * - More intuitive menu system
 * - Optimized for battery efficiency
 * 
 * Note: Touchscreen not available in current SDK but designed for future compatibility
 */

#include <pebble.h>

// ==================== Configuration ==========

// App settings
#define REFRESH_INTERVAL_MS (5 * 60 * 1000)  // 5 minutes
#define GLUCOSE_UPDATE_THRESHOLD 5           // Only update if change > threshold
#define BOLUS_MIN 0.05
#define BOLUS_MAX 10.0
#define BOLUS_STEP 0.05
#define CARBS_MIN 5
#define CARBS_MAX 200
#define CARBS_STEP 5

// Alert thresholds
#define LOW_GLUCOSE 70
#define HIGH_GLUCOSE 180
#define VERY_LOW_GLUCOSE 55
#define VERY_HIGH_GLUCOSE 250

// ==================== UI Layout ==========

// Main screen layout:
// [ TIME ]
// [ GLUCOSE (LARGE) ]
// [ TREND ARROW + UNITS ]
// [ STATUS BAR ]
// [ QUICK ACTIONS HINT ]

// ==================== Data Structures ==========

typedef struct {
    int glucose;           // mg/dL
    char trend[8];         // "doubleUp", "singleUp", etc.
    int iob_tenths;        // IOB * 10 for precision
    bool isClosedLoop;     // Loop status
    int cob;               // Carbs on board
    int battery;           // Pump battery %
    time_t timestamp;      // Data timestamp
} WatchData;

typedef struct {
    bool glucose_valid;
    bool trend_valid;
    bool iob_valid;
    bool loop_valid;
    bool cob_valid;
    bool battery_valid;
} DataValidity;

// ==================== UI Elements ==========

// Main Window
static Window *s_main_window;

// Time
static TextLayer *s_time_layer;

// Glucose (prominent display)
static TextLayer *s_glucose_layer;

// Trend indicator (with arrow)
static TextLayer *s_trend_layer;
static TextLayer *s_units_layer;

// Status bar (compact info)
static TextLayer *s_status_layer;

// Hint layer
static TextLayer *s_hint_layer;

// Menu Window
static Window *s_menu_window;
static SimpleMenuLayer *s_menu_layer;
static SimpleMenuItem s_menu_items[4];
static SimpleMenuSection s_menu_section;

// Entry Windows
static Window *s_entry_window;
static TextLayer *s_entry_title_layer;
static TextLayer *s_entry_value_layer;
static TextLayer *s_entry_hint_layer;

// Confirmation Window
static Window *s_confirm_window;
static TextLayer *s_confirm_title_layer;
static TextLayer *s_confirm_msg_layer;

// Icons
static GBitmap *s_icon_bolus;
static GBitmap *s_icon_carbs;
static GBitmap *s_icon_settings;
static GBitmap *s_icon_refresh;

// ==================== State ==========

static WatchData s_data;
static DataValidity s_valid;
static double s_bolus_amount = 0.5;
static int s_carbs_amount = 10;
static time_t s_last_update = 0;
static bool s_is_initial_load = true;

// ==================== AppMessage Keys ==========

#define KEY_GLUCOSE 0
#define KEY_TREND 1
#define KEY_IOB 2
#define KEY_IS_CLOSED_LOOP 3
#define KEY_COB 4
#define KEY_BATTERY 5
#define KEY_REQUEST_DATA 6
#define KEY_BOLUS_REQUEST 7
#define KEY_CARB_REQUEST 8
#define KEY_ABSORPTION_HOURS 9
#define KEY_COMMAND_STATUS 10
#define KEY_COMMAND_MSG 11
#define KEY_DATA_TIMESTAMP 12

// ==================== Helper Functions ==========

static void request_data_from_iphone(void) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        dict_write_uint8(iter, KEY_REQUEST_DATA, 1);
        app_message_outbox_send();
    }
}

static void send_bolus_request(double units) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        // Send as integer (units * 20 for 0.05U precision)
        dict_write_int32(iter, KEY_BOLUS_REQUEST, (int)(units * 20));
        app_message_outbox_send();
    }
}

static void send_carb_request(int grams, int absorption_hours) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        dict_write_int32(iter, KEY_CARB_REQUEST, grams);
        dict_write_int32(iter, KEY_ABSORPTION_HOURS, absorption_hours);
        app_message_outbox_send();
    }
}

static void update_time_display(void) {
    time_t now = time(NULL);
    struct tm *tick_time = localtime(&now);
    strftime(s_time_buffer, sizeof(s_time_buffer), "%H:%M", tick_time);
    text_layer_set_text(s_time_layer, s_time_buffer);
}

static void check_glucose_alerts(int glucose) {
    static time_t last_alert = 0;
    time_t now = time(NULL);
    
    // Don't alert too frequently (every 15 minutes max)
    if (now - last_alert < 15 * 60) return;
    
    if (glucose <= VERY_LOW_GLUCOSE) {
        // Very low - urgent alert
        vibes_double_pulse();
        last_alert = now;
    } else if (glucose <= LOW_GLUCOSE) {
        // Low - standard alert
        vibes_short_pulse();
        last_alert = now;
    } else if (glucose >= VERY_HIGH_GLUCOSE) {
        // Very high - urgent alert
        vibes_double_pulse();
        last_alert = now;
    } else if (glucose >= HIGH_GLUCOSE) {
        // High - standard alert
        vibes_short_pulse();
        last_alert = now;
    }
}

static void update_trend_arrow(const char *trend) {
    // Map Trio trend strings to visual indicators
    if (strcmp(trend, "doubleUp") == 0 || strcmp(trend, "tripleUp") == 0) {
        text_layer_set_text(s_trend_layer, "↑↑");
    } else if (strcmp(trend, "singleUp") == 0 || strcmp(trend, "fortyFiveUp") == 0) {
        text_layer_set_text(s_trend_layer, "↑");
    } else if (strcmp(trend, "doubleDown") == 0 || strcmp(trend, "tripleDown") == 0) {
        text_layer_set_text(s_trend_layer, "↓↓");
    } else if (strcmp(trend, "singleDown") == 0 || strcmp(trend, "fortyFiveDown") == 0) {
        text_layer_set_text(s_trend_layer, "↓");
    } else if (strcmp(trend, "flat") == 0) {
        text_layer_set_text(s_trend_layer, "→");
    } else {
        text_layer_set_text(s_trend_layer, "?");
    }
}

// ==================== Entry Window ==========

static void entry_window_update_value(void) {
    if (strcmp(s_entry_mode, "bolus") == 0) {
        snprintf(s_entry_value_buffer, sizeof(s_entry_value_buffer), "%.2f U", s_bolus_amount);
    } else if (strcmp(s_entry_mode, "carbs") == 0) {
        snprintf(s_entry_value_buffer, sizeof(s_entry_value_buffer), "%d g", s_carbs_amount);
    }
    text_layer_set_text(s_entry_value_layer, s_entry_value_buffer);
}

static void entry_select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (strcmp(s_entry_mode, "bolus") == 0) {
        send_bolus_request(s_bolus_amount);
        window_stack_pop(false);
        show_confirmation_window("Confirm bolus on iPhone");
    } else if (strcmp(s_entry_mode, "carbs") == 0) {
        send_carb_request(s_carbs_amount, 3); // Default 3h absorption
        window_stack_pop(false);
        show_confirmation_window("Confirm carbs on iPhone");
    }
}

static void entry_up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (strcmp(s_entry_mode, "bolus") == 0) {
        if (s_bolus_amount + BOLUS_STEP <= BOLUS_MAX) {
            s_bolus_amount += BOLUS_STEP;
            entry_window_update_value();
            vibes_short_pulse();
        }
    } else if (strcmp(s_entry_mode, "carbs") == 0) {
        if (s_carbs_amount + CARBS_STEP <= CARBS_MAX) {
            s_carbs_amount += CARBS_STEP;
            entry_window_update_value();
            vibes_short_pulse();
        }
    }
}

static void entry_down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (strcmp(s_entry_mode, "bolus") == 0) {
        if (s_bolus_amount - BOLUS_STEP >= BOLUS_MIN) {
            s_bolus_amount -= BOLUS_STEP;
            entry_window_update_value();
            vibes_short_pulse();
        }
    } else if (strcmp(s_entry_mode, "carbs") == 0) {
        if (s_carbs_amount - CARBS_STEP >= CARBS_MIN) {
            s_carbs_amount -= CARBS_STEP;
            entry_window_update_value();
            vibes_short_pulse();
        }
    }
}

static void entry_click_config(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, entry_select_click_handler);
    window_single_click_subscribe(BUTTON_ID_UP, entry_up_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, entry_down_click_handler);
}

static void entry_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    s_entry_title_layer = text_layer_create(GRect(0, 10, bounds.size.w, 30));
    text_layer_set_font(s_entry_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_entry_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_entry_title_layer, GColorClear);
    layer_add_child(window_layer, text_layer_get_layer(s_entry_title_layer));
    
    s_entry_value_layer = text_layer_create(GRect(0, 50, bounds.size.w, 40));
    text_layer_set_font(s_entry_value_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
    text_layer_set_text_alignment(s_entry_value_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_entry_value_layer, GColorClear);
    #ifdef PBL_COLOR
    if (strcmp(s_entry_mode, "bolus") == 0) {
        text_layer_set_text_color(s_entry_value_layer, GColorCyan);
    } else {
        text_layer_set_text_color(s_entry_value_layer, GColorOrange);
    }
    #endif
    layer_add_child(window_layer, text_layer_get_layer(s_entry_value_layer));
    
    s_entry_hint_layer = text_layer_create(GRect(10, 100, bounds.size.w - 20, 60));
    text_layer_set_font(s_entry_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(s_entry_hint_layer, GTextAlignmentCenter);
    if (strcmp(s_entry_mode, "bolus") == 0) {
        text_layer_set_text(s_entry_hint_layer, "▲▼ to adjust\nSELECT to send\nRequires iPhone confirmation");
    } else {
        text_layer_set_text(s_entry_hint_layer, "▲▼ to adjust\nSELECT to send\nRequires iPhone confirmation");
    }
    text_layer_set_background_color(s_entry_hint_layer, GColorClear);
    layer_add_child(window_layer, text_layer_get_layer(s_entry_hint_layer));
    
    entry_window_update_value();
}

static void entry_window_unload(Window *window) {
    text_layer_destroy(s_entry_title_layer);
    text_layer_destroy(s_entry_value_layer);
    text_layer_destroy(s_entry_hint_layer);
}

static void show_bolus_entry(void) {
    strcpy(s_entry_mode, "bolus");
    s_bolus_amount = 0.5; // Reset to default
    
    s_entry_window = window_create();
    window_set_background_color(s_entry_window, GColorBlack);
    window_set_click_config_provider(s_entry_window, entry_click_config);
    window_set_window_handlers(s_entry_window, (WindowHandlers) {
        .load = entry_window_load,
        .unload = entry_window_unload
    });
    window_stack_push(s_entry_window, true);
}

static void show_carb_entry(void) {
    strcpy(s_entry_mode, "carbs");
    s_carbs_amount = 10; // Reset to default
    
    s_entry_window = window_create();
    window_set_background_color(s_entry_window, GColorBlack);
    window_set_click_config_provider(s_entry_window, entry_click_config);
    window_set_window_handlers(s_entry_window, (WindowHandlers) {
        .load = entry_window_load,
        .unload = entry_window_unload
    });
    window_stack_push(s_entry_window, true);
}

// ==================== Menu Window ==========

static void menu_bolus_callback(int index, void *ctx) {
    show_bolus_entry();
}

static void menu_carbs_callback(int index, void *ctx) {
    show_carb_entry();
}

static void menu_settings_callback(int index, void *ctx) {
    // Future: Settings for refresh rate, alerts, etc.
    show_confirmation_window("Settings coming soon");
}

static void menu_refresh_callback(int index, void *ctx) {
    request_data_from_iphone();
    show_confirmation_window("Refreshing data...");
}

static void menu_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    // Load icons
    s_icon_bolus = gbitmap_create_with_resource(IMAGE_ICON_BOLUS);
    s_icon_carbs = gbitmap_create_with_resource(IMAGE_ICON_CARBS);
    s_icon_settings = gbitmap_create_with_resource(IMAGE_ICON_SETTINGS);
    s_icon_refresh = gbitmap_create_with_resource(IMAGE_ICON_REFRESH);
    
    s_menu_items[0] = (SimpleMenuItem){
        .title = "Bolus",
        .icon = s_icon_bolus,
        .callback = menu_bolus_callback,
    };
    s_menu_items[1] = (SimpleMenuItem){
        .title = "Carbs",
        .icon = s_icon_carbs,
        .callback = menu_carbs_callback,
    };
    s_menu_items[2] = (SimpleMenuItem){
        .title = "Settings",
        .icon = s_icon_settings,
        .callback = menu_settings_callback,
    };
    s_menu_items[3] = (SimpleMenuItem){
        .title = "Refresh",
        .icon = s_icon_refresh,
        .callback = menu_refresh_callback,
    };
    
    s_menu_section = (SimpleMenuSection){
        .items = s_menu_items,
        .num_items = 4,
    };
    
    s_menu_layer = simple_menu_layer_create(bounds, window, &s_menu_section, 1, NULL);
    layer_add_child(window_layer, simple_menu_layer_get_layer(s_menu_layer));
}

static void menu_window_unload(Window *window) {
    simple_menu_layer_destroy(s_menu_layer);
    gbitmap_destroy(s_icon_bolus);
    gbitmap_destroy(s_icon_carbs);
    gbitmap_destroy(s_icon_settings);
    gbitmap_destroy(s_icon_refresh);
}

// ==================== Confirmation Window ==========

static void confirm_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    s_confirm_title_layer = text_layer_create(GRect(0, 20, bounds.size.w, 30));
    text_layer_set_font(s_confirm_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_confirm_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_confirm_title_layer, GColorClear);
    #ifdef PBL_COLOR
    text_layer_set_text_color(s_confirm_title_layer, GColorGreen);
    #endif
    layer_add_child(window_layer, text_layer_get_layer(s_confirm_title_layer));
    
    s_confirm_msg_layer = text_layer_create(GRect(10, 60, bounds.size.w - 20, 80));
    text_layer_set_font(s_confirm_msg_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(s_confirm_msg_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_confirm_msg_layer, GColorClear);
    layer_add_child(window_layer, text_layer_get_layer(s_confirm_msg_layer));
}

static void confirm_window_unload(Window *window) {
    text_layer_destroy(s_confirm_title_layer);
    text_layer_destroy(s_confirm_msg_layer);
}

static void show_confirmation_window(const char *message) {
    s_confirm_window = window_create();
    window_set_background_color(s_confirm_window, GColorBlack);
    window_set_window_handlers(s_confirm_window, (WindowHandlers) {
        .load = confirm_window_load,
        .unload = confirm_window_unload
    });
    window_stack_push(s_confirm_window, true);
    
    if (message) {
        text_layer_set_text(s_confirm_msg_layer, message);
    }
    
    // Auto-dismiss after 3 seconds for non-confirmation messages
    if (strstr(message, "coming soon") || strstr(message, "Refreshing")) {
        app_timer_register(3000, (AppTimerCallback)window_stack_pop, s_confirm_window);
    }
}

// ==================== Main Window ==========

static void main_select_click_handler(ClickRecognizerRef recognizer, void *context) {
    // Open main menu
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) {
        .load = menu_window_load,
        .unload = menu_window_unload
    });
    window_stack_push(s_menu_window, true);
}

static void main_up_click_handler(ClickRecognizerRef recognizer, void *context) {
    // Quick bolus (0.5U)
    send_bolus_request(0.5);
    show_confirmation_window("Sent 0.5U bolus request");
}

static void main_down_click_handler(ClickRecognizerRef recognizer, void *context) {
    // Quick carbs (10g)
    send_carb_request(10, 3);
    show_confirmation_window("Sent 10g carbs request");
}

static void main_click_config(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, main_select_click_handler);
    window_single_click_subscribe(BUTTON_ID_UP, main_up_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, main_down_click_handler);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    // Handle command status responses
    Tuple *status_tuple = dict_find(iterator, KEY_COMMAND_STATUS);
    if (status_tuple) {
        int status = (int)status_tuple->value->int32;
        Tuple *msg_tuple = dict_find(iterator, KEY_COMMAND_MSG);
        const char *msg = msg_tuple ? msg_tuple->value->cstring : NULL;
        
        if (status == 1) {
            // Pending confirmation from iPhone
            show_confirmation_window(msg ? msg : "Check iPhone to confirm");
        } else if (status == -1) {
            // Error
            show_confirmation_window(msg ? msg : "Request failed");
        }
        return;
    }
    
    // Handle data updates
    bool data_changed = false;
    
    Tuple *glucose_tuple = dict_find(iterator, KEY_GLUCOSE);
    if (glucose_tuple) {
        int new_glucose = (int)glucose_tuple->value->int32;
        if (abs(new_glucose - s_data.glucose) > GLUCOSE_UPDATE_THRESHOLD || s_is_initial_load) {
            s_data.glucose = new_glucose;
            s_valid.glucose = true;
            data_changed = true;
            check_glucose_alerts(new_glucose);
        }
    }
    
    Tuple *trend_tuple = dict_find(iterator, KEY_TREND);
    if (trend_tuple) {
        strncpy(s_data.trend, trend_tuple->value->cstring, sizeof(s_data.trend)-1);
        s_data.trend[sizeof(s_data.trend)-1] = '\0';
        s_valid.trend = true;
        data_changed = true;
    }
    
    Tuple *iob_tuple = dict_find(iterator, KEY_IOB);
    if (iob_tuple) {
        int new_iob = (int)iob_tuple->value->int32;
        if (new_iob != s_data.iob_tenths || s_is_initial_load) {
            s_data.iob_tenths = new_iob;
            s_valid.iob = true;
            data_changed = true;
        }
    }
    
    Tuple *loop_tuple = dict_find(iterator, KEY_IS_CLOSED_LOOP);
    if (loop_tuple) {
        bool new_closed = loop_tuple->value->int32 > 0;
        if (new_closed != s_data.isClosedLoop || s_is_initial_load) {
            s_data.isClosedLoop = new_closed;
            s_valid.loop = true;
            data_changed = true;
        }
    }
    
    Tuple *cob_tuple = dict_find(iterator, KEY_COB);
    if (cob_tuple) {
        int new_cob = (int)cob_tuple->value->int32;
        if (new_cob != s_data.cob || s_is_initial_load) {
            s_data.cob = new_cob;
            s_valid.cob = true;
            data_changed = true;
        }
    }
    
    Tuple *battery_tuple = dict_find(iterator, KEY_BATTERY);
    if (battery_tuple) {
        int new_battery = (int)battery_tuple->value->int32;
        if (new_battery != s_data.battery || s_is_initial_load) {
            s_data.battery = new_battery;
            s_valid.battery = true;
            data_changed = true;
        }
    }
    
    Tuple *timestamp_tuple = dict_find(iterator, KEY_DATA_TIMESTAMP);
    if (timestamp_tuple) {
        time_t new_timestamp = (time_t)timestamp_tuple->value->int32;
        if (new_timestamp != s_data.timestamp || s_is_initial_load) {
            s_data.timestamp = new_timestamp;
            s_last_update = new_timestamp;
            data_changed = true;
        }
    }
    
    if (data_changed || s_is_initial_load) {
        update_main_display();
        s_is_initial_load = false;
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    // Silently handle dropped messages to save battery
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    // Show connection error briefly
    show_confirmation_window("Connection error");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
    // Success - no UI feedback needed to save battery
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time_display();
    
    // Request data every 5 minutes (on the 5-minute boundary)
    if (tick_time->tm_min % 5 == 0 && tick_time->tm_sec < 30) {
        request_data_from_iphone();
    }
}

static void update_main_display(void) {
    // Time (always updated)
    update_time_display();
    
    // Glucose (prominent)
    if (s_valid.glucose) {
        snprintf(s_glucose_buffer, sizeof(s_glucose_buffer), "%d", s_data.glucose);
        text_layer_set_text(s_glucose_layer, s_glucose_buffer);
        
        #ifdef PBL_COLOR
        if (s_data.glucose <= VERY_LOW_GLUCOSE) {
            text_layer_set_text_color(s_glucose_layer, GColorRed);
        } else if (s_data.glucose <= LOW_GLUCOSE) {
            text_layer_set_text_color(s_glucose_layer, GColorOrange);
        } else if (s_data.glucose >= VERY_HIGH_GLUCOSE) {
            text_layer_set_text_color(s_glucose_layer, GColorRed);
        } else if (s_data.glucose >= HIGH_GLUCOSE) {
            text_layer_set_text_color(s_glucose_layer, GColorOrange);
        } else {
            text_layer_set_text_color(s_glucose_layer, GColorGreen);
        }
        #endif
    } else {
        text_layer_set_text(s_glucose_layer, "--");
    }
    
    // Trend arrow
    if (s_valid.trend) {
        update_trend_arrow(s_data.trend);
    } else {
        text_layer_set_text(s_trend_layer, "?");
    }
    
    // Units label
    text_layer_set_text(s_units_layer, "mg/dL");
    
    // Status bar (compact)
    if (s_valid.iob || s_valid.loop || s_valid.cob || s_valid.battery) {
        snprintf(s_status_buffer, sizeof(s_status_buffer), 
                 "IOB:%d.%d %s COB:%d  🔋:%d%%",
                 s_data.iob_tenths / 10, abs(s_data.iob_tenths % 10),
                 s_data.isClosedLoop ? "ON" : "OFF",
                 s_data.cob,
                 s_data.battery);
        text_layer_set_text(s_status_layer, s_status_buffer);
    } else {
        text_layer_set_text(s_status_layer, "--");
    }
    
    // Hint
    text_layer_set_text(s_hint_layer, "SELECT Menu  ▲ Quick Bolus  ▼ Quick Carbs");
}

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    // Initialize buffers
    strcpy(s_glucose_buffer, "--");
    strcpy(s_trend_buffer, "?");
    strcpy(s_units_buffer, "mg/dL");
    strcpy(s_status_buffer, "--");
    strcpy(s_time_buffer, "--:--");
    
    // Time (top-right, compact)
    s_time_layer = text_layer_create(GRect(bounds.size.w - 50, 0, 50, 20));
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentRight);
    text_layer_set_background_color(s_time_layer, GColorClear);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
    
    // Glucose (center, prominent)
    s_glucose_layer = text_layer_create(GRect(0, 30, bounds.size.w, 50));
    text_layer_set_font(s_glucose_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_glucose_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_glucose_layer, GColorClear);
    text_layer_set_text(s_glucose_layer, "--");
    layer_add_child(window_layer, text_layer_get_layer(s_glucose_layer));
    
    // Trend (below glucose)
    s_trend_layer = text_layer_create(GRect(0, 85, bounds.size.w, 30));
    text_layer_set_font(s_trend_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(s_trend_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_trend_layer, GColorClear);
    text_layer_set_text(s_trend_layer, "?");
    layer_add_child(window_layer, text_layer_get_layer(s_trend_layer));
    
    // Units (beside trend)
    s_units_layer = text_layer_create(GRect(0, 85, bounds.size.w, 30));
    text_layer_set_font(s_units_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_units_layer, GTextAlignmentLeft);
    text_layer_set_background_color(s_units_layer, GColorClear);
    layer_add_child(window_layer, text_layer_get_layer(s_units_layer));
    
    // Status bar (bottom)
    s_status_layer = text_layer_create(GRect(0, bounds.size.h - 30, bounds.size.w, 24));
    text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_status_layer, GColorClear);
    text_layer_set_text(s_status_layer, "--");
    layer_add_child(window_layer, text_layer_get_layer(s_status_layer));
    
    // Hint (very bottom)
    s_hint_layer = text_layer_create(GRect(0, bounds.size.h - 10, bounds.size.w, 10));
    text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_hint_layer, GColorClear);
    text_layer_set_text(s_hint_layer, "Select for menu");
    layer_add_child(window_layer, text_layer_get_layer(s_hint_layer));
}

static void main_window_unload(Window *window) {
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_glucose_layer);
    text_layer_destroy(s_trend_layer);
    text_layer_destroy(s_units_layer);
    text_layer_destroy(s_status_layer);
    text_layer_destroy(s_hint_layer);
}

// ==================== Init/Deinit ==========

static void init(void) {
    // Initialize state
    memset(&s_data, 0, sizeof(s_data));
    memset(&s_valid, 0, sizeof(s_valid));
    s_is_initial_load = true;
    s_last_update = 0;
    
    // AppMessage
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);
    
    // Reasonable buffer sizes
    app_message_open(128, 128);
    
    // Main window
    s_main_window = window_create();
    window_set_background_color(s_main_window, GColorBlack);
    window_set_click_config_provider(s_main_window, main_click_config);
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);
    
    // Time service
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    
    // Initial data request
    request_data_from_iphone();
    
    // Initial time update
    update_time_display();
}

static void deinit(void) {
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
