/*
 * ScreenManager for M5Stack CoreS3
 * This file provides utilities for managing screens and UI elements
 */

#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include <lvgl.h>
#include "screen_transition.h"

// Forward declarations
class ScreenManager;

// Screen types enum
typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_SETTINGS,
    SCREEN_LOG_ENTRY,
    SCREEN_LOG_VIEW,
    SCREEN_POWER_MANAGEMENT,
    SCREEN_BRIGHTNESS,
    SCREEN_WIFI_CONFIG,
    SCREEN_CUSTOM_1,
    SCREEN_CUSTOM_2,
    // Add more screen types as needed
} screen_type_t;

// Screen base class
class BaseScreen {
protected:
    lv_obj_t* screen;
    ScreenManager* manager;

public:
    BaseScreen(ScreenManager* mgr) : manager(mgr), screen(nullptr) {}
    virtual ~BaseScreen() {
        if (screen) {
            lv_obj_del(screen);
            screen = nullptr;
        }
    }

    // Pure virtual functions that must be implemented by derived classes
    virtual void create() = 0;
    virtual void show(screen_transition_type_t transition = TRANSITION_FADE) = 0;
    virtual void hide() = 0;
    virtual void update() = 0;
    
    // Getters
    lv_obj_t* getScreen() { return screen; }
};

// Screen Manager class
class ScreenManager {
private:
    BaseScreen* current_screen;
    BaseScreen* screens[10]; // Array to hold pointers to screen objects
    uint8_t screen_count;
    
public:
    ScreenManager() : current_screen(nullptr), screen_count(0) {
        // Initialize screen pointers to nullptr
        for (int i = 0; i < 10; i++) {
            screens[i] = nullptr;
        }
    }
    
    ~ScreenManager() {
        // Clean up all screens
        for (int i = 0; i < 10; i++) {
            if (screens[i]) {
                delete screens[i];
                screens[i] = nullptr;
            }
        }
    }
    
    // Register a screen with the manager
    void registerScreen(screen_type_t type, BaseScreen* screen) {
        if (type < 10 && screen) {
            // Delete old screen if exists
            if (screens[type]) {
                delete screens[type];
            }
            
            screens[type] = screen;
            
            // Update screen count if needed
            if (type >= screen_count) {
                screen_count = type + 1;
            }
        }
    }
    
    // Switch to a specific screen
    void switchToScreen(screen_type_t type, screen_transition_type_t transition = TRANSITION_FADE) {
        if (type < 10 && screens[type]) {
            BaseScreen* next_screen = screens[type];
            
            // Create the screen if it hasn't been created yet
            if (!next_screen->getScreen()) {
                next_screen->create();
            }
            
            // Show the new screen with transition
            next_screen->show(transition);
            
            // Update current screen pointer
            current_screen = next_screen;
        }
    }
    
    // Get the current active screen
    BaseScreen* getCurrentScreen() {
        return current_screen;
    }
    
    // Get a specific screen by type
    BaseScreen* getScreen(screen_type_t type) {
        if (type < 10) {
            return screens[type];
        }
        return nullptr;
    }
    
    // Update all screens (or just the current one)
    void updateScreens(bool current_only = true) {
        if (current_only && current_screen) {
            current_screen->update();
        } else {
            for (int i = 0; i < screen_count; i++) {
                if (screens[i]) {
                    screens[i]->update();
                }
            }
        }
    }
};

#endif // SCREEN_MANAGER_H
