# Loss Prevention Log - M5Stack CoreS3 Project

## Required Libraries
- Wire.h
- M5CoreS3.h
- lv_conf.h
- lvgl.h (Version 8.4.0)
- WiFi.h
- Preferences.h
- SPI.h
- SD.h
- time.h

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

2. **Data Management**
   - Local storage on SD card
   - WiFi connectivity for time synchronization
   - Formatted entry logging with timestamps

### UI Elements
1. **Screens**
   - `mainScreen`: Home screen with main options
   - `genderMenu`: Gender selection menu
   - `colorMenu`: Color selection for clothing items
   - `itemMenu`: Stolen item selection
   - `confirmScreen`: Entry confirmation
   - `wifi_screen`: WiFi network scanning and connection
   - `wifi_manager_screen`: WiFi settings management

2. **Status Indicators**
   - Battery indicator (top-left corner)
     * Shows percentage and appropriate icon
     * Color-coded: green (>60%), yellow (20-60%), red (<20%)
     * Special cyan color when charging
   - WiFi indicator (top-right corner)
     * Shows connection status and signal strength

3. **Custom Styles**
   - Button styles with consistent appearance
   - Screen background style
   - Title and text styles
   - Keyboard button style

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

#### WiFi Management
- `scanNetworks()`: Scans for available WiFi networks
- `connectToWiFi()`: Handles WiFi connection process
- `updateWifiIndicator()`: Updates WiFi status display
- `loadSavedNetworks()`: Loads saved WiFi credentials
- `saveNetworks()`: Saves WiFi credentials
- `connectToSavedNetworks()`: Attempts to connect to saved networks

#### Status Indicators
- `addBatteryIndicator()`: Adds battery indicator to a screen
- `updateBatteryIndicator()`: Updates battery status display
- `addWifiIndicator()`: Adds WiFi indicator to a screen

#### Data Management
- `getTimestamp()`: Generates current timestamp
- `getFormattedEntry()`: Formats entry data
- `appendToLog()`: Appends entry to log file on SD card
- `saveEntry()`: Processes and saves entry
- `listSavedEntries()`: Lists entries saved on SD card

#### System Functions
- `initFileSystem()`: Initializes SD card file system
- `syncTimeWithNTP()`: Synchronizes time with NTP server
- `initStyles()`: Initializes LVGL styles
- `releaseSPIBus()`: Releases SPI bus for other operations

### Display Configuration
- Screen dimensions: 320x240 pixels
- LVGL configuration:
  - 16-bit color depth
  - LV_COLOR_16_SWAP = 1
  - Display buffer size: SCREEN_WIDTH * 10
  - Custom display flush callback

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

## Technical Implementation Details
- Touch coordinates are properly mapped to the screen
- Battery level monitoring includes smoothing algorithm
- WiFi scanning has timeout handling to prevent hanging
- SD card operations use a dedicated SPI instance
- Memory management includes proper cleanup of screens
- LVGL styles follow version 8.4.0 conventions
- Default WiFi credentials are included but can be overridden
- Debug macros are available for troubleshooting

## Development Notes
- The M5Stack CoreS3 only requires the M5CoreS3.h library
- LVGL 8.4.0 style functions do not accept a state parameter
- Custom keyboard implementation for WiFi password entry
- SD Card pins for M5Stack CoreS3:
  - SCK: 36
  - MISO: 35
  - MOSI: 37
  - CS: 4