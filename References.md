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
   - View Logs Screen
   - Settings Menu

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

### UI Elements
1. **Screens**
   - `mainScreen`: Home screen with main options
   - `genderMenu`: Gender selection menu
   - `colorMenu`: Color selection for clothing items
   - `itemMenu`: Stolen item selection
   - `confirmScreen`: Entry confirmation
   - `wifi_screen`: WiFi network scanning and connection
   - `wifi_manager_screen`: WiFi settings management
   - `settings_screen`: System settings management
   - `logs_screen`: View and navigate saved logs

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
   - Custom fonts (Montserrat 14, 16, and 20)

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
- `createWiFiManagerScreen()`: WiFi network management
- `createViewLogsScreen()`: View saved log entries
- `createSettingsMenu()`: System settings configuration

#### WiFi Management
- `WiFiManager` class: Custom implementation for WiFi management
  - `begin()`: Initializes WiFi functionality
  - `update()`: Updates WiFi state machine
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

#### Touch and Input Handling
- `my_disp_flush()`: Display driver flush callback
- `my_touchpad_read()`: Touch input driver callback
- `handleSwipeLeft()`: Handles left swipe gesture
- `handleSwipeVertical()`: Handles vertical swipe gesture

### Display Configuration
- Screen dimensions: 320x240 pixels
- LVGL configuration:
  - 16-bit color depth
  - LV_COLOR_16_SWAP = 1
  - Display buffer size: SCREEN_WIDTH * 20
  - Custom display flush callback
  - Custom touchpad read callback

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

## Usage Notes
1. The application requires initial WiFi setup for time synchronization
2. Entries can be made offline and stored on the SD card
3. The UI is optimized for touch interaction
4. All screens feature consistent navigation and status indicators
5. The battery indicator updates every 5 seconds to conserve resources
6. The confirmation screen shows a formatted summary of the entry
7. Logs can be viewed, sorted, and navigated by pages
8. Optional webhook integration for remote logging

## Technical Implementation Details
- Touch coordinates are properly mapped to the screen
- Battery level monitoring includes smoothing algorithm
- WiFi scanning has timeout handling to prevent hanging
- SD card operations use a dedicated SPI instance
- Memory management includes proper cleanup of screens
- LVGL styles follow version 8.4.0 conventions
- Default WiFi credentials are included but can be overridden
- Debug macros are available for troubleshooting
- Custom WiFiManager class for robust network handling
- Time synchronization with NTP servers
- ESP32Time library for RTC management

## Development Notes
- The M5Stack CoreS3 uses the M5Unified library
- LVGL 8.4.0 style functions do not accept a state parameter
- Custom keyboard implementation for WiFi password entry
- SD Card pins for M5Stack CoreS3:
  - SCK: 36
  - MISO: 35
  - MOSI: 37
  - CS: 4
- Debug output can be enabled/disabled via DEBUG_ENABLED flag
