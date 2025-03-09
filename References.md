# Loss Prevention Log - M5Stack CoreS3 Project

## Required Libraries
- Wire.h
- M5Unified.h
- M5GFX.h
- lv_conf.h
- lvgl.h (Version 8.4.0)
- WiFi.h
- Preferences.h
- SPI.h
- SD.h
- time.h
- ESP32Time.h
- HTTPClient.h

## Hardware Requirements
- M5Stack CoreS3
- M5Stack Dual Button & Key Unit

## Project Structure

### Main Components
1. **User Interface**
   - Main Menu
   - Gender Selection
   - Color Selection (Shirt, Pants, Shoes)
   - Item Selection
   - Confirmation Screen
   - WiFi Settings Screen
   - WiFi Loading Screen
   - View Logs Screen
   - Settings Menu
   - Brightness Settings Screen
   - Sound Settings Screen

2. **Data Management**
   - Local storage on SD card
   - WiFi connectivity for time synchronization
   - Formatted entry logging with timestamps
   - Webhook integration for remote logging

3. **WiFi Management**
   - Custom WiFiManager class for network management
   - Network scanning and connection handling
   - Saved network prioritization
   - Connection status monitoring
   - Visual connection feedback with loading screen
   - Proper state machine updates in main loop

### UI Elements
1. **Screens**
   - `mainScreen`: Home screen with main options
   - `genderMenu`: Gender selection menu
   - `colorMenu`: Color selection for clothing items
   - `itemMenu`: Stolen item selection
   - `confirmScreen`: Entry confirmation
   - `wifi_screen`: WiFi network scanning and connection
   - `wifi_manager_screen`: WiFi settings management
   - `wifi_loading_screen`: Visual feedback during WiFi connection attempts
   - `settingsScreen`: System settings management
   - `logs_screen`: View and navigate saved logs
   - `brightness_settings_screen`: Display brightness adjustment
   - `sound_settings_screen`: Sound settings configuration

2. **Status Indicators**
   - Battery indicator (top-left corner)
     * Shows percentage and appropriate icon
     * Color-coded: green (>60%), yellow (20-60%), red (<20%)
     * Special cyan color when charging
   - WiFi indicator (top-right corner)
     * Shows connection status and signal strength
   - Status bar
     * Displays system messages and notifications

3. **Custom Styles**
   - Button styles with consistent appearance
   - Screen background style
   - Title and text styles
   - Keyboard button style
   - Custom fonts (Montserrat 14, 16, 20, and 24)

4. **Scrollable Lists**
   - Implemented for saved networks list
   - Individual items within lists are non-scrollable
   - Headers are non-scrollable for better user experience
   - Proper padding between list items

### Key Functions

#### Screen Creation
- `createMainMenu()`: Creates the main menu interface
- `createGenderMenu()`: Displays gender selection options
- `createColorMenuShirt()`: Shirt color selection menu
- `createColorMenuPants()`: Pants color selection menu
- `createColorMenuShoes()`: Shoes color selection menu
- `createItemMenu()`: Shows available item options
- `createConfirmScreen()`: Shows entry confirmation
- `createWiFiScreen()`: WiFi network selection interface
- `createWiFiManagerScreen()`: WiFi network management with scrollable list
- `showWiFiLoadingScreen()`: Displays loading screen during WiFi connection
- `updateWiFiLoadingScreen()`: Updates loading screen based on connection result
- `createViewLogsScreen()`: View saved log entries with non-scrollable header
- `createSettingsScreen()`: System settings configuration
- `createBrightnessSettingsScreen()`: Display brightness adjustment
- `createSoundSettingsScreen()`: Sound settings configuration

#### WiFi Management
- `WiFiManager` class: Custom implementation for WiFi management
  - `begin()`: Initializes WiFi functionality
  - `update()`: Updates WiFi state machine (crucial for proper operation)
  - `connect()`: Connects to specified network
  - `startScan()`: Initiates network scanning
  - `addNetwork()`: Adds network to saved list
  - `removeNetwork()`: Removes network from saved list
  - `setNetworkPriority()`: Sets connection priority
- `scanNetworks()`: Scans for available WiFi networks
- `connectToWiFi()`: Handles WiFi connection process
- `updateWifiIndicator()`: Updates WiFi status display
- `loadSavedNetworks()`: Loads saved WiFi credentials
- `saveNetworks()`: Saves WiFi credentials
- `connectToSavedNetworks()`: Attempts to connect to saved networks
- `onWiFiStatus()`: Callback for WiFi status updates
- `onWiFiScanComplete()`: Callback for scan completion

#### Settings Management
- `loadSettings()`: Loads saved settings from Preferences
- `saveSettings()`: Saves settings to Preferences
- `updateBrightness()`: Updates display brightness
- `toggleSound()`: Toggles sound on/off
- `adjustVolume()`: Adjusts system volume
- `toggleAutoBrightness()`: Toggles auto-brightness feature

#### Status Indicators
- `addStatusBar()`: Adds status bar to a screen
- `updateStatus()`: Updates status message
- `addBatteryIndicator()`: Adds battery indicator to a screen
- `updateBatteryIndicator()`: Updates battery status display
- `addWifiIndicator()`: Adds WiFi indicator to a screen

#### Data Management
- `getTimestamp()`: Generates current timestamp
- `getFormattedEntry()`: Formats entry data
- `appendToLog()`: Appends entry to log file on SD card
- `saveEntry()`: Processes and saves entry
- `listSavedEntries()`: Lists entries saved on SD card
- `sendWebhook()`: Sends entry data to remote server
- `parseTimestamp()`: Parses timestamp from log entry

#### System Functions
- `initFileSystem()`: Initializes SD card file system
- `syncTimeWithNTP()`: Synchronizes time with NTP server
- `initStyles()`: Initializes LVGL styles
- `releaseSPIBus()`: Releases SPI bus for other operations
- `setSystemTimeFromRTC()`: Sets system time from M5Stack RTC
- `println_log()`: Helper for logging to Serial and Display
- `printf_log()`: Formatted logging to Serial and Display
- `DEBUG_PRINT()`: Debug logging macro
- `DEBUG_PRINTF()`: Formatted debug logging macro

#### Touch and Input Handling
- `my_disp_flush()`: Display driver flush callback
- `my_touchpad_read()`: Touch input driver callback
- `handleSwipeLeft()`: Handles left swipe gesture
- `handleSwipeVertical()`: Handles vertical swipe gesture

### UI Enhancements

#### Scrollable Lists
- Used for saved networks list in WiFi manager screen
- Individual network items are non-scrollable for better UX
- Implemented with `lv_obj_set_scroll_dir()` and `lv_obj_clear_flag()`
- Proper padding between items using `lv_obj_set_style_pad_bottom()`

#### Non-Scrollable Headers
- Headers in various screens are set as non-scrollable
- Implemented with `lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE)`
- Ensures titles remain visible during content scrolling

#### WiFi Connection Feedback
- Loading screen with spinner during connection attempts
- Visual feedback for successful or failed connections
- Automatic return to WiFi manager after successful connection
- Manual return option for failed connections

### Display Configuration
- Screen dimensions: 320x240 pixels
- LVGL configuration:
  - 16-bit color depth
  - LV_COLOR_16_SWAP = 1
  - Display buffer size: SCREEN_WIDTH * 20
  - Custom display flush callback
  - Custom touchpad read callback
- Brightness control:
  - Range: 10-255 (4-100%)
  - Preset options: Low (50), Medium (150), High (250)
  - Auto-brightness option
  - Saved in Preferences

### WiFi Configuration
- Scan timeout handling with proper state machine updates
- Connection status feedback with loading screen
- Saved networks stored in Preferences
- Network priority system for automatic connections
- Scrollable saved networks list with non-scrollable items

### Color Options
Available colors for shirt items:
```cpp
const char* shirtColors[] = {
    "Red", "Orange", "Yellow", "Green", "Blue", "Purple", 
    "Black", "White"
};
```

Available colors for pants items:
```cpp
const char* pantsColors[] = {
    "Black", "Blue", "Grey", "Khaki", "Brown", "Navy",
    "White", "Beige"
};
```

Available colors for shoe items:
```cpp
const char* shoeColors[] = {
    "Black", "Brown", "White", "Grey", "Navy", "Red",
    "Blue", "Green"
};
```

### Item Options
Available items for loss reporting:
```cpp
const char* items[] = {
    "Jewelry", "Women's Shoes", "Men's Shoes", "Cosmetics", 
    "Fragrances", "Home", "Kids"
};
```

## Important Implementation Notes

### WiFi Scanning
- WiFiManager::update() must be called in the main loop
- This ensures proper processing of WiFi events and state changes
- Without this call, WiFi scanning will time out

### Scrollable Lists
- For proper scrolling behavior:
  1. Set the parent container as scrollable with `lv_obj_set_scroll_dir()`
  2. Make individual items non-scrollable with `lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE)`
  3. Add proper padding between items with `lv_obj_set_style_pad_bottom()`

### Non-Scrollable Headers
- To prevent headers from scrolling:
  1. Create a header container
  2. Clear the scrollable flag with `lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE)`
  3. Add content to the header

### WiFi Connection Feedback
- The loading screen provides visual feedback during connection attempts
- It automatically returns to the WiFi manager after successful connection
- For failed connections, it shows a "Back" button
- This improves user experience by providing clear status information
