SCREEN TRANSITIONS IMPLEMENTATION GUIDE

To complete the implementation of screen transitions in your M5Stack CoreS3 Loss Prevention Log application, 
follow these steps to update your code:

1. CONFIRM SCREEN TRANSITIONS:
--------------------------------------------------------
void createConfirmScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(confirmScreen, TRANSITION_ZOOM_IN, 300);
}

2. SETTINGS SCREEN TRANSITIONS:
--------------------------------------------------------
void createSettingsScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(settingsScreen, TRANSITION_SLIDE_LEFT, 300);
}

void createSoundSettingsScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(sound_settings_screen, TRANSITION_SLIDE_UP, 300);
}

void createBrightnessSettingsScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(brightness_settings_screen, TRANSITION_SLIDE_UP, 300);
}

3. WIFI SCREEN TRANSITIONS:
--------------------------------------------------------
void createWiFiScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(wifi_screen, TRANSITION_SLIDE_LEFT, 300);
}

void createWiFiManagerScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(wifi_manager_screen, TRANSITION_SLIDE_LEFT, 300);
}

void showWiFiLoadingScreen(const String& ssid) {
   /* existing code */
   
   // Replace lv_scr_load with:
   load_screen_with_animation(wifi_loading_screen, TRANSITION_FADE, 300);
}

4. LOGS VIEW TRANSITION:
--------------------------------------------------------
void createViewLogsScreen() {
   /* existing code */
   
   // At the end of the function:
   load_screen_with_animation(logs_screen, TRANSITION_SLIDE_LEFT, 300);
}

5. BACK BUTTON TRANSITIONS:
--------------------------------------------------------
For all your "Back" buttons across screens, update the handlers to use a different
transition style (like TRANSITION_SLIDE_RIGHT) to create a consistent feeling of 
navigating backward in the UI flow. For example:

// In createViewLogsScreen:
lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
    if (e->code == LV_EVENT_CLICKED) {
        M5.Speaker.tone(1800, 40);
        createMainMenu();
        load_screen_with_animation(main_menu_screen, TRANSITION_SLIDE_RIGHT, 300);
    }
}, LV_EVENT_CLICKED, NULL);

// In createGenderMenu:
lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
    if (e->code == LV_EVENT_CLICKED) {
        M5.Speaker.tone(1800, 40);
        createMainMenu();
        load_screen_with_animation(main_menu_screen, TRANSITION_SLIDE_RIGHT, 300);
    }
}, LV_EVENT_CLICKED, NULL);

// Similar for all other back buttons

6. ADDITIONAL TRANSITION EFFECTS TO CONSIDER:
--------------------------------------------------------
- Use TRANSITION_OVER_LEFT/RIGHT for screens that overlay (like popups)
- Use TRANSITION_ZOOM_OUT for returning to previous screens
- Use TRANSITION_FADE for screens that don't have a clear directional relationship

7. TRANSITION RECOMMENDATIONS FOR DUAL BUTTON MODULE:
--------------------------------------------------------
Since you're using the M5Stack Dual Button unit, consider mapping these buttons to
navigation actions:
- Left button: Go back (with TRANSITION_SLIDE_RIGHT)
- Right button: Confirm/Next (with TRANSITION_SLIDE_LEFT)

This would create a consistent, intuitive UI flow with appropriate transitions.

8. PERFORMANCE CONSIDERATIONS:
--------------------------------------------------------
Transitions require more processing power. If you notice any performance issues:
- Use simpler transitions like FADE instead of ZOOM
- Reduce the transition duration (e.g., from 300ms to 200ms)
- Consider adding this helper function to disable transitions when battery is low:

void set_transition_mode(bool enabled) {
    if (enabled) {
        defaultTransition = TRANSITION_FADE;
        defaultTransitionDuration = 300;
    } else {
        defaultTransition = TRANSITION_NONE;
        defaultTransitionDuration = 0;
    }
}

Then call this when battery is low:
if (M5.Power.getBatteryLevel() < 15) {
    set_transition_mode(false);
} else {
    set_transition_mode(true);
}


/*
 * Screen Transition Utilities for M5Stack CoreS3
 * This file provides reusable screen transition effects using LVGL
 */

#ifndef SCREEN_TRANSITION_H
#define SCREEN_TRANSITION_H

#include <lvgl.h>

// Transition types
typedef enum {
    TRANSITION_NONE,        // No animation (instant)
    TRANSITION_FADE,        // Fade in/out
    TRANSITION_SLIDE_LEFT,  // Current screen exits left, new screen enters from right
    TRANSITION_SLIDE_RIGHT, // Current screen exits right, new screen enters from left
    TRANSITION_SLIDE_UP,    // Current screen exits up, new screen enters from bottom
    TRANSITION_SLIDE_DOWN,  // Current screen exits down, new screen enters from top
    TRANSITION_ZOOM_IN,     // New screen zooms in
    TRANSITION_ZOOM_OUT,    // Current screen zooms out, new screen appears
    TRANSITION_OVER_LEFT,   // New screen slides over from left
    TRANSITION_OVER_RIGHT,  // New screen slides over from right
    TRANSITION_OVER_TOP,    // New screen slides over from top
    TRANSITION_OVER_BOTTOM  // New screen slides over from bottom
} screen_transition_type_t;

// Default transition duration in milliseconds
#define DEFAULT_TRANSITION_TIME 300

// Screen transition data (used internally)
typedef struct {
    lv_obj_t* old_screen;
    lv_obj_t* new_screen;
    screen_transition_type_t type;
} screen_transition_data_t;

// Forward declarations
static void transition_fade_anim(void* var, int32_t v);
static void transition_slide_anim(void* var, int32_t v);
static void transition_zoom_anim(void* var, int32_t v);
static void transition_over_anim(void* var, int32_t v);
static void transition_anim_ready_cb(lv_anim_t* a);

// Transition state variables
static screen_transition_data_t transition_data;
static lv_anim_t a_old, a_new;

/**
 * Load a new screen with transition animation
 * @param new_screen The screen to load
 * @param type The transition type
 * @param time Animation time in milliseconds (0 = use default)
 */
void load_screen_with_animation(lv_obj_t* new_screen, screen_transition_type_t type, uint32_t time = DEFAULT_TRANSITION_TIME) {
    // If animation time is 0, use default
    if (time == 0) time = DEFAULT_TRANSITION_TIME;
    
    // If no transition is requested, just load the screen normally
    if (type == TRANSITION_NONE) {
        lv_scr_load(new_screen);
        return;
    }
    
    // Get the current screen
    lv_obj_t* old_screen = lv_scr_act();
    
    // If same screen, do nothing
    if (old_screen == new_screen) return;
    
    // Store transition data
    transition_data.old_screen = old_screen;
    transition_data.new_screen = new_screen;
    transition_data.type = type;
    
    // Initialize animations
    lv_anim_init(&a_old);
    lv_anim_init(&a_new);
    
    // Set animation time
    lv_anim_set_time(&a_old, time);
    lv_anim_set_time(&a_new, time);
    
    // Set animation targets
    lv_anim_set_var(&a_old, old_screen);
    lv_anim_set_var(&a_new, new_screen);
    
    // Make sure the new screen is visible
    lv_obj_set_style_opa(new_screen, LV_OPA_TRANSP, 0);
    
    // Start with different transition types
    switch (type) {
        case TRANSITION_FADE:
            // Fade animation
            lv_anim_set_values(&a_old, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_values(&a_new, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_exec_cb(&a_old, transition_fade_anim);
            lv_anim_set_exec_cb(&a_new, transition_fade_anim);
            break;
            
        case TRANSITION_SLIDE_LEFT:
            // Slide left animation (current exits left, new enters from right)
            lv_anim_set_values(&a_old, 0, -lv_obj_get_width(old_screen));
            lv_anim_set_values(&a_new, lv_obj_get_width(new_screen), 0);
            lv_anim_set_exec_cb(&a_old, transition_slide_anim);
            lv_anim_set_exec_cb(&a_new, transition_slide_anim);
            break;
            
        case TRANSITION_SLIDE_RIGHT:
            // Slide right animation (current exits right, new enters from left)
            lv_anim_set_values(&a_old, 0, lv_obj_get_width(old_screen));
            lv_anim_set_values(&a_new, -lv_obj_get_width(new_screen), 0);
            lv_anim_set_exec_cb(&a_old, transition_slide_anim);
            lv_anim_set_exec_cb(&a_new, transition_slide_anim);
            break;
            
        case TRANSITION_SLIDE_UP:
            // Slide up animation (current exits up, new enters from bottom)
            lv_anim_set_values(&a_old, 0, -lv_obj_get_height(old_screen));
            lv_anim_set_values(&a_new, lv_obj_get_height(new_screen), 0);
            lv_anim_set_path_cb(&a_old, lv_anim_path_ease_in_out);
            lv_anim_set_path_cb(&a_new, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(&a_old, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_exec_cb(&a_new, (lv_anim_exec_xcb_t)lv_obj_set_y);
            break;
            
        case TRANSITION_SLIDE_DOWN:
            // Slide down animation (current exits down, new enters from top)
            lv_anim_set_values(&a_old, 0, lv_obj_get_height(old_screen));
            lv_anim_set_values(&a_new, -lv_obj_get_height(new_screen), 0);
            lv_anim_set_path_cb(&a_old, lv_anim_path_ease_in_out);
            lv_anim_set_path_cb(&a_new, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(&a_old, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_exec_cb(&a_new, (lv_anim_exec_xcb_t)lv_obj_set_y);
            break;
            
        case TRANSITION_ZOOM_IN:
            // Zoom in animation
            lv_obj_set_style_opa(new_screen, LV_OPA_COVER, 0);
            lv_anim_set_values(&a_new, 128, 256); // 128 = 50%, 256 = 100%
            lv_anim_set_exec_cb(&a_new, transition_zoom_anim);
            break;
            
        case TRANSITION_ZOOM_OUT:
            // Zoom out animation
            lv_anim_set_values(&a_old, 256, 128); // 256 = 100%, 128 = 50%
            lv_anim_set_values(&a_new, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_exec_cb(&a_old, transition_zoom_anim);
            lv_anim_set_exec_cb(&a_new, transition_fade_anim);
            break;
            
        case TRANSITION_OVER_LEFT:
        case TRANSITION_OVER_RIGHT:
        case TRANSITION_OVER_TOP:
        case TRANSITION_OVER_BOTTOM:
            // Over animations
            lv_anim_set_values(&a_new, 0, 256);
            lv_anim_set_exec_cb(&a_new, transition_over_anim);
            break;
            
        default:
            // Default to fade if unknown type
            lv_anim_set_values(&a_old, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_values(&a_new, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_exec_cb(&a_old, transition_fade_anim);
            lv_anim_set_exec_cb(&a_new, transition_fade_anim);
            break;
    }
    
    // Set animation path (easing function)
    lv_anim_set_path_cb(&a_old, lv_anim_path_ease_in_out);
    lv_anim_set_path_cb(&a_new, lv_anim_path_ease_in_out);
    
    // Set completion callback for the second animation
    lv_anim_set_ready_cb(&a_old, NULL);
    lv_anim_set_ready_cb(&a_new, transition_anim_ready_cb);
    
    // Set the new screen as active but keep the old one visible
    lv_scr_load_anim(new_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    
    // Start animations
    lv_anim_start(&a_old);
    lv_anim_start(&a_new);
}

// Fade animation callback
static void transition_fade_anim(void* var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
}

// Slide animation callback
static void transition_slide_anim(void* var, int32_t v) {
    lv_obj_set_x((lv_obj_t*)var, v);
}

// Zoom animation callback
static void transition_zoom_anim(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_style_transform_zoom(obj, v, 0);
    
    // Keep the object centered during zoom
    int32_t pivot_x = lv_obj_get_width(obj) / 2;
    int32_t pivot_y = lv_obj_get_height(obj) / 2;
    lv_obj_set_style_transform_pivot_x(obj, pivot_x, 0);
    lv_obj_set_style_transform_pivot_y(obj, pivot_y, 0);
}

// Over animation callback
static void transition_over_anim(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    screen_transition_type_t type = transition_data.type;
    
    // Set opacity to fully visible
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    
    // Position based on transition type and progress (v: 0-256)
    float progress = v / 256.0f;
    
    switch (type) {
        case TRANSITION_OVER_LEFT:
            lv_obj_set_x(obj, (1.0f - progress) * lv_obj_get_width(obj) * -1);
            break;
        case TRANSITION_OVER_RIGHT:
            lv_obj_set_x(obj, (1.0f - progress) * lv_obj_get_width(obj));
            break;
        case TRANSITION_OVER_TOP:
            lv_obj_set_y(obj, (1.0f - progress) * lv_obj_get_height(obj) * -1);
            break;
        case TRANSITION_OVER_BOTTOM:
            lv_obj_set_y(obj, (1.0f - progress) * lv_obj_get_height(obj));
            break;
        default:
            break;
    }
}

// Animation ready callback
static void transition_anim_ready_cb(lv_anim_t* a) {
    // Make sure we fully cleanup and set final states
    lv_obj_t* old_screen = transition_data.old_screen;
    lv_obj_t* new_screen = transition_data.new_screen;
    
    // Reset transforms and opacity
    lv_obj_set_style_opa(new_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_zoom(new_screen, 256, 0); // 256 = 100%
    lv_obj_set_x(new_screen, 0);
    lv_obj_set_y(new_screen, 0);
    
    // Final screen load to ensure proper cleanup
    lv_scr_load(new_screen);
}

#endif // SCREEN_TRANSITION_H
