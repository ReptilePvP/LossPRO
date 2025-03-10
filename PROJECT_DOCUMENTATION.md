# Loss Prevention Log System Documentation

## Project Overview

The Loss Prevention Log system is an IoT device built on the M5Stack CoreS3 platform with M5Stack Dual button & Key unit for input. It serves as a digital logging system for retail loss prevention, allowing staff to quickly document suspicious activities or incidents with standardized information including gender, clothing colors, and items of interest.

## Hardware Components

### Primary Hardware
- **M5Stack CoreS3**: The main computing and display unit featuring:
  - ESP32-S3 microcontroller
  - 2" IPS LCD touchscreen (320x240 resolution)
  - Built-in battery management
  - RTC (Real-Time Clock)
  - SD card slot for data storage
  - Wi-Fi connectivity

### Input Peripherals
- **M5Stack Dual button & Key unit**: Provides physical button inputs for navigation and interaction
  - Used for scrolling through menus and confirming selections
  - Complements the touchscreen interface

## Software Architecture

### Core Components

1. **Main Application (`Loss_Prevention_Log.ino`)**
   - Handles the overall application flow and user interface
   - Manages the LVGL graphics library for UI rendering
   - Implements the loss prevention logging workflow
   - Manages file system operations for log storage

2. **WiFi Management (`WiFiManager.cpp` & `WiFiManager.h`)**
   - Custom WiFi connection management system
   - Handles network scanning, connection, and reconnection
   - Stores and prioritizes saved networks
   - Provides callbacks for connection status updates
   - Includes visual feedback for connection attempts with loading screen

3. **Screen Transitions (`screen_transition.h`)**
   - Provides smooth and consistent transitions between screens
   - Implements multiple transition types:
     - Fade transitions for subtle screen changes
     - Slide transitions (left/right/up/down) for directional navigation
     - Zoom transitions for emphasis on new screens
     - Over transitions for overlay screens
   - Configurable animation duration and easing functions
   - Optimized for M5Stack CoreS3 performance

4. **Card-Style UI Components**
   - Modern card-based interface design
   - Consistent styling across all screens
   - Enhanced button styles with pressed state animations
   - Improved visual hierarchy with cards for content grouping
   - Better touch target sizing for improved usability
   - Responsive layout adapting to screen orientation

5. **User Interface**
   - Built with LVGL (Light and Versatile Graphics Library) (8.4.0)
   - Features multiple screens for different functions:
     - Main menu
     - Gender selection
     - Color selection (shirt, pants, shoes)
     - Item selection
     - Confirmation screen
     - WiFi configuration with loading screen
     - Log viewing
     - Settings screens (sound, brightness)
   - Optimized scrollable lists with non-scrollable headers and individual items
   - Visual feedback for WiFi connection attempts
   - Smooth transitions between screens for improved user experience

6. **Data Storage**
   - Uses SD card for log file storage
   - Implements file operations with error handling
   - Stores logs in a text-based format with timestamps

7. **Time Synchronization**
   - NTP (Network Time Protocol) for internet time synchronization
   - Fallback to RTC when internet is unavailable
   - Time display in 12-hour format with AM/PM

8. **Network Connectivity**
   - WiFi connection for time synchronization
   - Optional webhook functionality for remote logging
   - Enhanced WiFi scanning with proper state management
   - Visual connection status feedback

## Functional Workflow

1. **Startup Sequence**
   - Initialize hardware components (display, touch, SD card)
   - Set up LVGL UI framework
   - Load saved WiFi networks and attempt connection
   - Synchronize time with NTP if connected
   - Display main menu

2. **Logging Process**
   - Select gender (Male/Female)
   - Select clothing colors (shirt, pants, shoes)
   - Select items of interest
   - Confirm and save entry
   - Entry is timestamped and saved to SD card
   - Optional: Send entry to webhook if configured

3. **Log Management**
   - View saved logs with pagination
   - Logs are displayed with timestamps
   - Sorted by recency (newest first)
   - Non-scrollable headers for better navigation

4. **Settings Management**
   - WiFi configuration with visual connection feedback
   - Sound settings
   - Brightness control
   - Time settings

5. **WiFi Management**
   - Scan for available networks
   - Connect to selected networks with loading screen feedback
   - Manage saved networks with scrollable list
   - Prioritize networks for automatic connection

## UI Enhancements

### Card-Style UI
- Modern card-based interface design for content organization
- Elevated appearance with subtle shadows and rounded corners
- Consistent styling across all screens for visual coherence
- Improved touch targets for better usability
- Visual feedback on interaction (pressed states, animations)
- Optimized for the M5Stack CoreS3 display size and resolution

### Screen Transitions
- Smooth animated transitions between screens
- Multiple transition types for different navigation contexts:
  - Slide Left/Right: For forward/backward navigation
  - Slide Up/Down: For hierarchical navigation
  - Fade: For subtle screen changes
  - Zoom In/Out: For emphasis on new screens or returning to previous screens
- Consistent navigation patterns:
  - Forward navigation uses slide left or zoom in
  - Backward navigation uses slide right or zoom out
  - Settings screens use slide up transitions
  - Confirmation screens use fade transitions
- Configurable animation duration (default: 300ms)
- Performance optimizations for smooth animations

### Scrollable Lists
- Implemented in the WiFi manager screen for saved networks list
- Individual network items are non-scrollable for better user experience
- Proper padding between items for visual separation

### Non-Scrollable Headers
- Headers in various screens (logs, settings, etc.) are set as non-scrollable
- Improves user experience by keeping titles visible during scrolling

### WiFi Connection Feedback
- Loading screen with spinner during connection attempts
- Visual feedback for successful or failed connections
- Automatic return to WiFi manager after successful connection
- Manual return option for failed connections

## Key Files and Their Functions

### Main Application Files
- **`Loss_Prevention_Log.ino`**: Main application entry point and core functionality
- **`WiFiManager.cpp`**: Implementation of WiFi management functionality
- **`WiFiManager.h`**: Header file defining the WiFiManager class and related structures
- **`screen_transition.h`**: Implementation of screen transition effects and animations

### Documentation Files
- **`README.md`**: Overview and quick-start documentation
- **`PROJECT_DOCUMENTATION.md`**: This detailed technical documentation
- **`References.md`**: Reference materials and function listings
- **`Card Style UI.md`**: Implementation details for the card-style UI
- **`SCREEN TRANSITIONS IMPLEMENTATION GUIDE.md`**: Guide for implementing screen transitions

### Data Files
- **`log.txt`**: Main log file stored on the SD card
- **`wifi_config`**: Saved in Preferences storage for WiFi credentials

## Important Functions

### UI Management
- `createMainMenu()`: Creates the main application menu
- `createGenderMenu()`: Creates the gender selection screen
- `createColorMenuShirt()`, `createColorMenuPants()`, `createColorMenuShoes()`: Color selection screens
- `createItemMenu()`: Creates the item selection screen
- `createConfirmScreen()`: Creates the confirmation screen
- `createViewLogsScreen()`: Creates the log viewing screen with non-scrollable header
- `createSettingsScreen()`: Creates the settings menu
- `createWiFiManagerScreen()`: Creates the WiFi manager screen with scrollable network list
- `showWiFiLoadingScreen()`: Shows loading screen during WiFi connection attempts
- `updateWiFiLoadingScreen()`: Updates the WiFi loading screen based on connection results

### Screen Transitions
- `load_screen_with_animation()`: Main function for loading screens with transitions
- `transition_fade_anim()`: Handles fade transition animations
- `transition_slide_anim()`: Handles slide transition animations
- `transition_zoom_anim()`: Handles zoom transition animations
- `transition_over_anim()`: Handles overlay transition animations
- `transition_anim_ready_cb()`: Callback function when animations complete

### WiFi Management
- `WiFiManager::connect()`: Connects to a specific WiFi network
- `WiFiManager::startScan()`: Initiates a WiFi network scan
- `WiFiManager::update()`: Updates the WiFi state machine to process events and state changes
- `WiFiManager::addNetwork()`: Adds a network to saved networks
- `scanNetworks()`: Scans for available WiFi networks
- `connectToSavedNetworks()`: Attempts to connect to previously saved networks
- `onWiFiStatus()`: Callback for WiFi connection status updates

### Data Management
- `appendToLog()`: Appends a new entry to the log file
- `getFormattedEntry()`: Formats the entry with all selected attributes
- `getTimestamp()`: Gets the current timestamp for log entries
- `listSavedEntries()`: Lists all saved log entries
- `saveEntry()`: Saves the current entry to storage

### System Functions
- `initFileSystem()`: Initializes the SD card file system
- `syncTimeWithNTP()`: Synchronizes time with NTP servers
- `updateBatteryIndicator()`: Updates the battery level indicator
- `updateWifiIndicator()`: Updates the WiFi connection indicator

## UI Enhancements

### Scrollable Lists
- Implemented in the WiFi manager screen for saved networks list
- Individual network items are non-scrollable for better user experience
- Proper padding between items for visual separation

### Non-Scrollable Headers
- Headers in various screens (logs, settings, etc.) are set as non-scrollable
- Improves user experience by keeping titles visible during scrolling

### WiFi Connection Feedback
- Loading screen with spinner during connection attempts
- Visual feedback for successful or failed connections
- Automatic return to WiFi manager after successful connection
- Manual return option for failed connections

## Hardware Interfaces

### Display
- 320x240 pixel IPS LCD touchscreen
- Managed through LVGL library
- Touch input handled by `my_touchpad_read()` function

### SD Card
- Connected via SPI interface
- Pins defined in the main sketch:
  - SCK: 36
  - MISO: 35
  - MOSI: 37
  - CS: 4

### Button Interface
- M5Stack Dual button & Key unit connected via I2C
- I2C address: 0x58 (AW9523 chip)
- Used for navigation and selection

## Development References

### M5Stack CoreS3 Resources
- [M5Stack CoreS3 Official Documentation](https://docs.m5stack.com/en/core/CoreS3)
- [M5Stack CoreS3 Pinout Diagram](https://docs.m5stack.com/en/core/CoreS3)
- [M5Stack CoreS3 GitHub Repository](https://github.com/m5stack/M5CoreS3)

### M5Stack Dual Button & Key Unit Resources
- [Dual Button Unit Documentation](https://docs.m5stack.com/en/unit/dual_button)
- [Key Unit Documentation](https://docs.m5stack.com/en/unit/key)

### Libraries
- [M5Unified](https://github.com/m5stack/M5Unified): Unified library for M5Stack devices
- [M5GFX](https://github.com/m5stack/M5GFX): Graphics library for M5Stack
- [LVGL](https://lvgl.io/): Light and Versatile Graphics Library
- [ESP32Time](https://github.com/fbiego/ESP32Time): Time management library for ESP32

## Future Development Considerations

### Potential Enhancements
1. **Cloud Integration**
   - Implement full cloud synchronization of logs
   - Add user authentication for secure access

2. **Advanced UI Features**
   - Add photo capture capability
   - Implement search functionality for logs
   - Add filtering options for log viewing

3. **Hardware Expansion**
   - Support for additional M5Stack modules
   - Barcode scanner integration
   - RFID reader support

4. **Performance Optimizations**
   - Battery life improvements
   - Memory usage optimization
   - Faster UI rendering

### Known Limitations
1. Limited battery life when WiFi is enabled
2. SD card operations can be slow for large log files
3. Touch interface may require calibration in some environments

## Troubleshooting Guide

### Common Issues
1. **WiFi Connection Problems**
   - Check that WiFi credentials are correct
   - Ensure WiFiManager::update() is called in the main loop
   - Verify the WiFi network is within range
   - Try resetting saved networks if persistent issues occur

2. **SD Card Issues**
   - Ensure SD card is properly formatted (FAT32)
   - Check that SD card is properly inserted
   - Verify SPI bus is properly released after operations
   - Try a different SD card if problems persist

3. **Display Issues**
   - Adjust brightness settings
   - Recalibrate touch if input is not registering correctly
   - Check for LVGL memory allocation issues if UI elements are missing

4. **Battery Issues**
   - Ensure device is properly charged
   - Reduce brightness to extend battery life
   - Disable WiFi when not needed
   - Check battery indicator for accurate readings

### Debugging Tools
1. Serial monitor output with DEBUG_PRINT and DEBUG_PRINTF macros
2. On-screen status messages for user feedback
3. Visual indicators for WiFi and battery status
